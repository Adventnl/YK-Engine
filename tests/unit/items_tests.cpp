// Items, inventories, containers and pickups: the definition files, stacks, slots and equipment,
// durability and tools, using items, permission tokens, containers with locks and hidden slots,
// items on the ground, the spatial index they use, and the rules over all of it.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/items/Inventory.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/world/SpatialIndex.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace yk;

namespace {
Json J(const char *text) {
    auto parsed = Json::parse(text);
    CHECK(parsed);
    return parsed ? parsed.value() : Json();
}
bool has(const std::string &text, const char *part) {
    return text.find(part) != std::string::npos;
}

const char *characterData = R"({
  "format": "yk.data", "version": 1,
  "stats": [
    {"id": "health", "max": 100, "start": "max"},
    {"id": "stamina", "max": 50, "start": "max"},
    {"id": "strength", "max": 100, "start": 10}
  ],
  "effects": [
    {"id": "warm", "flags": ["warm"]},
    {"id": "knocked_out", "flags": ["no_move", "no_act"]}
  ],
  "items": [
    {"id": "coin", "displayName": "Coin", "stackSize": 99, "value": 1, "tags": ["currency"]},
    {"id": "screwdriver", "displayName": "Screwdriver", "icon": "assets/items/screwdriver.png",
     "tags": ["tool", "contraband"], "durability": 3, "toolActions": {"unscrew": 15},
     "weapon": {"damage": 8, "range": 1.1, "staminaCost": 6}},
    {"id": "file", "tags": ["tool"], "durability": 10, "toolActions": {"cut": 4, "unscrew": 5}},
    {"id": "sandwich", "stackSize": 5,
     "use": {"actions": [{"type": "Heal", "amount": 10}], "consume": true, "cooldown": 2}},
    {"id": "brick", "stackSize": 4, "tags": ["heavy"]},
    {"id": "guard_outfit", "tags": ["outfit", "contraband"],
     "equip": {"slot": "Outfit", "modifiers": [{"stat": "strength", "add": 3}],
               "flags": ["disguise.guard"], "grants": ["access:guard"],
               "factors": {"perception.visibility": 0.5}, "effects": ["warm"],
               "appearance": {"outfit": "assets/char/guard.png"}}},
    {"id": "inmate_outfit", "tags": ["outfit"],
     "equip": {"slot": "Outfit", "modifiers": [{"stat": "strength", "add": 1}]}},
    {"id": "bat", "durability": 5, "equip": {"slot": "Weapon"}, "weapon": {"damage": 10}},
    {"id": "cell_key", "tags": ["key", "contraband"], "grants": ["key:cell_3"]},
    {"id": "badge", "equip": {"slot": "Badge"}},
    {"id": "gizmo", "use": {"actions": [{"type": "SetVariable", "name": "gizmo_used", "value": true}],
                             "requires": {"var": "powered"}}}
  ]
})";

struct Rig {
    ComponentRegistry registry;
    MemoryAssets assets;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<GameRuntime> runtime;
    EntityId hero, other, chest, desk;
    std::vector<GameEvent> heard;

    Rig() {
        registerEngineComponents(registry);
        assets.files["data/character.ykdata"] = characterData;
        scene = std::make_unique<Scene>(registry, 5);
    }
    // A character with everything an inventory needs.
    Entity &character(const char *name, int slots = 6) {
        Entity &entity = scene->createEntity(name);
        entity.add<StatSet>();
        entity.add<StatusEffects>();
        entity.add<Health>();
        auto &inventory = entity.add<Inventory>();
        inventory.slots = slots;
        inventory.equipmentSlots = {"Outfit", "Weapon"};
        return entity;
    }
    void start() {
        RuntimeOptions options;
        options.assets = &assets;
        auto created = GameRuntime::create(std::move(scene), options);
        CHECK(created);
        if (!created)
            return;
        runtime = std::move(created.value());
        runtime->events().subscribe("*",
                                    [this](const GameEvent &event) { heard.push_back(event); });
        runtime->stepOnce(Keyboard{});
    }
    void step(int ticks = 1) {
        for (int i = 0; i < ticks; ++i)
            runtime->stepOnce(Keyboard{});
    }
    Entity &entity(EntityId id) {
        return *runtime->scene().find(id);
    }
    Inventory &inventory(EntityId id) {
        return *entity(id).get<Inventory>();
    }
    GameContext &game() {
        return *runtime;
    }
    int count(const char *name) const {
        return static_cast<int>(
            std::count_if(heard.begin(), heard.end(),
                          [&](const GameEvent &event) { return event.name == name; }));
    }
    const GameEvent *last(const char *name) const {
        for (auto it = heard.rbegin(); it != heard.rend(); ++it)
            if (it->name == name)
                return &*it;
        return nullptr;
    }
    ItemStack stack(const char *id, int count = 1) {
        return gameData(game()).items.make(id, count);
    }
};

// ---- Definitions
// ---------------------------------------------------------------------------------
void definitions() {
    std::vector<std::string> warnings;
    auto item = ItemDefinition::fromJson(
        J(R"({"id":"screwdriver","displayName":"Screwdriver","description":"Flat.","category":"tools",
              "icon":"assets/items/screwdriver.png","worldPrefab":"prefabs/items/screwdriver.ykprefab",
              "tags":["tool","contraband"],"stackSize":1,"durability":100,"value":12,"weight":0.4,
              "toolActions":{"unscrew":15},"grants":["x"],
              "weapon":{"damage":8,"range":1.1,"arc":60,"staminaCost":6,"cooldown":0.4,"knockback":2,
                        "damageType":"pierce","chargeSeconds":1,"chargeMultiplier":2,"durabilityCost":2},
              "equip":{"slot":"Tool","modifiers":[{"stat":"strength","add":1}],"flags":["armed"],
                       "factors":{"noise":1.5},"grants":["y"],"effects":["warm"],
                       "appearance":{"held":"assets/held.png"}},
              "use":{"actions":[{"type":"Log","message":"scratch"}],"requires":{"var":"ok"},
                     "consume":false,"cooldown":3,"durabilityCost":1},
              "data":{"story":"found in the yard"}})"),
        warnings);
    CHECK(item && warnings.empty());
    if (item) {
        const ItemDefinition &def = item.value();
        CHECK(def.id == "screwdriver" && def.displayName == "Screwdriver" &&
              def.hasTag("contraband") && !def.hasTag("weapon") && def.durability == 100 &&
              def.toolEfficiency("unscrew") == 15.0 && def.toolEfficiency("dig") == 0.0 &&
              def.weapon && def.weapon->arc == 60 && def.weapon->damageType == "pierce" &&
              def.weapon->durabilityCost == 2 && def.equip && def.equip->slot == "Tool" &&
              def.equip->factors.at("noise") == 1.5 && def.use && def.use->cooldown == 3 &&
              !def.use->condition.empty() &&
              def.data.get("story").asString() == "found in the yard");
        CHECK(def.itemId() == ItemId("screwdriver"));
        auto again = ItemDefinition::fromJson(def.toJson(), warnings);
        CHECK(again && again.value().toJson() == def.toJson());
    }
    const auto error = [&](const char *text) {
        auto parsed = ItemDefinition::fromJson(J(text), warnings);
        return parsed ? std::string() : parsed.error();
    };
    CHECK(has(error("[]"), "must be an object"));
    CHECK(has(error("{}"), "'id' is needed"));
    CHECK(has(error(R"({"id":"a b"})"), "not a usable id"));
    CHECK(has(error(R"({"id":"x","stackSize":0})"), "'stackSize'"));
    CHECK(has(error(R"({"id":"x","stackSize":5,"durability":10})"), "stackSize must be 1"));
    CHECK(has(error(R"({"id":"x","toolActions":{"dig":0}})"), "efficiency above 0"));
    CHECK(has(error(R"({"id":"x","toolActions":[1]})"), "'toolActions' must be an object"));
    CHECK(has(error(R"({"id":"x","weapon":{"range":0}})"), "weapon: 'range'"));
    CHECK(has(error(R"({"id":"x","weapon":{"arc":400}})"), "weapon: 'arc'"));
    CHECK(has(error(R"({"id":"x","equip":{}})"), "'slot' is needed"));
    CHECK(
        has(error(R"({"id":"x","equip":{"slot":"S","modifiers":[{"stat":"a"}]}})"), "modifier 1"));
    CHECK(has(error(R"({"id":"x","equip":{"slot":"S","factors":{"a":-1}}})"), "factor 'a'"));
    CHECK(has(error(R"({"id":"x","use":{}})"), "does nothing"));
    CHECK(has(error(R"({"id":"x","use":{"actions":[{"nope":1}]}})"), "use: "));
    CHECK(has(error(R"({"id":"x","use":{"consume":true,"requires":{"all":3}}})"), "requires"));
    warnings.clear();
    CHECK(ItemDefinition::fromJson(J(R"({"id":"x","durabilty":5,"weapon":{"dmg":2}})"), warnings));
    CHECK(warnings.size() == 2 && has(warnings[0], "'durabilty'") && has(warnings[1], "'dmg'"));

    // Stacks.
    auto stack = ItemStack::fromJson(J(
        R"({"item":"bat","count":1,"durability":4,"owner":"npc.warden","meta":{"note":"mine"}})"));
    CHECK(stack && stack.value().item.str() == "bat" && stack.value().durability == 4 &&
          stack.value().owner == "npc.warden");
    CHECK(ItemStack::fromJson(stack.value().toJson()).value() == stack.value());
    CHECK(has(ItemStack::fromJson(J("{}")).error(), "'item' is needed"));
    CHECK(has(ItemStack::fromJson(J(R"({"item":"x","count":0})")).error(), "'count'"));
    ItemStack a, b;
    a.item = b.item = ItemId("coin");
    a.count = 3;
    b.count = 90;
    CHECK(a.matches(b) && !a.empty() && ItemStack{}.empty());
    b.owner = "thief";
    CHECK(!a.matches(b));
    b.owner.clear();
    b.durability = 2;
    CHECK(!a.matches(b));
    CHECK(ItemId("a") < ItemId("b") && ItemId() == ItemId("") && ItemId("q").str() == "q");
}

