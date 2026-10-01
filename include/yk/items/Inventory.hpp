#pragma once
#include "yk/items/Items.hpp"
#include "yk/scene/Registry.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace yk {
class GameContext;

// The things a character, a desk, a locker, a shop or a hole in the wall holds. General slots hold
// stacks of items (merged up to each item's stack size); named equipment slots ("Outfit",
// "Weapon", "Tool"... whatever the game configures) hold what is worn or wielded, and while an item
// is in one its effect (stats, flags, permissions, appearance) is on the character.
//
// Every change raises events with the inventory's entity as the source:
//   item.added / item.removed     data: item, count
//   item.equipped / item.unequipped   data: item, slot
//   item.used, item.broke         data: item
//   inventory.changed             after any of them, for a screen to redraw
class Inventory final : public Component {
  public:
    int slots{20};
    std::vector<std::string> equipmentSlots; // Names; empty: nothing can be worn or wielded.
    std::string owner;                       // Persistent id of who or what it belongs to.
    Json startItems{Json::array()}; // [{"item": "screwdriver", "count": 1, "equipped": true}]
    float collectRadius{0.0F};      // Above 0: picks up pickups within this many meters.
    static void describe(TypeBuilder<Inventory> &type);

    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onDestroy(GameContext &context) override;
    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;

    // ---- Looking ---------------------------------------------------------------------------
    int size() const {
        return static_cast<int>(slots_.size());
    }
    // The stack in a slot; an empty stack for a slot that does not exist.
    const ItemStack &slot(int index) const;
    // What is in an equipment slot (null: nothing, or no such slot).
    const ItemStack *equipped(std::string_view equipmentSlot) const;
    // How many of an item there are, in the slots and worn.
    int count(const ItemId &item) const;
    bool has(const ItemId &item, int atLeast = 1) const {
        return count(item) >= atLeast;
    }
    int countWithTag(GameContext &context, std::string_view tag) const;
    // The highest durability among the stacks of an item (-1: none, or it does not wear).
    int bestDurability(const ItemId &item) const;
    int freeSlots() const;
    // How many of the stack would fit (merging into stacks and using free slots).
    int canAdd(GameContext &context, const ItemStack &stack) const;
    // Changes since the game started, so a screen can tell when to redraw.
    std::uint64_t revision() const {
        return revision_;
    }

    // ---- Changing --------------------------------------------------------------------------
    // Puts the stack in; returns how many did not fit (and are still the caller's).
    int add(GameContext &context, ItemStack stack);
    // Makes a stack of `count` of an item (full durability) and adds it; the count that did not
    // fit.
    int addItem(GameContext &context, std::string_view item, int count = 1);
    // Takes up to `count` of an item out (from the slots first, then what is worn); the count
    // taken.
    int remove(GameContext &context, const ItemId &item, int count = 1);
    // Takes up to `count` from a slot and hands them over (an empty stack when there is nothing).
    ItemStack take(GameContext &context, int slotIndex, int count = 1 << 20);
    // Moves a stack onto another slot: merges when they match, swaps otherwise.
    bool move(GameContext &context, int from, int to);
    // Puts a stack into a slot: when it is empty, or holds a matching stack with room (merged; what
    // does not fit comes back in the result). False and unchanged when something else is there.
    bool put(GameContext &context, int slotIndex, ItemStack &stack);
    // Splits `count` off a stack into the first free slot.
    bool split(GameContext &context, int slotIndex, int count);
    // Wears or wields the item in a slot (one of a stack); what was there goes back to the slot.
    bool equip(GameContext &context, int slotIndex);
    // Takes what is in an equipment slot off, into a free slot.
    bool unequip(GameContext &context, std::string_view equipmentSlot);
    // Uses the item in a slot: its actions run for this character, it may be used up or worn, and
    // it cannot be used again for its cooldown. `target` is what it was used on.
    bool use(GameContext &context, int slotIndex, EntityId target = {});
    // Empties it (the contents are gone, not dropped).
    void clear(GameContext &context);

    // Where an item is.
    struct Location {
        int slot{-1};          // A general slot, or -1
        std::string equipSlot; // an equipment slot
        bool valid() const {
            return slot >= 0 || !equipSlot.empty();
        }
    };
    struct Tool {
        Location where;
        ItemId item;
        double efficiency{0.0};
    };
    // The best tool for an action among what the character carries and wears: it must do the action
    // and not be worn out.
    std::optional<Tool> findTool(GameContext &context, std::string_view action) const;
    // Wears an item down (a tool used, a weapon swung); true when it broke and is gone.
    bool wear(GameContext &context, const Location &where, int amount);
    ItemStack *stackAt(const Location &where);

