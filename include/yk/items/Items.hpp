#pragma once
#include "yk/core/Json.hpp"
#include "yk/core/Result.hpp"
#include "yk/data/Table.hpp"
#include "yk/rules/Rules.hpp"
#include "yk/stats/Stats.hpp"
#include <compare>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Items are data: a project's item files say what exists (a screwdriver, a guard's outfit, a
// sandwich), what it can be used for, and what carrying or wearing it does. Nothing here knows a
// game; "contraband" and "tool" are tags the game's rules give meaning to.
namespace yk {
// The stable name of an item: what files, saved games, rules and scripts refer to. Never a pointer
// or an index, which would change between runs and versions.
class ItemId {
  public:
    ItemId() = default;
    explicit ItemId(std::string id) : id_(std::move(id)) {}
    const std::string &str() const {
        return id_;
    }
    bool empty() const {
        return id_.empty();
    }
    friend bool operator==(const ItemId &, const ItemId &) = default;
    friend auto operator<=>(const ItemId &, const ItemId &) = default;

  private:
    std::string id_;
};

// A weapon's numbers (used by the combat model; an item without them is not a weapon).
struct ItemWeapon {
    double damage{1.0};
    double range{1.0}; // Meters from the wielder.
    double arc{90.0};  // Degrees of the swing.
    double staminaCost{0.0};
    double cooldown{0.5}; // Seconds between attacks.
    double knockback{0.0};
    std::string damageType{"blunt"};
    double chargeSeconds{0.0}; // Hold this long for a charged attack (0: no charged attack).
    double chargeMultiplier{1.5};
    int durabilityCost{1}; // Per hit.
};

// What wearing or holding the item does. It becomes a status effect ("equip:<item id>") that is on
// the character while the item is in the slot, so stats, flags other systems ask about (disguise,
// noise), factors, permissions and even other effects all use the one mechanism.
struct ItemEquip {
    std::string slot; // One of the equipment slots of the character's Inventory.
    std::vector<StatModifierSpec> modifiers;
    std::vector<std::string> flags;
    std::map<std::string, double> factors;
    std::vector<std::string> grants;
    std::vector<std::string> effects; // Other status effects applied while it is equipped.
    Json appearance;                  // Layers of the character's look: {"outfit": "assets/..png"}.
};

// What using the item does (eating, reading, switching on).
struct ItemUse {
    std::vector<Action> actions; // Rule actions: the user is the actor, the item's owner is self.
    Condition condition;         // The JSON key is "requires".
    bool consume{false};         // One of the stack is used up.
    double cooldown{0.0};        // Seconds before the same item (by id) can be used again.
    int durabilityCost{0};
};

struct ItemDefinition {
    std::string id;
    std::string file;
    std::string displayName;
    std::string description;
    std::string category;    // For sorting and filtering in lists ("tools", "food").
    std::string icon;        // Texture path.
    std::string worldPrefab; // What it looks like on the ground (a prefab with a Pickup); optional.
    std::vector<std::string> tags;
    int stackSize{1};
    int durability{0}; // 0: it does not wear out.
    int value{0};      // What it is worth (the economy's unit).
    double weight{0.0};
    std::map<std::string, double> toolActions; // "Dig": 12 - what the item can do and how well.
    std::vector<std::string> grants; // Permission tokens held by whoever carries it (keys).
    std::optional<ItemWeapon> weapon;
    std::optional<ItemEquip> equip;
    std::optional<ItemUse> use;
    Json data; // Anything else the game's rules and scripts want to read.

    ItemId itemId() const {
        return ItemId(id);
    }
    bool hasTag(std::string_view tag) const {
        return data::has(tags, tag);
    }
    // The efficiency at an action, 0 when the item cannot do it.
    double toolEfficiency(std::string_view action) const;
    static Result<ItemDefinition> fromJson(const Json &json, std::vector<std::string> &warnings);
    Json toJson() const;
};

// The id of the status effect that is on a character while the item is equipped.
std::string equipEffectId(const std::string &itemId);

// A number of one kind of item, with what is particular to this stack: how worn it is, whose it is,
// and anything the game attached.
struct ItemStack {
    ItemId item;
    int count{0};
    int durability{-1}; // Remaining; -1 when the item does not wear out.
    std::string owner;  // Persistent id of who it belongs to; empty: nobody (or anybody).
    Json meta;          // Per-stack data (a custom name, a note, who made it); null when none.

    bool empty() const {
        return item.empty() || count <= 0;
    }
    // The same kind of thing: stacks that match can be merged.
    bool matches(const ItemStack &other) const {
        return item == other.item && durability == other.durability && owner == other.owner &&
               meta == other.meta;
    }
    friend bool operator==(const ItemStack &, const ItemStack &) = default;
    static Result<ItemStack> fromJson(const Json &json);
    Json toJson() const;
};

using ItemTable = DefinitionTable<ItemDefinition>;

struct ItemCatalog {
    ItemTable items;
    // The "items" section of a data file.
    void load(const Json &document, const std::string &file, std::vector<DataProblem> &problems);
    // Items that refer to stats, effects or other things that do not exist.
    void check(const StatCatalog &stats, std::vector<DataProblem> &problems) const;
    void visitRules(const RuleSourceVisitor &visit) const;
    // Where the definitions point at files of the project ("texture", "prefab"): file, label, path.
    void visitAssets(
        const std::function<void(const std::string &file, const std::string &label,
                                 const std::string &path, const std::string &kind)> &visit) const;
    // Adds the "equip:<item>" effects to `effects` (called once everything is loaded).
    void synthesizeEffects(EffectTable &effects, std::vector<DataProblem> &problems) const;
    // A new stack of `count` of an item (full durability); empty when the item does not exist.
    ItemStack make(std::string_view id, int count = 1) const;
    // Items that carry the tag, in definition order.
    std::vector<const ItemDefinition *> withTag(std::string_view tag) const;
};
} // namespace yk

template <> struct std::hash<yk::ItemId> {
    std::size_t operator()(const yk::ItemId &id) const noexcept {
        return std::hash<std::string>{}(id.str());
    }
};