void gameDataItems() {
    MemoryAssets assets;
    assets.files["data/character.ykdata"] = characterData;
    // A single item can be a file of its own, and a project may spread items over many files.
    assets.files["items/lockpick.ykitem"] =
        R"({"id":"lockpick","tags":["tool"],"toolActions":{"pick":8},"durability":6})";
    assets.files["items/more.ykitem"] =
        R"({"items":[{"id":"soap"},{"id":"coin"},{"id":"vest","equip":{"slot":"Outfit","modifiers":[{"stat":"charm","add":1}],"effects":["ghostly"]}},
                                                      {"id":"cape","equip":{"slot":"Outfit","flags":["flies"]},"icon":"assets/cape.png","worldPrefab":"prefabs/cape.ykprefab"},
                                                      {"id":"zapper","weapon":{"damage":2,"staminaCost":3},"use":{"actions":[{"type":"GiveItem","item":"unicorn"}]}}]})";
    std::vector<DataProblem> problems;
    const GameData data = GameData::load(assets, problems);
    CHECK(data.items.items.contains("lockpick") && data.items.items.contains("soap") &&
          data.items.items.contains("screwdriver"));
    const auto problem = [&](const char *file, const char *part, bool error = true) {
        return std::any_of(problems.begin(), problems.end(), [&](const DataProblem &item) {
            return item.file == file && has(item.message, part) && item.error == error;
        });
    };
    CHECK(problem("items/more.ykitem", "two definitions called 'coin'"));
    // Equipment items have an effect of their own that the engine applies when worn.
    CHECK(data.stats.effects.contains("equip:guard_outfit") &&
          data.stats.effects.contains("equip:bat") && !data.stats.effects.contains("equip:coin"));
    const EffectDefinition *effect = data.stats.effects.find("equip:guard_outfit");
    CHECK(effect && effect->flags == std::vector<std::string>{"disguise.guard"} &&
          effect->grants.size() == 1 && effect->factors.at("perception.visibility") == 0.5 &&
          effect->modifiers.size() == 1 && effect->hidden && effect->duration == 0.0 &&
          equipEffectId("bat") == "equip:bat");
    CHECK(data.known("item", "soap") && !data.known("item", "unicorn") &&
          data.known("effect", "equip:vest"));
    // Make: a full-durability stack, or nothing for what does not exist.
    const ItemStack screwdriver = data.items.make("screwdriver");
    CHECK(screwdriver.count == 1 && screwdriver.durability == 3 &&
          data.items.make("coin", 7).durability == -1 && data.items.make("nothing").empty() &&
          data.items.make("coin", 0).empty());
    CHECK(data.items.withTag("tool").size() == 3 && data.items.withTag("contraband").size() == 3 &&
          data.items.withTag("zzz").empty());

    // What does not hang together is reported.
    ComponentRegistry registry;
    registerEngineComponents(registry);
    problems.clear();
    data.check(registry.extension<RuleCatalog>(), problems);
    CHECK(problem("items/more.ykitem", "item 'vest': equipped, it changes the stat 'charm'"));
    CHECK(problem("items/more.ykitem", "item 'vest': equipped, it applies the effect 'ghostly'"));
    CHECK(problem("items/more.ykitem", "no item 'unicorn'"));
    CHECK(problem("items/more.ykitem", "item 'zapper' use"));
    CHECK(!problem("data/character.ykdata", "not defined"));
    // Files the items point at.
    std::vector<std::string> paths;
    data.visitAssets(
        [&](const std::string &, const std::string &label, const std::string &path,
            const std::string &kind) { paths.push_back(kind + ":" + path + " (" + label + ")"); });
    CHECK(std::find(paths.begin(), paths.end(),
                    "texture:assets/items/screwdriver.png (item 'screwdriver' icon)") !=
          paths.end());
    CHECK(std::find(paths.begin(), paths.end(),
                    "prefab:prefabs/cape.ykprefab (item 'cape' worldPrefab)") != paths.end());
    CHECK(std::find(paths.begin(), paths.end(),
                    "texture:assets/char/guard.png (item 'guard_outfit' appearance 'outfit')") !=
          paths.end());
    const auto rows = data.summary();
    CHECK(std::any_of(rows.begin(), rows.end(),
                      [](const auto &row) { return row.first == "Items" && row.second == "16"; }));
}