    // Puts the stack on the ground at `at` (merging into a stack already there): see Pickup.
    bool drop(GameContext &context, int slotIndex, int count, Vec2 at);

  private:
    void changed(GameContext &context);
    void announce(GameContext &context, const char *event, const ItemStack &stack,
                  const std::string &slot = {});
    int stackLimit(GameContext &context, const ItemId &item) const;
    void applyEquipment(GameContext &context, const ItemStack &stack, bool on);
    int firstFree() const;

    std::vector<ItemStack> slots_;
    std::vector<std::pair<std::string, ItemStack>> worn_;
    std::map<std::string, double> readyAt_; // Item id -> game time it can be used again.
    std::uint64_t revision_{0};
    bool started_{false};
    ItemStack none_;
};

// Whether the character holds a permission token: a key or keycard in its inventory (an item whose
// "grants" list names the token), or an effect on it that grants it.
bool holdsToken(GameContext &context, const Entity &who, std::string_view token);

// A container in the world: a locker, a desk, a chest, a vending machine, a body. It holds an
// Inventory (on the same entity) and says who may open it, how long searching it takes and which
// of its contents are hidden until it is searched. Raises container.opened / container.closed
// (source: the container; other: who), container.searched, and container.taken (what a character
// took out of it, with whose it was).
class Container final : public Component {
  public:
    std::string
        owner; // Whose it is (persistent id, or a faction id); informational for rules and AI.
    bool locked{false};
    std::string unlockToken; // The permission token that opens it while locked (a key's grant).
    Json openRequires; // A rule condition ({"type": "HasItem", ...}) that must hold; actor is who
                       // opens.
    float searchSeconds{3.0F};
    int hiddenSlots{0}; // The last this-many slots are not shown until it has been searched.
    static void describe(TypeBuilder<Container> &type);

    void onStart(GameContext &context) override;
    void onDestroy(GameContext &context) override;
    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;

    bool searched() const {
        return searched_;
    }
    bool isLocked() const {
        return locked;
    }
    bool canOpen(GameContext &context, const Entity &who, std::string *whyNot = nullptr) const;
    bool open(GameContext &context, Entity &who);
    void close(GameContext &context, Entity &who);
    bool openBy(EntityId who) const;
    void setLocked(GameContext &context, bool value);
    // Reveals the hidden slots (the search happened).
    void search(GameContext &context, Entity &who);
    // How many of the container's slots `who` can see: all once it is searched, else the visible
    // ones.
    int visibleSlots() const;
    // Moves a whole slot from the container to `who`'s inventory (what does not fit stays); the
    // count moved. The container must be open by `who`.
    int take(GameContext &context, Entity &who, int slotIndex);
    // And back: a slot of the character's inventory into the container.
    int put(GameContext &context, Entity &who, int whoSlot);

  private:
    std::vector<EntityId> openBy_;
    bool searched_{false};
};

// An item lying in the world. Characters with an Inventory that has a collect radius pick it up by
// walking close (when `autoCollect`), others by an interaction that calls collect(); it must be on
// the same level. Dropped items merge with matching ones on the ground.
class Pickup final : public Component {
  public:
    std::string item; // What it is, as authored.
    int count{1};
    bool autoCollect{true};
    float lifetime{0.0F}; // Seconds on the ground before it vanishes; 0: forever.
    bool bob{false};      // Hovers, to catch the eye.
    static void describe(TypeBuilder<Pickup> &type);

    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onDestroy(GameContext &context) override;
    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;

    const ItemStack &stack() const {
        return stack_;
    }
    void setStack(GameContext &context, ItemStack stack);
    // Gives what fits to the collector; the pickup goes when it is empty. True when anything moved.
    bool collect(GameContext &context, Entity &collector);

    // Creates a pickup of the stack at a position and level (that of `from` when given); merges
    // into a matching one close by. Returns the entity (null when it could not be made).
    static EntityId place(GameContext &context, const ItemStack &stack, Vec2 at,
                          const Entity *from = nullptr);
    static constexpr float mergeDistance = 0.45F;

  private:
    ItemStack stack_;
    double age_{0.0};
    float baseY_{0.0F};
    bool placed_{false};
};

void registerItemRules(RuleCatalog &catalog);
void registerItemComponents(ComponentRegistry &registry);
} // namespace yk