// ---- Inventory
// -------------------------------------------------------------------------------------
void slotsAndStacks() {
    Rig rig;
    rig.hero = rig.character("Hero", 4).id();
    rig.start();
    Inventory &inventory = rig.inventory(rig.hero);
    GameContext &game = rig.game();
    CHECK(inventory.size() == 4 && inventory.freeSlots() == 4 &&
          inventory.count(ItemId("coin")) == 0);
    // Stacks fill up to the stack size, then the next slot.
    CHECK(inventory.addItem(game, "coin", 60) == 0);
    CHECK(inventory.addItem(game, "coin", 60) == 0);
    CHECK(inventory.slot(0).count == 99 && inventory.slot(1).count == 21 &&
          inventory.freeSlots() == 2);
    CHECK(inventory.count(ItemId("coin")) == 120 && inventory.has(ItemId("coin"), 120) &&
          !inventory.has(ItemId("coin"), 121));
    CHECK(inventory.addItem(game, "brick", 6) == 0); // 4 and 2: bricks stack to 4.
    CHECK(inventory.slot(2).count == 4 && inventory.slot(3).count == 2 &&
          inventory.freeSlots() == 0);
    // No room: what does not fit comes back to the caller.
    CHECK(inventory.addItem(game, "coin", 100) == 22); // The 21 coin stack takes 78.
    rig.step();
    CHECK(rig.count("item.added") == 4 && rig.count("inventory.changed") == 4);
    const GameEvent *added = rig.last("item.added");
    CHECK(added && added->source == rig.hero && added->data.get("item").asString() == "coin" &&
          added->data.get("count").asNumber() == 78);
    CHECK(inventory.canAdd(game, rig.stack("coin", 10)) == 0 &&
          inventory.canAdd(game, rig.stack("brick", 5)) == 2 &&
          inventory.canAdd(game, rig.stack("brick", 1)) == 1);
    CHECK(inventory.addItem(game, "nothing", 1) == 1); // Not an item: nothing is added.
    CHECK(inventory.add(game, ItemStack{}) == 0);

    // Taking out, from the slots in order.
    CHECK(inventory.remove(game, ItemId("coin"), 100) == 100);
    CHECK(inventory.slot(0).empty() && inventory.slot(1).count == 98 &&
          inventory.count(ItemId("coin")) == 98);
    CHECK(inventory.remove(game, ItemId("coin"), 500) == 98 &&
          inventory.remove(game, ItemId("coin"), 1) == 0);
    rig.step();
    CHECK(rig.count("item.removed") == 2);
    inventory.clear(game);
    CHECK(inventory.freeSlots() == 4 && inventory.count(ItemId("brick")) == 0);

    // Moving: onto an empty slot or another item swaps, onto a matching stack merges.
    inventory.addItem(game, "brick", 3);
    inventory.addItem(game, "coin", 5);
    CHECK(inventory.slot(0).item.str() == "brick" && inventory.slot(1).item.str() == "coin");
    CHECK(inventory.move(game, 0, 1));
    CHECK(inventory.slot(0).item.str() == "coin" && inventory.slot(1).item.str() == "brick");
    CHECK(inventory.move(game, 1, 2) && inventory.slot(2).item.str() == "brick" &&
          inventory.slot(1).empty());
    CHECK(!inventory.move(game, 1, 2) && !inventory.move(game, 0, 0) &&
          !inventory.move(game, 0, 9) &&
          !inventory.move(game, -1, 0)); // Nothing to move, the same slot, no such slot.
    inventory.addItem(game, "brick", 1); // Joins the stack that has room.
    CHECK(inventory.slot(2).count == 4 && inventory.slot(1).empty());
    inventory.addItem(game, "brick", 2);
    CHECK(inventory.slot(1).count == 2);
    CHECK(inventory.move(game, 1, 2) && inventory.slot(2).count == 2 &&
          inventory.slot(1).count == 4); // Full: swapped.
    inventory.clear(game);

    // Splitting and merging back.
    inventory.addItem(game, "brick", 4);
    CHECK(inventory.split(game, 0, 1) && inventory.slot(0).count == 3 &&
          inventory.slot(1).count == 1);
    CHECK(inventory.move(game, 1, 0) && inventory.slot(0).count == 4 && inventory.slot(1).empty());
    CHECK(!inventory.split(game, 0, 4) && !inventory.split(game, 0, 0) &&
          !inventory.split(game, 3, 1) && !inventory.split(game, 9, 1));
    inventory.addItem(game, "coin", 99);
    inventory.addItem(game, "coin", 99);
    inventory.addItem(game, "coin", 99);
    CHECK(inventory.freeSlots() == 0 &&
          !inventory.split(game, 0, 1)); // No free slot to split into.
    inventory.clear(game);

    // Taking part of a stack, and putting stacks into slots.
    inventory.addItem(game, "brick", 4);
    const ItemStack part = inventory.take(game, 0, 3);
    CHECK(part.count == 3 && part.item.str() == "brick" && inventory.slot(0).count == 1);
    CHECK(inventory.take(game, 5, 1).empty() && inventory.take(game, -1, 1).empty() &&
          inventory.take(game, 1, 1).empty());
    ItemStack bricks = part;
    CHECK(inventory.put(game, 0, bricks) && inventory.slot(0).count == 4 &&
          bricks.empty()); // Merged.
    ItemStack coins = rig.stack("coin", 3);
    CHECK(!inventory.put(game, 0, coins) && coins.count == 3); // Something else is there.
    CHECK(inventory.put(game, 2, coins) && inventory.slot(2).count == 3 && coins.empty());
    ItemStack nothing;
    CHECK(!inventory.put(game, 1, nothing) && !inventory.put(game, 9, coins));
}

void stackBookkeeping() {
    Rig rig;
    rig.hero = rig.character("Hero", 4).id();
    rig.start();
    Inventory &inventory = rig.inventory(rig.hero);
    GameContext &game = rig.game();
    inventory.addItem(game, "coin", 98);
    ItemStack coin = rig.stack("coin", 5);
    // A matching stack with room takes what fits and the rest stays in the caller's hands.
    CHECK(inventory.put(game, 0, coin) && coin.count == 4 && inventory.slot(0).count == 99);
    ItemStack more = rig.stack("coin", 4);
    CHECK(inventory.put(game, 1, more) && more.empty() && inventory.slot(1).count == 4);
    // Owned items do not merge with unowned ones, and a stack that is worn does not merge either.
    ItemStack owned = rig.stack("coin", 2);
    owned.owner = "npc.guard";
    CHECK(inventory.canAdd(game, owned) == 2);
    CHECK(inventory.add(game, owned) == 0 && inventory.slot(2).owner == "npc.guard" &&
          inventory.slot(1).count == 4);
    ItemStack worn = rig.stack("screwdriver");
    worn.durability = 1;
    CHECK(inventory.add(game, worn) == 0 && inventory.add(game, rig.stack("screwdriver")) == 1);
    CHECK(inventory.bestDurability(ItemId("screwdriver")) == 1);
}

void equipment() {
    Rig rig;
    rig.hero = rig.character("Hero", 6).id();
    rig.start();
    Inventory &inventory = rig.inventory(rig.hero);
    GameContext &game = rig.game();
    auto &effects = *rig.entity(rig.hero).get<StatusEffects>();
    StatSet &stats = *rig.entity(rig.hero).get<StatSet>();
    inventory.addItem(game, "guard_outfit");
    inventory.addItem(game, "inmate_outfit");
    inventory.addItem(game, "bat");
    inventory.addItem(game, "coin", 5);
    CHECK(!inventory.equip(game, 3) && !inventory.equip(game, 9) && !inventory.equip(game, -1) &&
          !inventory.equip(game, 5));
    CHECK(inventory.equip(game, 0));
    // Worn: it leaves the slot and its effect is on the character.
    CHECK(inventory.slot(0).empty() && inventory.equipped("Outfit") &&
          inventory.equipped("Outfit")->item.str() == "guard_outfit");
    CHECK(effects.has("equip:guard_outfit") && effects.has("warm") &&
          effects.hasFlag("disguise.guard") && effects.grants("access:guard") &&
          effects.factor("perception.visibility") == 0.5);
    CHECK_NEAR(stats.value("strength"), 13.0);
    CHECK(inventory.count(ItemId("guard_outfit")) == 1 && inventory.has(ItemId("guard_outfit")));
    CHECK(holdsToken(game, rig.entity(rig.hero), "access:guard") &&
          !holdsToken(game, rig.entity(rig.hero), "access:warden"));
    // A second outfit swaps with the first, which goes back to the slots.
    CHECK(inventory.equip(game, 1));
    CHECK(inventory.equipped("Outfit")->item.str() == "inmate_outfit" &&
          inventory.slot(1).item.str() == "guard_outfit");
    CHECK(!effects.has("equip:guard_outfit") && !effects.has("warm") &&
          effects.has("equip:inmate_outfit"));
    CHECK_NEAR(stats.value("strength"), 11.0);
    CHECK(!holdsToken(game, rig.entity(rig.hero), "access:guard"));
    // An item for a slot the character does not have cannot be worn.
    inventory.addItem(game, "badge");
    CHECK(inventory.slot(0).item.str() == "badge" && !inventory.equip(game, 0));
    // Taking off puts it back among the slots and takes its effect away.
    CHECK(inventory.unequip(game, "Outfit") && !inventory.equipped("Outfit") &&
          !effects.has("equip:inmate_outfit"));
    CHECK(inventory.count(ItemId("inmate_outfit")) == 1);
    CHECK_NEAR(stats.value("strength"), 10.0);
    CHECK(!inventory.unequip(game, "Outfit") && !inventory.unequip(game, "Nowhere"));
    // With no room to put it down, it stays on.
    CHECK(inventory.equip(game, 1)); // The guard outfit again.
    while (inventory.freeSlots() > 0)
        inventory.addItem(game, "brick", 4);
    CHECK(inventory.freeSlots() == 0 && !inventory.unequip(game, "Outfit") &&
          inventory.equipped("Outfit"));
    CHECK(effects.has("equip:guard_outfit"));
    // Wearing something while the slots are full works when the swap frees the slot it came from.
    inventory.remove(game, ItemId("brick"), 4);
    CHECK(inventory.unequip(game, "Outfit") && inventory.freeSlots() == 0);
    rig.step();
    CHECK(rig.count("item.equipped") == 3 && rig.count("item.unequipped") == 3);
    CHECK(rig.last("item.unequipped")->data.get("slot").asString() == "Outfit" &&
          rig.last("item.unequipped")->data.get("item").asString() == "guard_outfit");
}

void equipmentRemoval() {
    Rig rig;
    rig.hero = rig.character("Hero", 4).id();
    rig.start();
    Inventory &inventory = rig.inventory(rig.hero);
    GameContext &game = rig.game();
    auto &effects = *rig.entity(rig.hero).get<StatusEffects>();
    inventory.addItem(game, "guard_outfit");
    inventory.equip(game, 0);
    CHECK(effects.has("equip:guard_outfit"));
    // Removing a worn item takes it off too.
    CHECK(inventory.remove(game, ItemId("guard_outfit"), 1) == 1);
    CHECK(!inventory.equipped("Outfit") && !effects.has("equip:guard_outfit") &&
          !effects.has("warm"));
    // A wielded item that breaks is gone and its effect with it.
    inventory.addItem(game, "bat");
    CHECK(inventory.equip(game, 0));
    Inventory::Location where;
    where.equipSlot = "Weapon";
    CHECK(!inventory.wear(game, where, 4) && inventory.equipped("Weapon")->durability == 1);
    CHECK(inventory.wear(game, where, 1) && !inventory.equipped("Weapon") &&
          !effects.has("equip:bat"));
    rig.step();
    CHECK(rig.count("item.broke") == 1 &&
          rig.last("item.broke")->data.get("item").asString() == "bat");
    CHECK(!inventory.wear(game, where, 1)); // Nothing left to wear.
}

void tools() {
    Rig rig;
    rig.hero = rig.character("Hero", 5).id();
    rig.start();
    Inventory &inventory = rig.inventory(rig.hero);
    GameContext &game = rig.game();
    CHECK(!inventory.findTool(game, "unscrew"));
    inventory.addItem(game, "file");
    inventory.addItem(game, "screwdriver");
    inventory.addItem(game, "coin", 3);
    // The best tool for the action wins; an action nobody can do has none.
    auto tool = inventory.findTool(game, "unscrew");
    CHECK(tool && tool->item.str() == "screwdriver" && tool->efficiency == 15.0 &&
          tool->where.slot == 1);
    tool = inventory.findTool(game, "cut");
    CHECK(tool && tool->item.str() == "file" && tool->efficiency == 4.0);
    CHECK(!inventory.findTool(game, "dig"));
    // Using it wears it; at zero it is gone and the next best takes over.
    for (int i = 0; i < 2; ++i)
        CHECK(!inventory.wear(game, inventory.findTool(game, "unscrew")->where, 1));
    CHECK(inventory.bestDurability(ItemId("screwdriver")) == 1);
    CHECK(inventory.wear(game, inventory.findTool(game, "unscrew")->where, 1));
    CHECK(inventory.count(ItemId("screwdriver")) == 0);
    tool = inventory.findTool(game, "unscrew");
    CHECK(tool && tool->item.str() == "file" && tool->efficiency == 5.0);
    CHECK(inventory.bestDurability(ItemId("file")) == 10 &&
          inventory.bestDurability(ItemId("coin")) == -1 &&
          inventory.bestDurability(ItemId("nothing")) == -1);
    // A tool that is already worn through (an old save) is not offered, and wearing it removes it.
    Inventory::Location where;
    where.slot = 0;
    inventory.stackAt(where)->durability = 0;
    CHECK(!inventory.findTool(game, "cut"));
    CHECK(inventory.wear(game, where, 1) && inventory.slot(0).empty());
    CHECK(!inventory.wear(game, Inventory::Location{}, 1) &&
          inventory.stackAt(Inventory::Location{}) == nullptr);
    CHECK(!inventory.wear(game, where, 1)); // Nothing is there to wear.
    where.slot = 2;
    CHECK(!inventory.wear(game, where, 1)); // Coins do not wear.
    rig.step();
    CHECK(rig.count("item.broke") == 2);
}

void usingItems() {
    Rig rig;
    rig.hero = rig.character("Hero", 5).id();
    rig.start();
    Inventory &inventory = rig.inventory(rig.hero);
    GameContext &game = rig.game();
    StatSet &stats = *rig.entity(rig.hero).get<StatSet>();
    stats.add(game, "health", -50);
    inventory.addItem(game, "sandwich", 3);
    CHECK(!inventory.use(game, 1) && !inventory.use(game, 9) && !inventory.use(game, -1));
    CHECK(inventory.use(game, 0)); // Heals 10 and one is used up.
    CHECK_NEAR(stats.value("health"), 60.0);
    CHECK(inventory.slot(0).count == 2);
    CHECK(!inventory.use(game, 0)); // It cools down for two seconds.
    rig.step(100);
    CHECK(!inventory.use(game, 0));
    rig.step(30);
    CHECK(inventory.use(game, 0) && inventory.slot(0).count == 1);
    CHECK_NEAR(stats.value("health"), 70.0);
    rig.step(130);
    CHECK(inventory.use(game, 0) && inventory.slot(0).empty());
    CHECK_NEAR(stats.value("health"), 80.0);
    rig.step();
    CHECK(rig.count("item.used") == 3 && rig.count("item.removed") == 3);
    // A requirement keeps it from working until it holds.
    inventory.addItem(game, "gizmo");
    CHECK(!inventory.use(game, 0) && !rig.runtime->blackboard().has("gizmo_used"));
    rig.runtime->blackboard().setBool("powered", true);
    CHECK(inventory.use(game, 0) && rig.runtime->blackboard().flag("gizmo_used") &&
          inventory.slot(0).count == 1);
    // Items with no use.
    inventory.addItem(game, "coin");
    CHECK(!inventory.use(game, 1));
}

void tokens() {
    Rig rig;
    rig.hero = rig.character("Hero", 4).id();
    rig.start();
    Inventory &inventory = rig.inventory(rig.hero);
    GameContext &game = rig.game();
    Entity &hero = rig.entity(rig.hero);
    CHECK(!holdsToken(game, hero, "key:cell_3"));
    inventory.addItem(game, "cell_key");
    CHECK(holdsToken(game, hero, "key:cell_3") && !holdsToken(game, hero, "key:cell_4"));
    inventory.remove(game, ItemId("cell_key"), 1);
    CHECK(!holdsToken(game, hero, "key:cell_3"));
    // A character without an inventory holds nothing.
    Entity &bare = rig.runtime->scene().createEntity("Bare");
    CHECK(!holdsToken(game, bare, "key:cell_3"));
}

void inventoryState() {
    Json saved;
    {
        Rig rig;
        rig.hero = rig.character("Hero", 5).id();
        rig.start();
        Inventory &inventory = rig.inventory(rig.hero);
        GameContext &game = rig.game();
        inventory.addItem(game, "coin", 120);
        inventory.addItem(game, "screwdriver");
        inventory.addItem(game, "guard_outfit");
        inventory.addItem(game, "sandwich", 2);
        CHECK(inventory.equip(game, 3));
        Inventory::Location where;
        where.slot = 2;
        inventory.wear(game, where, 2);
        CHECK(inventory.use(game, 4)); // A sandwich: one is left and it cools down.
        ItemStack owned = rig.stack("brick", 2);
        owned.owner = "npc.warden";
        owned.meta = J(R"({"note":"stolen"})");
        inventory.add(game, owned);
        saved = inventory.saveState();
    }
    Rig rig;
    rig.hero = rig.character("Hero", 5).id();
    rig.start();
    Inventory &inventory = rig.inventory(rig.hero);
    GameContext &game = rig.game();
    CHECK(inventory.loadState(game, saved));
    CHECK(inventory.count(ItemId("coin")) == 120 && inventory.slot(0).count == 99 &&
          inventory.slot(1).count == 21);
    CHECK(inventory.bestDurability(ItemId("screwdriver")) == 1 &&
          inventory.count(ItemId("sandwich")) == 1);
    CHECK(inventory.equipped("Outfit") &&
          inventory.equipped("Outfit")->item.str() == "guard_outfit");
    CHECK(inventory.count(ItemId("brick")) == 2);
    bool ownedFound = false;
    for (int i = 0; i < inventory.size(); ++i)
        if (inventory.slot(i).owner == "npc.warden")
            ownedFound = inventory.slot(i).meta.get("note").asString() == "stolen";
    CHECK(ownedFound);
    CHECK(!inventory.use(game, 4)); // The cooldown came back with it.
    // Items that no longer exist are dropped, and a slot outside the inventory is refused.
    Json old = saved;
    Json slotsList = Json::array();
    slotsList.push(J(R"({"item":"unicorn","count":1,"slot":0})"));
    old.set("slots", slotsList);
    setLogStderrEnabled(false);
    CHECK(inventory.loadState(game, old) && inventory.count(ItemId("unicorn")) == 0 &&
          inventory.freeSlots() == 5);
    CHECK(has(
        inventory.loadState(game, J(R"({"slots":[{"item":"coin","count":1,"slot":40}]})")).error(),
        "outside"));
    CHECK(has(inventory.loadState(game, J(R"({"slots":[{"count":1}]})")).error(),
              "inventory slot 1"));
}

void startItems() {
    Rig rig;
    Entity &hero = rig.character("Hero", 3);
    hero.get<Inventory>()->startItems =
        J(R"([{"item":"coin","count":10},{"item":"guard_outfit","equipped":true},
                                              {"item":"screwdriver"},{"item":"unicorn"},{"item":"coin","count":500}])");
    rig.hero = hero.id();
    Entity &broken = rig.character("Broken", 3);
    broken.get<Inventory>()->startItems = J(R"({"item":"coin"})");
    setLogStderrEnabled(false);
    rig.start();
    Inventory &inventory = rig.inventory(rig.hero);
    CHECK(inventory.count(ItemId("coin")) > 10 && inventory.equipped("Outfit") &&
          inventory.count(ItemId("screwdriver")) == 1);
    CHECK(inventory.count(ItemId("unicorn")) == 0 &&
          inventory.bestDurability(ItemId("screwdriver")) == 3);
    CHECK(rig.entity(rig.hero).get<StatusEffects>()->has("equip:guard_outfit"));
    // A start list that is not a list is reported by validation, and ignored at run time.
    GameData data;
    std::vector<DataProblem> problems;
    data.add(J(characterData), "c.ykdata", problems);
    data.finalize(problems);
    std::vector<std::string> found;
    CheckContext context;
    context.known = [&](std::string_view kind, std::string_view id) {
        return data.known(kind, id);
    };
    Inventory &check = *rig.entity(rig.hero).get<Inventory>();
    check.startItems =
        J(R"([{"item":"unicorn"},{"count":1},{"item":"coin","count":2},{"item":"coin","count":2},
                             {"item":"coin"},{"item":"coin"},{"item":"coin"}])");
    check.equipmentSlots = {"Outfit", "Outfit", ""};
    check.type().check(rig.entity(rig.hero), check, context, found);
    CHECK(found.size() == 5);
    CHECK(std::any_of(found.begin(), found.end(), [](const std::string &m) {
        return has(m, "'unicorn', which is not defined");
    }));
    CHECK(std::any_of(found.begin(), found.end(),
                      [](const std::string &m) { return has(m, "starting item 2"); }));
    CHECK(std::any_of(found.begin(), found.end(), [](const std::string &m) {
        return has(m, "two equipment slots are called 'Outfit'");
    }));
    CHECK(std::any_of(found.begin(), found.end(), [](const std::string &m) {
        return has(m, "more stacks than it has slots");
    }));
    found.clear();
    Inventory &worse = *rig.entity(rig.hero).get<Inventory>();
    worse.startItems = J("{}");
    worse.type().check(rig.entity(rig.hero), worse, context, found);
    CHECK(std::any_of(found.begin(), found.end(),
                      [](const std::string &m) { return has(m, "'startItems' must be a list"); }));
}

// ---- Containers
// -----------------------------------------------------------------------------------
void containers() {
    Rig rig;
    rig.hero = rig.character("Hero", 3).id();
    rig.other = rig.character("Visitor", 3).id();
    Entity &locker = rig.scene->createEntity("Locker");
    auto &inventory = locker.add<Inventory>();
    inventory.slots = 4;
    inventory.startItems = J(
        R"([{"item":"coin","count":5},{"item":"cell_key"},{"item":"brick","count":2},{"item":"file"}])");
    auto &container = locker.add<Container>();
    container.owner = "npc.warden";
    container.locked = true;
    container.unlockToken = "key:cell_3";
    container.hiddenSlots = 2;
    rig.chest = locker.id();
    rig.start();
    GameContext &game = rig.game();
    Container &box = *rig.entity(rig.chest).get<Container>();
    Entity &hero = rig.entity(rig.hero);
    std::string why;
    // Locked: needs the key.
    CHECK(!box.canOpen(game, hero, &why) && why == "It is locked." && !box.open(game, hero) &&
          !box.openBy(hero.id()));
    rig.inventory(rig.hero).addItem(game, "cell_key");
    CHECK(box.canOpen(game, hero, &why) && box.open(game, hero) && box.openBy(hero.id()));
    rig.step();
    CHECK(rig.count("container.opened") == 1 && rig.last("container.opened")->source == rig.chest &&
          rig.last("container.opened")->other == rig.hero);
    // Two slots are hidden until searched.
    CHECK(box.visibleSlots() == 2 && !box.searched());
    CHECK(box.take(game, hero, 3) == 0 && box.take(game, hero, 2) == 0); // Hidden.
    box.search(game, hero);
    CHECK(box.visibleSlots() == 4 && box.searched());
    // Taking: whole slots move to the character; the owner is on record.
    rig.heard.clear();
    CHECK(box.take(game, hero, 0) == 5 && rig.inventory(rig.chest).slot(0).empty() &&
          rig.inventory(rig.hero).count(ItemId("coin")) == 5);
    rig.step();
    const GameEvent *taken = rig.last("container.taken");
    CHECK(taken && taken->source == rig.chest && taken->other == rig.hero &&
          taken->data.get("item").asString() == "coin" &&
          taken->data.get("count").asNumber() == 5 &&
          taken->data.get("owner").asString() == "npc.warden");
    CHECK(box.take(game, hero, 0) == 0); // Already empty.
    // The visitor has not opened it: nothing moves for them.
    CHECK(box.take(game, rig.entity(rig.other), 3) == 0);
    // Not enough room: what does not fit stays in the container.
    rig.inventory(rig.hero).addItem(game, "brick", 4);
    rig.inventory(rig.hero).addItem(game, "brick", 4);
    CHECK(rig.inventory(rig.hero).freeSlots() == 0);
    CHECK(box.take(game, hero, 2) == 0 && rig.inventory(rig.chest).slot(2).count == 2);
    // Putting things in.
    rig.inventory(rig.hero).clear(game);
    rig.inventory(rig.hero).addItem(game, "coin", 30);
    CHECK(box.put(game, hero, 0) == 30 && rig.inventory(rig.chest).slot(0).count == 30);
    CHECK(box.put(game, hero, 0) == 0);
    rig.step();
    CHECK(rig.count("container.stored") == 1);
    box.close(game, hero);
    CHECK(!box.openBy(hero.id()) && box.put(game, hero, 0) == 0);
    rig.step();
    CHECK(rig.count("container.closed") == 1);
    box.close(game, hero); // Already closed: nothing.
    // Locking and unlocking, by code or by rule.
    box.setLocked(game, false);
    CHECK(box.canOpen(game, rig.entity(rig.other)));
    box.setLocked(game, true);
    rig.step();
    CHECK(rig.count("container.unlocked") == 1 && rig.count("container.locked") == 1);

    // Saved: locked, searched.
    const Json saved = box.saveState();
    box.setLocked(game, false);
    CHECK(box.loadState(game, saved) && box.isLocked());
}

void openPermissions() {
    Rig rig;
    rig.hero = rig.character("Hero", 3).id();
    Entity &desk = rig.scene->createEntity("Desk");
    desk.add<Inventory>().slots = 2;
    auto &container = desk.add<Container>();
    container.openRequires =
        J(R"({"all":[{"type":"HasItem","item":"badge"},{"type":"HasTag","tag":"hero"}]})");
    rig.desk = desk.id();
    rig.scene->find(rig.hero)->addTag("hero");
    rig.start();
    GameContext &game = rig.game();
    Container &box = *rig.entity(rig.desk).get<Container>();
    Entity &hero = rig.entity(rig.hero);
    std::string why;
    CHECK(!box.canOpen(game, hero, &why) && has(why, "not allowed"));
    rig.inventory(rig.hero).addItem(game, "badge");
    CHECK(box.canOpen(game, hero) && box.open(game, hero));
    box.openRequires = J(R"({"all":3})");
    CHECK(!box.canOpen(game, hero, &why) && has(why, "cannot be opened"));
    // The component checks its own conditions.
    std::vector<std::string> problems;
    CheckContext context;
    context.known = [](std::string_view, std::string_view id) { return id != "nothing"; };
    box.type().check(rig.entity(rig.desk), box, context, problems);
    CHECK(problems.size() == 1 && has(problems[0], "'openRequires' is not valid"));
    problems.clear();
    box.openRequires = J(R"({"type":"HasItem","item":"nothing"})");
    box.type().check(rig.entity(rig.desk), box, context, problems);
    CHECK(problems.size() == 1 && has(problems[0], "no item 'nothing'"));
    problems.clear();
    box.openRequires = Json();
    box.locked = true;
    box.hiddenSlots = 5;
    box.type().check(rig.entity(rig.desk), box, context, problems);
    CHECK(problems.size() == 2);
}

// ---- Pickups
// -----------------------------------------------------------------------------------------
void pickups() {
    Rig rig;
    rig.scene->settings.levels.levels = {{"ground", "", LevelKind::Floor, 0.0F},
                                         {"upper", "", LevelKind::Floor, 3.0F}};
    Entity &hero = rig.character("Hero", 4);
    hero.get<Inventory>()->collectRadius = 1.0F;
    hero.setWorldPosition({0.0F, 0.0F});
    rig.hero = hero.id();
    const auto pickup = [&](const char *item, int count, Vec2 at, const char *level) -> Entity & {
        Entity &entity = rig.scene->createEntity(std::string("Pickup ") + item);
        entity.setWorldPosition(at);
        auto &p = entity.add<Pickup>();
        p.item = item;
        p.count = count;
        entity.add<WorldLayer>().level = level;
        return entity;
    };
    const EntityId near = pickup("coin", 12, {0.5F, 0.0F}, "ground").id();
    const EntityId far = pickup("coin", 3, {5.0F, 0.0F}, "ground").id();
    const EntityId above = pickup("brick", 2, {0.3F, 0.0F}, "upper").id();
    Entity &manual = pickup("file", 1, {0.2F, 0.2F}, "ground");
    manual.get<Pickup>()->autoCollect = false;
    const EntityId manualId = manual.id();
    Entity &bad = pickup("unicorn", 1, {9.0F, 0.0F}, "ground");
    const EntityId badId = bad.id();
    Entity &fading = pickup("sandwich", 1, {7.0F, 7.0F}, "ground");
    fading.get<Pickup>()->lifetime = 1.0F;
    const EntityId fadingId = fading.id();
    setLogStderrEnabled(false);
    rig.start();
    GameContext &game = rig.game();
    Inventory &inventory = rig.inventory(rig.hero);
    auto *spatial = rig.runtime->services().find<SpatialIndexService>();
    CHECK(spatial && spatial->count("pickup") == 5 &&
          spatial->count("nothing") == 0); // One was taken at once.
    rig.step();
    // Close ones on the same level are taken; others stay.
    CHECK(inventory.count(ItemId("coin")) == 12 && inventory.count(ItemId("brick")) == 0 &&
          inventory.count(ItemId("file")) == 0);
    CHECK(!rig.runtime->scene().find(near) && rig.runtime->scene().find(far) &&
          rig.runtime->scene().find(above) && rig.runtime->scene().find(manualId) &&
          rig.runtime->scene().find(badId));
    rig.step();
    CHECK(rig.count("item.picked_up") == 1 && rig.last("item.picked_up")->source == rig.hero &&
          rig.last("item.picked_up")->other == near);
    // An interaction can collect the manual one; across levels it cannot.
    CHECK(rig.entity(manualId).get<Pickup>()->collect(game, rig.entity(rig.hero)));
    rig.step();
    CHECK(inventory.count(ItemId("file")) == 1 && !rig.runtime->scene().find(manualId));
    CHECK(!rig.entity(above).get<Pickup>()->collect(game, rig.entity(rig.hero)) &&
          inventory.count(ItemId("brick")) == 0);
    // Walking to the far one picks it up.
    game.teleport(rig.entity(rig.hero), {5.0F, 0.0F});
    rig.step(2);
    CHECK(inventory.count(ItemId("coin")) == 15 && !rig.runtime->scene().find(far));
    // A pickup of an item that does not exist does nothing; one with a lifetime fades.
    CHECK(rig.entity(badId).get<Pickup>()->stack().empty());
    rig.step(70);
    CHECK(!rig.runtime->scene().find(fadingId));
    // A full inventory takes what fits and leaves the rest where it lies.
    inventory.clear(game);
    for (int i = 0; i < 4; ++i)
        inventory.addItem(game, "brick", 4);
    const EntityId lying =
        Pickup::place(game, rig.stack("coin", 5), {5.0F, 5.0F}, &rig.entity(rig.hero));
    Pickup *coins = rig.runtime->scene().find(lying)->get<Pickup>();
    CHECK(coins && !coins->collect(game, rig.entity(rig.hero)) && coins->stack().count == 5);
    inventory.remove(game, ItemId("brick"), 4);
    CHECK(coins->collect(game, rig.entity(rig.hero)) && inventory.count(ItemId("coin")) == 5);
    rig.step();
    CHECK(!rig.runtime->scene().find(lying));
    // Dropping: the stack lands as a pickup at the spot, merging with a matching one.
    inventory.clear(game);
    inventory.addItem(game, "coin", 30);
    game.teleport(rig.entity(rig.hero), {20.0F, 20.0F});
    rig.entity(rig.hero).get<Inventory>()->collectRadius = 0.0F;
    CHECK(inventory.drop(game, 0, 10, {20.5F, 20.0F}) && inventory.count(ItemId("coin")) == 20);
    rig.step();
    CHECK(rig.count("item.dropped") == 1);
    CHECK(spatial->near("pickup", {20.5F, 20.0F}, 0.1F).size() == 1);
    CHECK(inventory.drop(game, 0, 5, {20.6F, 20.0F}));
    rig.step();
    const auto here = spatial->near("pickup", {20.5F, 20.0F}, 0.5F);
    CHECK(here.size() == 1);
    Pickup *dropped = rig.runtime->scene().find(here.front().id)->get<Pickup>();
    CHECK(dropped && dropped->stack().count == 15 && dropped->stack().item.str() == "coin");
    // Far from it: a pickup of its own, on the dropper's level.
    CHECK(inventory.drop(game, 0, 5, {25.0F, 20.0F}));
    rig.step();
    CHECK(spatial->count("pickup") >= 3);
    CHECK(!inventory.drop(game, 7, 1, {0.0F, 0.0F}) && !inventory.drop(game, 0, 0, {0.0F, 0.0F}));
    // The stack on the ground is saved with its age.
    const Json saved = dropped->saveState();
    CHECK(saved.get("stack").get("count").asNumber() == 15);
    CHECK(dropped->loadState(game, saved) && dropped->stack().count == 15);
    CHECK(has(dropped->loadState(game, J("{}")).error(), "pickup"));
    // The component validates what it names.
    std::vector<std::string> problems;
    CheckContext context;
    context.known = [](std::string_view, std::string_view id) { return id != "unicorn"; };
    rig.entity(badId).get<Pickup>()->type().check(
        rig.entity(badId), *rig.entity(badId).get<Pickup>(), context, problems);
    CHECK(problems.size() == 1 && has(problems[0], "'unicorn', which is not defined"));
}

void spatialIndex() {
    Rig rig;
    for (int i = 0; i < 20; ++i) {
        Entity &entity = rig.scene->createEntity("P" + std::to_string(i));
        entity.setWorldPosition({static_cast<float>(i) * 2.0F, 0.0F});
        entity.add<Pickup>().item = "coin";
    }
    rig.start();
    auto &spatial = rig.runtime->services().get<SpatialIndexService>();
    CHECK(spatial.count("pickup") == 20);
    CHECK(spatial.near("pickup", {0, 0}, 2.5F).size() == 2);
    const EntityId nearest = spatial.nearest("pickup", {7.1F, 0.0F}, 10.0F, SpatialHash::anyLevel);
    CHECK(nearest && rig.runtime->scene().find(nearest)->worldPosition().x == 8.0F);
    CHECK(!spatial.nearest("pickup", {100, 100}, 5.0F, SpatialHash::anyLevel) &&
          spatial.near("none", {0, 0}, 50.0F).empty());
    CHECK(spatial.hash("pickup") && !spatial.hash("none"));
    // Moving things are followed; ones that are not stay until refreshed.
    Entity &mover = rig.runtime->scene().createEntity("Mover");
    mover.setWorldPosition({100.0F, 100.0F});
    spatial.track(mover, "actor", 0.5F, true);
    spatial.track(rig.runtime->scene().createEntity("Fixed"), "actor", 0.0F, false);
    CHECK(spatial.count("actor") == 2);
    mover.setWorldPosition({150.0F, 100.0F});
    rig.step();
    CHECK(spatial.near("actor", {150.0F, 100.0F}, 1.0F).size() == 1 &&
          spatial.near("actor", {100.0F, 100.0F}, 1.0F).empty());
    spatial.untrack(mover.id(), "actor");
    CHECK(spatial.count("actor") == 1);
    spatial.untrack(mover.id(), "never");
    std::vector<std::pair<std::string, std::string>> rows;
    spatial.describe(rows);
    CHECK(rows.size() == 2);
    // An entity that is destroyed drops out by itself.
    const EntityId doomed = rig.runtime->scene().createEntity("Doomed").id();
    spatial.track(*rig.runtime->scene().find(doomed), "actor", 0.0F, true);
    rig.runtime->destroyLater(doomed);
    rig.step(2);
    CHECK(spatial.count("actor") == 1);
}

// ---- Rules
// -------------------------------------------------------------------------------------------
void rules() {
    Rig rig;
    Entity &hero = rig.character("Hero", 4);
    rig.hero = hero.id();
    Entity &console = rig.scene->createEntity("Console");
    rig.other = console.id();
    Entity &locker = rig.scene->createEntity("Locker");
    locker.add<Inventory>().slots = 2;
    locker.add<Container>();
    rig.chest = locker.id();
    CHECK(console.add<RuleSet>().setRulesJson(J(R"([
      {"id":"repair","when":"repair","if":{"all":[{"type":"HasItem","item":"screwdriver"},
                                                   {"type":"HasItem","item":"coin","count":3}]},
       "then":[{"type":"RemoveItem","item":"coin","count":3},{"type":"WearItem","item":"screwdriver"},
               {"type":"SetVariable","name":"repaired","value":true}],
       "else":{"type":"SetVariable","name":"repaired","value":false}},
      {"id":"loot","when":"loot","then":[{"type":"GiveItem","item":"coin","count":7},{"type":"GiveItem","item":"screwdriver"}]},
      {"id":"flood","when":"flood","then":{"type":"GiveItem","item":"brick","count":30}},
      {"id":"facts","when":"facts","then":[
         {"type":"SetVariable","name":"coins","value":"$actor.inventory.count.coin"},
         {"type":"SetVariable","name":"tools","value":"$actor.inventory.tag.tool"},
         {"type":"SetVariable","name":"free","value":"$actor.inventory.free"},
         {"type":"SetVariable","name":"outfit","value":"$actor.inventory.equipped.Outfit"},
         {"type":"SetVariable","name":"wear","value":"$actor.inventory.durability.screwdriver"},
         {"type":"SetVariable","name":"keyed","value":"$actor.inventory.token.key:cell_3"},
         {"type":"SetVariable","name":"hasfile","value":"$actor.inventory.has.file"}]},
      {"id":"dress","when":"dress","then":[{"type":"GiveItem","item":"guard_outfit"},{"type":"EquipItem","item":"guard_outfit"}]},
      {"id":"undress","when":"undress","then":{"type":"UnequipItem","slot":"Outfit"}},
      {"id":"eat","when":"eat","then":[{"type":"GiveItem","item":"sandwich"},{"type":"UseItem","item":"sandwich"}]},
      {"id":"drop","when":"drop","then":{"type":"DropItem","item":"coin","count":2}},
      {"id":"take","when":"take","then":{"type":"PickUpItem"}},
      {"id":"lock","when":"lock","then":{"type":"LockContainer","entity":"name:Locker"}},
      {"id":"unlock","when":"unlock","then":{"type":"UnlockContainer","entity":"name:Locker"}},
      {"id":"cond","when":"cond","if":{"all":[{"type":"HasItemTag","tag":"tool","count":2},{"type":"HoldsToken","token":"key:cell_3"},
                                                 {"type":"ItemEquipped","slot":"Outfit"},{"type":"HasRoomFor","item":"coin","count":50}]},
       "then":{"type":"SetVariable","name":"cond","value":true},"else":{"type":"SetVariable","name":"cond","value":false}}])")));
    rig.start();
    GameContext &game = rig.game();
    Blackboard &board = rig.runtime->blackboard();
    Inventory &inventory = rig.inventory(rig.hero);
    const auto fire = [&](const char *name) {
        rig.runtime->events().emit(GameEvent(name, rig.other, rig.hero));
        rig.step();
    };
    const auto lyingNearHero = [&]() {
        rig.step(); // Things made this tick start being tracked on the next.
        const auto *spatial = rig.runtime->services().find<SpatialIndexService>();
        return spatial ? spatial->near("pickup", rig.entity(rig.hero).worldPosition(), 1.0F).size()
                       : 0U;
    };
    fire("repair");
    CHECK(board.has("repaired") && !board.flag("repaired"));
    fire("loot");
    CHECK(inventory.count(ItemId("coin")) == 7 && inventory.count(ItemId("screwdriver")) == 1);
    fire("repair");
    CHECK(board.flag("repaired") && inventory.count(ItemId("coin")) == 4 &&
          inventory.bestDurability(ItemId("screwdriver")) == 2);
    fire("facts");
    CHECK(board.integer("coins") == 4 && board.integer("tools") == 1 &&
          board.integer("free") == 2 && board.text("outfit").empty() &&
          board.integer("wear") == 2 && !board.flag("keyed") && !board.flag("hasfile"));
    // Too much to carry lands on the floor beside the character.
    fire("flood");
    CHECK(inventory.freeSlots() == 0 && inventory.count(ItemId("brick")) == 8);
    CHECK(lyingNearHero() == 1);
    inventory.clear(game);
    fire("dress");
    CHECK(inventory.equipped("Outfit") &&
          rig.entity(rig.hero).get<StatusEffects>()->hasFlag("disguise.guard"));
    fire("facts");
    CHECK(board.text("outfit") == "guard_outfit");
    inventory.addItem(game, "cell_key");
    inventory.addItem(game, "screwdriver");
    inventory.addItem(game, "file");
    fire("cond");
    CHECK(board.flag("cond")); // Two tools, the key, the outfit on, and a slot free for coins.
    inventory.addItem(game, "brick");
    fire("cond");
    CHECK(!board.flag("cond")); // No room for 50 coins any more.
    inventory.remove(game, ItemId("brick"), 1);
    inventory.remove(game, ItemId("file"), 1);
    fire("undress");
    CHECK(!inventory.equipped("Outfit") && inventory.count(ItemId("guard_outfit")) == 1);
    // Using, dropping, locking.
    rig.entity(rig.hero).get<StatSet>()->set(game, "health", 50);
    fire("eat");
    CHECK_NEAR(rig.entity(rig.hero).get<StatSet>()->value("health"), 60.0);
    inventory.addItem(game, "coin", 5);
    const auto before = lyingNearHero();
    fire("drop");
    CHECK(inventory.count(ItemId("coin")) == 3 && lyingNearHero() == before + 1);
    fire("lock");
    CHECK(rig.entity(rig.chest).get<Container>()->isLocked());
    fire("unlock");
    CHECK(!rig.entity(rig.chest).get<Container>()->isLocked());
    // Picking up what the rule's target is. More coins put down where the dropped ones lie join
    // them.
    const EntityId lying = Pickup::place(
        game, rig.stack("coin", 4), rig.entity(rig.hero).worldPosition(), &rig.entity(rig.hero));
    rig.runtime->events().emit(GameEvent("take", lying, rig.hero));
    rig.step();
    CHECK(inventory.count(ItemId("coin")) == 3 + 2 + 4);
    // Actions that cannot do their job fail and say so, and do not stop the rest of a list.
    RuleContext ctx(game);
    ctx.actor = rig.hero;
    ctx.origin = "test";
    const auto run = [&](const char *text) {
        return execute(Action::fromJson(J(text)).value(), ctx);
    };
    CHECK(run(R"({"type":"PickUpItem"})") == ActionResult::Failed);
    CHECK(run(R"({"type":"RemoveItem","item":"brick","count":99})") == ActionResult::Failed);
    CHECK(run(R"({"type":"GiveItem","item":"unicorn"})") == ActionResult::Failed);
    CHECK(run(R"({"type":"EquipItem","item":"coin"})") == ActionResult::Failed);
    CHECK(run(R"({"type":"UnequipItem","slot":"Outfit"})") == ActionResult::Failed);
    CHECK(run(R"({"type":"WearItem","item":"coin"})") == ActionResult::Failed);
    CHECK(run(R"({"type":"UseItem","item":"coin"})") == ActionResult::Failed);
    CHECK(run(R"({"type":"DropItem","item":"unicorn"})") == ActionResult::Failed);
    CHECK(run(R"({"type":"LockContainer","entity":"name:Hero"})") == ActionResult::Failed);
    ctx.actor = rig.other; // The console has no inventory.
    CHECK(run(R"({"type":"GiveItem","item":"coin"})") == ActionResult::Failed);

    // Validation names what is wrong.
    class Collect final : public RuleReport {
      public:
        std::vector<std::string> errors;
        const GameData *data{};
        void error(const std::string &message) override {
            errors.push_back(message);
        }
        void warning(const std::string &) override {}
        bool known(std::string_view kind, std::string_view id) const override {
            return data->known(kind, id);
        }
    } report;
    report.data = &gameData(game);
    auto bad = rulesFromJson(J(
        R"([{"when":"x","if":{"all":[{"type":"HasItem","item":"unicorn"},{"type":"ItemEquipped"}]},
        "then":[{"type":"GiveItem","item":"coin","count":0},{"type":"RemoveItem","item":"dragon"},
                {"type":"DropItem","item":"coin","count":-2}]}])"));
    CHECK(bad);
    check(*rig.registry.extension<RuleCatalog>(), bad.value().front(), report);
    CHECK(std::any_of(report.errors.begin(), report.errors.end(),
                      [](const std::string &m) { return has(m, "no item 'unicorn'"); }));
    CHECK(std::any_of(report.errors.begin(), report.errors.end(),
                      [](const std::string &m) { return has(m, "no item 'dragon'"); }));
    CHECK(std::any_of(report.errors.begin(), report.errors.end(),
                      [](const std::string &m) { return has(m, "needs an 'item' or a 'slot'"); }));
    CHECK(std::count_if(report.errors.begin(), report.errors.end(), [](const std::string &m) {
              return has(m, "'count' must be at least 1");
          }) == 2);
}
} // namespace

int main() {
    setLogStderrEnabled(false);
    definitions();
    gameDataItems();
    slotsAndStacks();
    stackBookkeeping();
    equipment();
    equipmentRemoval();
    tools();
    usingItems();
    tokens();
    inventoryState();
    startItems();
    containers();
    openPermissions();
    pickups();
    spatialIndex();
    rules();
    return yk::test::finish("items");
}
