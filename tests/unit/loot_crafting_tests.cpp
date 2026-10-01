// Loot tables and pools, and crafting: the definitions and their files, rolling with a seeded
// generator, containers that fill themselves, dealing must-exist items among a group, recipes with
// stations, stats and tools, and the rules over both.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/items/Crafting.hpp"
#include "yk/items/Inventory.hpp"
#include "yk/items/Loot.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/world/SpatialIndex.hpp"
#include <algorithm>
#include <map>
#include <set>
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
bool entity_hasInventory(const Entity &entity) {
    return entity.get<Inventory>() != nullptr;
}

const char *worldData = R"({
  "format": "yk.data", "version": 1,
  "stats": [{"id": "health", "max": 100}, {"id": "intellect", "name": "Intellect", "max": 100, "start": 20}],
  "items": [
    {"id": "soap", "stackSize": 10}, {"id": "file", "durability": 10, "tags": ["tool"]},
    {"id": "cell_key", "tags": ["key"]}, {"id": "cloth_a", "stackSize": 10, "tags": ["cloth"]},
    {"id": "cloth_b", "stackSize": 10, "tags": ["cloth"]}, {"id": "bandage", "stackSize": 5},
    {"id": "circuit_board"}, {"id": "energy_module"}, {"id": "bolt", "stackSize": 50},
    {"id": "coin", "stackSize": 99}, {"id": "sandwich", "stackSize": 5},
    {"id": "junk_a", "stackSize": 20}, {"id": "junk_b", "stackSize": 20},
    {"id": "gold_tooth", "tags": ["valuable"]}, {"id": "gem", "tags": ["valuable"]},
    {"id": "scrap", "stackSize": 20}, {"id": "spring"}
  ],
  "lootTables": [
    {"id": "desk_basic", "rolls": [1, 3],
     "entries": [{"item": "coin", "count": [1, 5], "weight": 10}, {"item": "sandwich", "weight": 2},
                 {"table": "junk", "weight": 4}, {"nothing": true, "weight": 6}]},
    {"id": "junk", "rolls": 2, "entries": [{"item": "junk_a", "count": [1, 3]}, {"item": "junk_b"}]},
    {"id": "unique_demo", "rolls": 10,
     "entries": [{"item": "gold_tooth", "unique": true, "weight": 5}, {"item": "coin"}]},
    {"id": "guaranteed_demo", "rolls": 0, "entries": [],
     "guaranteed": [{"item": "soap", "count": 2}, {"tag": "valuable"}]},
    {"id": "odds", "rolls": 1, "entries": [{"item": "coin", "weight": 90}, {"item": "sandwich", "weight": 10}]},
    {"id": "fixed", "rolls": 1, "entries": [{"item": "bolt", "count": 7}]},
    {"id": "big", "rolls": 1, "entries": [{"item": "bolt", "count": 120}]}
  ],
  "lootPools": [
    {"id": "escape_parts", "group": "desks", "items": [{"item": "circuit_board"}, {"item": "energy_module"}]},
    {"id": "bolts", "group": "desks", "perContainer": 1, "items": [{"item": "bolt", "count": 3}, {"item": "bolt", "count": 3}]},
    {"id": "too_many", "group": "lockers", "items": [{"item": "coin", "count": 5}, {"item": "sandwich"}, {"item": "gem"}]},
    {"id": "auto_parts", "group": "shelves", "autoStart": true, "items": [{"item": "spring"}]}
  ],
  "recipes": [
    {"id": "fake_key", "displayName": "Fake Key", "station": "desk", "knownByDefault": false,
     "ingredients": [{"item": "soap"}, {"item": "file", "consume": false, "wear": 2}],
     "output": {"item": "cell_key"}, "statRequirements": {"intellect": 30}},
    {"id": "bandage", "ingredients": [{"tag": "cloth", "count": 2}], "output": {"item": "bandage", "count": 2}},
    {"id": "dismantle", "ingredients": [{"item": "cell_key"}],
     "outputs": [{"item": "scrap", "count": 3}, {"item": "spring"}], "requires": {"var": "workshop_open"}}
  ]
})";

struct Rig {
    ComponentRegistry registry;
    MemoryAssets assets;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<GameRuntime> runtime;
    std::vector<EntityId> desks;
    EntityId hero;
    std::vector<GameEvent> heard;
    std::uint64_t seed{7};

    Rig() {
        registerEngineComponents(registry);
        assets.files["data/world.ykdata"] = worldData;
        scene = std::make_unique<Scene>(registry, 4);
    }
    Entity &container(const char *name, const char *group, int slots = 2) {
        Entity &entity = scene->createEntity(name);
        entity.add<Inventory>().slots = slots;
        entity.add<Container>().group = group;
        return entity;
    }
    Entity &person(const char *name, int slots = 8) {
        Entity &entity = scene->createEntity(name);
        entity.add<StatSet>();
        entity.add<StatusEffects>();
        entity.add<Inventory>().slots = slots;
        entity.add<Crafter>();
        return entity;
    }
    void start() {
        RuntimeOptions options;
        options.assets = &assets;
        options.randomSeed = seed;
        auto created = GameRuntime::create(std::move(scene), options);
        CHECK(created);
        if (!created)
            return;
        runtime = std::move(created.value());
        runtime->events().subscribe("*",
                                    [this](const GameEvent &event) { heard.push_back(event); });
        runtime->stepOnce(Keyboard{});
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
    GameContext &game() {
        return *runtime;
    }
};

GameData loadWorld(std::vector<DataProblem> &problems) {
    MemoryAssets assets;
    assets.files["data/world.ykdata"] = worldData;
    return GameData::load(assets, problems);
}

// ---- Loot definitions
// ----------------------------------------------------------------------------
void lootDefinitions() {
    const auto range = [](const char *text) { return IntRange::fromJson(J(text), "count", 1); };
    CHECK(range("5").value() == IntRange{5, 5} && range("[2, 6]").value() == IntRange{2, 6} &&
          range(R"({"min":1,"max":3})").value() == IntRange{1, 3} &&
          IntRange::fromJson(Json(), "count").value() == IntRange{});
    CHECK(has(range("0").error(), "'count' must run from 1") &&
          has(range("[5, 2]").error(), "'count'") &&
          has(range(R"("many")").error(), "'count' must be a number") &&
          has(range("[1]").error(), "[low, high]"));
    CHECK(IntRange{3, 3}.toJson() == Json(3) && IntRange{1, 4}.toJson().get("max").asNumber() == 4);

    const auto entryError = [](const char *text) {
        auto entry = LootEntry::fromJson(J(text));
        return entry ? std::string() : entry.error();
    };
    CHECK(has(entryError("[]"), "must be an object"));
    CHECK(has(entryError("{}"), "exactly one"));
    CHECK(has(entryError(R"({"item":"a","table":"b"})"), "exactly one"));
    CHECK(has(entryError(R"({"item":""})"), "'item' is empty"));
    CHECK(has(entryError(R"({"item":"a","weight":0})"), "'weight'"));
    CHECK(has(entryError(R"({"item":"a","count":[3,1]})"), "'count'"));
    auto nothing = LootEntry::fromJson(J(R"({"nothing":true,"weight":6})"));
    CHECK(nothing && nothing.value().kind == LootEntry::Kind::Nothing &&
          nothing.value().weight == 6.0);
    auto category = LootEntry::fromJson(J(R"({"tag":"tool","count":[1,2],"unique":true})"));
    CHECK(category && category.value().kind == LootEntry::Kind::Category &&
          category.value().unique);

    std::vector<std::string> warnings;
    auto table = LootTable::fromJson(
        J(R"({"id":"t","rolls":[1,2],"entries":[{"item":"a","count":[1,3],"weight":4,"unique":true},{"table":"u"},{"tag":"x"},{"nothing":true}],
              "guaranteed":[{"item":"b","count":2}]})"),
        warnings);
    CHECK(table && table.value().entries.size() == 4 && table.value().guaranteed.size() == 1 &&
          warnings.empty());
    CHECK(LootTable::fromJson(table.value().toJson(), warnings).value().toJson() ==
          table.value().toJson());
    CHECK(has(LootTable::fromJson(J(R"({"id":"t","entries":[{}]})"), warnings).error(), "entry 1"));
    CHECK(has(LootTable::fromJson(J(R"({"id":"t","entries":3})"), warnings).error(),
              "must be a list"));
    CHECK(has(LootTable::fromJson(J(R"({"id":"t","rolls":-1})"), warnings).error(), "'rolls'"));

    auto pool = LootPool::fromJson(
        J(R"({"id":"p","group":"desks","perContainer":2,"autoStart":true,"items":[{"item":"a"},{"tag":"x","count":2}]})"),
        warnings);
    CHECK(pool && pool.value().perContainer == 2 && pool.value().autoStart &&
          pool.value().items.size() == 2);
    CHECK(LootPool::fromJson(pool.value().toJson(), warnings).value().toJson() ==
          pool.value().toJson());
    CHECK(has(LootPool::fromJson(J(R"({"id":"p","group":"g","items":[]})"), warnings).error(),
              "needs 'items'"));
    CHECK(
        has(LootPool::fromJson(J(R"({"id":"p","group":"g","items":[{"nothing":true}]})"), warnings)
                .error(),
            "real things"));
    CHECK(has(LootPool::fromJson(J(R"({"id":"p","items":[{"item":"a"}]})"), warnings).error(),
              "container group"));
    warnings.clear();
    CHECK(LootPool::fromJson(J(R"({"id":"p","group":"g","items":[{"item":"a"}],"autostart":true})"),
                             warnings));
    CHECK(warnings.size() == 1 && has(warnings[0], "'autostart'"));
}

void lootFiles() {
    MemoryAssets assets;
    assets.files["data/world.ykdata"] = worldData;
    assets.files["loot/more.ykloot"] = R"({"tables":[
        {"id":"loop_a","rolls":1,"entries":[{"table":"loop_b"}]},{"id":"loop_b","rolls":1,"entries":[{"table":"loop_a"}]},
        {"id":"self","rolls":1,"entries":[{"table":"self"}]},
        {"id":"typo","rolls":1,"entries":[{"item":"unicorn"},{"table":"nowhere"},{"tag":"nothingtagged"}],"guaranteed":[{"item":"phoenix"}]},
        {"id":"empty","rolls":1,"entries":[]},{"id":"never","rolls":0,"entries":[{"item":"coin"}]}],
      "pools":[{"id":"bad","group":"g","items":[{"table":"junk"},{"item":"ghost"}]}]})";
    std::vector<DataProblem> problems;
    const GameData data = GameData::load(assets, problems);
    CHECK(data.loot.tables.size() == 13 && data.loot.pools.size() == 5);
    const auto problem = [&](const char *file, const char *part, bool error = true) {
        return std::any_of(problems.begin(), problems.end(), [&](const DataProblem &item) {
            return item.file == file && has(item.message, part) && item.error == error;
        });
    };
    ComponentRegistry registry;
    registerEngineComponents(registry);
    problems.clear();
    data.check(registry.extension<RuleCatalog>(), problems);
    CHECK(problem("loot/more.ykloot", "loot table 'loop_a': contains itself"));
    CHECK(problem("loot/more.ykloot", "loot table 'self': contains itself"));
    CHECK(problem("loot/more.ykloot", "loot table 'typo': the item 'unicorn' is not defined"));
    CHECK(problem("loot/more.ykloot", "the table 'nowhere' is not defined"));
    CHECK(problem("loot/more.ykloot", "no item has the tag 'nothingtagged'", false));
    CHECK(problem("loot/more.ykloot", "guaranteed: the item 'phoenix' is not defined"));
    CHECK(problem("loot/more.ykloot", "loot table 'empty': has no entries", false));
    CHECK(
        problem("loot/more.ykloot", "loot table 'never': has entries but never rolls them", false));
    CHECK(problem("loot/more.ykloot", "loot pool 'bad': deals items, not tables ('junk')"));
    CHECK(problem("loot/more.ykloot", "loot pool 'bad': the item 'ghost' is not defined"));
    CHECK(!problem("data/world.ykdata", "not defined") &&
          !problem("data/world.ykdata", "contains itself"));
    CHECK(data.known("loot", "junk") && !data.known("loot", "ghost") &&
          data.known("loot pool", "bolts") && !data.known("loot pool", "x"));
    const auto rows = data.summary();
    CHECK(std::any_of(rows.begin(), rows.end(), [](const auto &row) {
        return row.first == "Loot tables" && row.second == "13";
    }));
}

// ---- Rolling
// -------------------------------------------------------------------------------------
std::map<std::string, int> totals(const std::vector<ItemStack> &stacks) {
    std::map<std::string, int> sums;
    for (const ItemStack &stack : stacks)
        sums[stack.item.str()] += stack.count;
    return sums;
}

void rolling() {
    std::vector<DataProblem> problems;
    const GameData data = loadWorld(problems);
    Rng rng(1);
    LootRoller roller(data.items, data.loot, rng);
    CHECK(roller.roll("nowhere").empty());

    // Quantity ranges and nested tables; equal seeds give equal loot, another seed another.
    const auto sample = [&](std::uint64_t seed) {
        Rng local(seed);
        LootRoller r(data.items, data.loot, local);
        std::vector<std::string> lines;
        for (int i = 0; i < 20; ++i)
            for (const ItemStack &stack : r.roll("desk_basic"))
                lines.push_back(stack.item.str() + "x" + std::to_string(stack.count));
        return lines;
    };
    CHECK(sample(5) == sample(5) && sample(5) != sample(6));
    std::set<std::string> seen;
    int rollsWithNothing = 0;
    for (int i = 0; i < 400; ++i) {
        const auto stacks = roller.roll("desk_basic");
        rollsWithNothing += stacks.empty() ? 1 : 0;
        for (const ItemStack &stack : stacks) {
            seen.insert(stack.item.str());
            const std::string &id = stack.item.str();
            CHECK(id == "coin" || id == "sandwich" || id == "junk_a" || id == "junk_b");
            CHECK(stack.count >= 1 && stack.count <= (id == "coin"       ? 5 * 3
                                                      : id == "sandwich" ? 5
                                                                         : 20));
            CHECK(stack.durability == -1 && stack.owner.empty());
        }
    }
    CHECK(seen.size() == 4 && rollsWithNothing > 0);

    // Weights: 90 to 10.
    int coins = 0;
    for (int i = 0; i < 4000; ++i)
        coins += roller.roll("odds").front().item.str() == "coin" ? 1 : 0;
    CHECK(coins > 3450 && coins < 3750);

    // Unique entries come up once per roll however many rolls there are; the rest still fill in.
    for (int i = 0; i < 50; ++i) {
        const auto sums = totals(roller.roll("unique_demo"));
        const int teeth = sums.count("gold_tooth") ? sums.at("gold_tooth") : 0;
        CHECK(teeth <= 1 && sums.at("coin") >= 5);
    }
    // Guaranteed entries are always there, with their counts; a category picks something tagged.
    for (int i = 0; i < 30; ++i) {
        const auto sums = totals(roller.roll("guaranteed_demo"));
        CHECK(sums.at("soap") == 2 && (sums.count("gold_tooth") + sums.count("gem")) == 1 &&
              sums.size() == 2);
    }
    // Stacks merge up to the stack size and then start another.
    const auto big = roller.roll("big");
    CHECK(big.size() == 3 && big[0].count == 50 && big[1].count == 50 && big[2].count == 20);
    CHECK(roller.roll("fixed").size() == 1 && roller.roll("fixed")[0].count == 7);

    // A pool expands to the stacks it deals.
    const auto parts = roller.expand(*data.loot.pools.find("escape_parts"));
    CHECK(parts.size() == 2 && parts[0].item.str() == "circuit_board" &&
          parts[1].item.str() == "energy_module");
    CHECK(roller.expand(*data.loot.pools.find("bolts")).size() == 2);
    // A table that contains itself does not hang a roll (validation reports it).
    LootCatalog loops;
    std::vector<std::string> loopWarnings;
    std::string loopError;
    CHECK(loops.tables.add(
        LootTable::fromJson(J(R"({"id":"self","rolls":1,"entries":[{"table":"self"}]})"),
                            loopWarnings)
            .value(),
        loopError));
    Rng spin(2);
    LootRoller looping(data.items, loops, spin);
    CHECK(looping.roll("self").empty());
}

void containersFill() {
    // A seed makes a container hold the same things in every game; without one it follows the dice.
    const auto contents = [](std::uint64_t gameSeed, int lootSeed) {
        Rig rig;
        rig.seed = gameSeed;
        Entity &desk = rig.container("Desk", "desks", 6);
        auto &box = *desk.get<Container>();
        box.lootTable = "desk_basic";
        box.lootSeed = lootSeed;
        box.owner = "npc.warden";
        const EntityId id = desk.id();
        rig.start();
        std::vector<std::string> list;
        const Inventory &inventory = rig.inventory(id);
        for (int i = 0; i < inventory.size(); ++i)
            if (!inventory.slot(i).empty()) {
                list.push_back(inventory.slot(i).item.str() + "x" +
                               std::to_string(inventory.slot(i).count));
                CHECK(inventory.slot(i).owner ==
                      "npc.warden"); // Loot belongs to whoever owns the container.
            }
        return list;
    };
    CHECK(contents(1, 77) == contents(2, 77) && contents(1, 77) == contents(99, 77));
    bool differ = false;
    for (std::uint64_t seed = 1; seed < 8 && !differ; ++seed)
        differ = contents(seed, 0) != contents(1, 0);
    CHECK(differ);
    CHECK(contents(3, 0) == contents(3, 0)); // The same game seed repeats.

    // Filled once: a saved container that was filled and emptied stays empty after loading.
    Rig rig;
    Entity &desk = rig.container("Desk", "desks", 4);
    desk.get<Container>()->lootTable = "fixed";
    const EntityId id = desk.id();
    Entity &broken = rig.container("Broken", "desks", 4);
    broken.get<Container>()->lootTable = "no_such_table";
    const EntityId brokenId = broken.id();
    setLogStderrEnabled(false);
    rig.start();
    CHECK(rig.inventory(id).count(ItemId("bolt")) == 7 && rig.inventory(brokenId).freeSlots() == 4);
    const Json saved = rig.entity(id).get<Container>()->saveState();
    CHECK(saved.get("generated").asBool());
    rig.inventory(id).clear(rig.game());
    CHECK(rig.entity(id).get<Container>()->loadState(rig.game(), saved));
    // A validation pass names the table that is not there.
    std::vector<std::string> problems;
    GameData data;
    std::vector<DataProblem> dataProblems;
    data.add(J(worldData), "w.ykdata", dataProblems);
    CheckContext context;
    context.known = [&](std::string_view kind, std::string_view what) {
        return data.known(kind, what);
    };
    rig.entity(brokenId).get<Container>()->type().check(
        rig.entity(brokenId), *rig.entity(brokenId).get<Container>(), context, problems);
    CHECK(problems.size() == 1 && has(problems[0], "'no_such_table' is not defined"));
}

void pools() {
    Rig rig;
    for (const char *name : {"DeskA", "DeskB", "DeskC"})
        rig.desks.push_back(rig.container(name, "desks", 2).id());
    const EntityId locker = rig.container("Locker", "lockers", 1).id();
    const EntityId stray = rig.container("Stray", "", 5).id();
    rig.start();
    GameContext &game = rig.game();
    auto &service = game.services().get<LootService>();
    // Each must-exist item lands in exactly one container of the group.
    auto placed = service.scatter(game, "escape_parts");
    CHECK(placed.size() == 2 && placed[0].container && placed[1].container);
    int boards = 0, modules = 0;
    for (const EntityId desk : rig.desks) {
        boards += rig.inventory(desk).count(ItemId("circuit_board"));
        modules += rig.inventory(desk).count(ItemId("energy_module"));
    }
    CHECK(boards == 1 && modules == 1 && rig.inventory(locker).freeSlots() == 1 &&
          rig.inventory(stray).freeSlots() == 5);
    // perContainer: three desks, six stacks of bolts, at most one stack each: only three fit.
    setLogStderrEnabled(false);
    placed = service.scatter(game, "bolts");
    CHECK(placed.size() == 2);
    int bolts = 0;
    for (const EntityId desk : rig.desks)
        bolts += rig.inventory(desk).count(ItemId("bolt"));
    CHECK(bolts == 6 &&
          std::all_of(placed.begin(), placed.end(), [](const auto &p) { return p.container; }));
    CHECK(placed[0].container != placed[1].container);
    // No room: the locker has one slot for three stacks.
    rig.step();
    rig.heard.clear();
    placed = service.scatter(game, "too_many");
    CHECK(placed.size() == 3);
    const auto dealt = std::count_if(placed.begin(), placed.end(),
                                     [](const auto &p) { return static_cast<bool>(p.container); });
    CHECK(dealt == 1 && rig.inventory(locker).freeSlots() == 0);
    rig.step();
    CHECK(rig.count("loot.failed") == 2 &&
          rig.last("loot.failed")->data.get("pool").asString() == "too_many");
    CHECK(service.scatter(game, "no_such_pool").empty());

    // The same seed deals the same way; another seed deals differently somewhere.
    const auto layout = [](std::uint64_t seed) {
        Rig other;
        other.seed = seed;
        std::vector<EntityId> ids;
        for (const char *name : {"DeskA", "DeskB", "DeskC", "DeskD", "DeskE"})
            ids.push_back(other.container(name, "desks", 2).id());
        other.start();
        other.game().services().get<LootService>().scatter(other.game(), "escape_parts");
        std::string where;
        for (const EntityId id : ids)
            where += std::to_string(other.inventory(id).count(ItemId("circuit_board"))) +
                     std::to_string(other.inventory(id).count(ItemId("energy_module")));
        return where;
    };
    CHECK(layout(3) == layout(3));
    std::set<std::string> layouts;
    for (std::uint64_t seed = 1; seed <= 12; ++seed)
        layouts.insert(layout(seed));
    CHECK(layouts.size() > 3); // Where things are varies; that they exist does not.
}

void autoPools() {
    Rig rig;
    for (const char *name : {"Shelf1", "Shelf2"})
        rig.container(name, "shelves", 1);
    rig.desks = {rig.scene->findByName("Shelf1")->id(), rig.scene->findByName("Shelf2")->id()};
    rig.start();
    // The pool that says autoStart is dealt when the scene starts, once.
    int springs = 0;
    for (const EntityId shelf : rig.desks)
        springs += rig.inventory(shelf).count(ItemId("spring"));
    CHECK(springs == 1);
    rig.step(3);
    springs = 0;
    for (const EntityId shelf : rig.desks)
        springs += rig.inventory(shelf).count(ItemId("spring"));
    CHECK(springs == 1);
}

void lootRules() {
    Rig rig;
    for (const char *name : {"DeskA", "DeskB"})
        rig.desks.push_back(rig.container(name, "desks", 2).id());
    const EntityId labId = rig.container("Lab", "labs", 4).id();
    Entity &console = rig.scene->createEntity("Console");
    CHECK(console.add<RuleSet>().setRulesJson(J(R"([
      {"id":"deal","when":"deal","then":{"type":"ScatterLoot","pool":"escape_parts"}},
      {"id":"fill","when":"fill","then":{"type":"FillContainer","entity":"name:Lab"}},
      {"id":"overfull","when":"overfull","then":{"type":"ScatterLoot","pool":"too_many"}}])")));
    const EntityId consoleId = console.id();
    setLogStderrEnabled(false);
    rig.start();
    const auto fire = [&](const char *name) {
        rig.runtime->events().emit(GameEvent(name, consoleId, {}));
        rig.step();
    };
    fire("deal");
    int parts = 0;
    for (const EntityId desk : rig.desks)
        parts += rig.inventory(desk).count(ItemId("circuit_board")) +
                 rig.inventory(desk).count(ItemId("energy_module"));
    CHECK(parts == 2);
    // A table given to a container after the start is rolled once, by rule.
    rig.entity(labId).get<Container>()->lootTable = "fixed";
    fire("fill");
    CHECK(rig.inventory(labId).count(ItemId("bolt")) == 7);
    fire("fill");
    CHECK(rig.inventory(labId).count(ItemId("bolt")) == 7);
    fire("overfull"); // No lockers at all: the action fails and the game goes on.
    // Validation: unknown pools are caught in rules.
    GameData data;
    std::vector<DataProblem> problems;
    data.add(J(worldData), "w.ykdata", problems);
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
    report.data = &data;
    auto bad = rulesFromJson(J(R"([{"when":"x","then":{"type":"ScatterLoot","pool":"nowhere"}}])"));
    check(*rig.registry.extension<RuleCatalog>(), bad.value().front(), report);
    CHECK(report.errors.size() == 1 && has(report.errors[0], "no loot pool 'nowhere'"));
}

// ---- Recipes
// -------------------------------------------------------------------------------------
void recipeDefinitions() {
    std::vector<std::string> warnings;
    auto recipe = RecipeDefinition::fromJson(
        J(R"({"id":"fake_key","displayName":"Fake Key","description":"A key from soap.","category":"keys",
              "ingredients":[{"item":"soap","count":2},{"item":"file","consume":false,"wear":3},{"tag":"cloth","count":1}],
              "output":{"item":"cell_key","count":1},"statRequirements":{"intellect":30},"station":"desk",
              "knownByDefault":false,"tags":["contraband"],"requires":{"var":"ok"}})"),
        warnings);
    CHECK(recipe && warnings.empty());
    if (recipe) {
        const RecipeDefinition &def = recipe.value();
        CHECK(def.ingredients.size() == 3 && def.ingredients[0].count == 2 &&
              !def.ingredients[1].consume && def.ingredients[1].wear == 3 &&
              def.ingredients[2].tag == "cloth" && def.outputs.size() == 1 &&
              def.statRequirements.at("intellect") == 30 && def.station == "desk" &&
              !def.knownByDefault && !def.condition.empty());
        CHECK(RecipeDefinition::fromJson(def.toJson(), warnings).value().toJson() == def.toJson());
    }
    auto many = RecipeDefinition::fromJson(
        J(R"({"id":"r","ingredients":[{"item":"a"}],"outputs":[{"item":"b"},{"item":"c","count":2}]})"),
        warnings);
    CHECK(many && many.value().outputs.size() == 2 && many.value().knownByDefault);
    CHECK(RecipeDefinition::fromJson(many.value().toJson(), warnings).value().toJson() ==
          many.value().toJson());
    const auto error = [&](const char *text) {
        auto parsed = RecipeDefinition::fromJson(J(text), warnings);
        return parsed ? std::string() : parsed.error();
    };
    CHECK(has(error(R"({"id":"r","output":{"item":"b"}})"), "'ingredients' must be a list"));
    CHECK(has(error(R"({"id":"r","ingredients":[],"output":{"item":"b"}})"),
              "at least one ingredient"));
    CHECK(has(error(R"({"id":"r","ingredients":[{"item":"a","tag":"t"}],"output":{"item":"b"}})"),
              "exactly one of 'item' or 'tag'"));
    CHECK(has(error(R"({"id":"r","ingredients":[{"count":2}],"output":{"item":"b"}})"),
              "ingredient 1"));
    CHECK(has(error(R"({"id":"r","ingredients":[{"item":"a","wear":2}],"output":{"item":"b"}})"),
              "'wear' only applies"));
    CHECK(has(error(R"({"id":"r","ingredients":[{"item":"a","count":0}],"output":{"item":"b"}})"),
              "'count'"));
    CHECK(has(error(R"({"id":"r","ingredients":[{"item":"a"}]})"), "'output' (one) or 'outputs'"));
    CHECK(has(
        error(
            R"({"id":"r","ingredients":[{"item":"a"}],"output":{"item":"b"},"outputs":[{"item":"c"}]})"),
        "'output' (one) or 'outputs'"));
    CHECK(has(error(R"({"id":"r","ingredients":[{"item":"a"}],"output":{}})"), "needs an 'item'"));
    CHECK(has(error(R"({"id":"r","ingredients":[{"item":"a"}],"outputs":[]})"),
              "at least one output"));
    CHECK(has(
        error(
            R"({"id":"r","ingredients":[{"item":"a"}],"output":{"item":"b"},"statRequirements":[1]})"),
        "'statRequirements'"));
    CHECK(has(
        error(
            R"({"id":"r","ingredients":[{"item":"a"}],"output":{"item":"b"},"requires":{"all":3}})"),
        "requires"));
}

void recipeFiles() {
    MemoryAssets assets;
    assets.files["data/world.ykdata"] = worldData;
    assets.files["recipes/shiv.ykrecipe"] =
        R"({"id":"shiv","ingredients":[{"item":"scrap","count":2},{"item":"file","consume":false,"wear":1}],"output":{"item":"cell_key"}})";
    assets.files["recipes/zbad.ykrecipe"] = R"({"recipes":[
        {"id":"ghosts","ingredients":[{"item":"unicorn"},{"tag":"nothingtagged"}],"output":{"item":"phoenix"},"statRequirements":{"charm":5}},
        {"id":"free","ingredients":[{"item":"file","consume":false}],"output":{"item":"soap"}},
        {"id":"scrap_wear","ingredients":[{"item":"soap","consume":false,"wear":1}],"output":{"item":"soap"}},
        {"id":"shiv","ingredients":[{"item":"soap"}],"output":{"item":"soap"}}]})";
    std::vector<DataProblem> problems;
    const GameData data = GameData::load(assets, problems);
    CHECK(data.recipes.recipes.contains("shiv") && data.recipes.recipes.contains("fake_key") &&
          data.recipes.recipes.contains("ghosts"));
    const auto problem = [&](const char *file, const char *part, bool error = true) {
        return std::any_of(problems.begin(), problems.end(), [&](const DataProblem &item) {
            return item.file == file && has(item.message, part) && item.error == error;
        });
    };
    CHECK(problem("recipes/zbad.ykrecipe", "two definitions called 'shiv'"));
    ComponentRegistry registry;
    registerEngineComponents(registry);
    problems.clear();
    data.check(registry.extension<RuleCatalog>(), problems);
    CHECK(problem("recipes/zbad.ykrecipe", "the ingredient 'unicorn' is not an item"));
    CHECK(problem("recipes/zbad.ykrecipe", "no item has the tag 'nothingtagged'"));
    CHECK(problem("recipes/zbad.ykrecipe", "it makes 'phoenix', which is not an item"));
    CHECK(problem("recipes/zbad.ykrecipe", "it needs the stat 'charm'"));
    CHECK(problem("recipes/zbad.ykrecipe", "recipe 'free': uses nothing up", false));
    CHECK(problem("recipes/zbad.ykrecipe", "wears 'soap' down but it never wears out", false));
    CHECK(!problem("recipes/shiv.ykrecipe", "not") && !problem("data/world.ykdata", "recipe"));
    CHECK(data.known("recipe", "fake_key") && !data.known("recipe", "x"));
    const auto rows = data.summary();
    CHECK(std::any_of(rows.begin(), rows.end(),
                      [](const auto &row) { return row.first == "Recipes"; }));
}

// ---- Crafting
// ------------------------------------------------------------------------------------
void crafting() {
    Rig rig;
    Entity &hero = rig.person("Hero", 6);
    hero.setWorldPosition({0.0F, 0.0F});
    rig.hero = hero.id();
    Entity &desk = rig.scene->createEntity("Desk");
    desk.setWorldPosition({1.0F, 0.0F});
    desk.add<CraftingStation>().station = "desk";
    Entity &forge = rig.scene->createEntity("Forge");
    forge.setWorldPosition({30.0F, 0.0F});
    forge.add<CraftingStation>().station = "desk";
    const EntityId forgeId = forge.id();
    rig.start();
    GameContext &game = rig.game();
    Crafter &crafter = *rig.entity(rig.hero).get<Crafter>();
    Inventory &inventory = rig.inventory(rig.hero);
    CHECK(rig.runtime->services().get<SpatialIndexService>().count("station") == 2);

    // What is not allowed, and why.
    CHECK(crafter.check(game, "nothing").reason == "There is no such recipe.");
    CHECK(crafter.check(game, "fake_key").reason == "You don't know how to make that.");
    CHECK(!crafter.knows(game, "fake_key") && crafter.knows(game, "bandage") &&
          !crafter.knows(game, "nothing"));
    CHECK(crafter.available(game).size() == 2); // The two everyone knows.
    CHECK(crafter.learn(game, "fake_key") && !crafter.learn(game, "fake_key") &&
          !crafter.learn(game, "nothing"));
    CHECK(crafter.available(game).size() == 3 && crafter.knows(game, "fake_key"));
    CHECK(crafter.check(game, "bandage").reason == "You need 2 x something cloth.");
    inventory.addItem(game, "cloth_a", 1);
    inventory.addItem(game, "cloth_b", 1);
    CHECK(crafter.check(game, "bandage").ok); // Any two cloths do.
    CHECK(crafter.check(game, "fake_key").reason == "You need 30 Intellect.");
    rig.entity(rig.hero).get<StatSet>()->set(game, "intellect", 45);
    CHECK(crafter.check(game, "fake_key").reason == "You need 1 x soap.");
    inventory.addItem(game, "soap", 1);
    CHECK(crafter.check(game, "fake_key").reason == "You need 1 x file.");
    inventory.addItem(game, "file", 1);
    CHECK(crafter.check(game, "fake_key").ok);

    // The station: the desk is close, the forge is far, and standing elsewhere there is none.
    CHECK(crafter.check(game, "fake_key").ok);
    game.teleport(rig.entity(rig.hero), {30.0F, 5.0F});
    rig.step();
    CHECK(crafter.check(game, "fake_key").reason == "You need to be at a desk.");
    game.teleport(rig.entity(rig.hero), {30.5F, 0.5F});
    CHECK(crafter.check(game, "fake_key").ok); // The forge is a desk too.
    rig.entity(forgeId).get<CraftingStation>()->station = "forge";
    game.teleport(rig.entity(rig.hero), {1.2F, 0.0F});
    CHECK(crafter.check(game, "fake_key").ok);

    // Crafting uses things up, wears the tool, makes the key and says so.
    rig.heard.clear();
    const CraftResult made = crafter.craft(game, "fake_key");
    CHECK(made.ok && made.outputs.size() == 1 && made.outputs[0].item.str() == "cell_key");
    CHECK(inventory.count(ItemId("soap")) == 0 && inventory.count(ItemId("file")) == 1 &&
          inventory.bestDurability(ItemId("file")) == 8 &&
          inventory.count(ItemId("cell_key")) == 1);
    rig.step();
    CHECK(rig.count("craft.completed") == 1 &&
          rig.last("craft.completed")->data.get("recipe").asString() == "fake_key" &&
          rig.last("craft.completed")->data.get("outputs").at(0).get("item").asString() ==
              "cell_key" &&
          rig.last("craft.completed")->source == rig.hero);
    // Not again: the soap is gone. The failure is an event too.
    const CraftResult again = crafter.craft(game, "fake_key");
    CHECK(!again.ok && again.reason == "You need 1 x soap.");
    rig.step();
    CHECK(rig.count("craft.failed") == 1 &&
          rig.last("craft.failed")->data.get("reason").asString() == "You need 1 x soap.");
    // A tool worn through is refused, and one that breaks while crafting is gone.
    inventory.addItem(game, "soap", 3);
    Inventory::Location where = inventory.find(ItemId("file"));
    CHECK(where.valid() && where.slot >= 0);
    inventory.stackAt(where)->durability = 2;
    CHECK(crafter.craft(game, "fake_key").ok && inventory.count(ItemId("file")) == 0);
    CHECK(inventory.count(ItemId("cell_key")) == 2);
    inventory.addItem(game, "file");
    inventory.stackAt(inventory.find(ItemId("file")))->durability = 0;
    CHECK(crafter.check(game, "fake_key").reason == "Your file is worn out.");

    // Tag ingredients take whichever cloth there is; several outputs; the extra condition.
    inventory.clear(game);
    inventory.addItem(game, "cloth_a", 1);
    inventory.addItem(game, "cloth_b", 3);
    CHECK(crafter.craft(game, "bandage").ok && inventory.countWithTag(game, "cloth") == 2 &&
          inventory.count(ItemId("bandage")) == 2);
    CHECK(crafter.craft(game, "bandage").ok && inventory.countWithTag(game, "cloth") == 0 &&
          inventory.count(ItemId("bandage")) == 4);
    inventory.addItem(game, "cell_key");
    CHECK(crafter.learn(game, "dismantle") == false); // Known by default already.
    CHECK(crafter.check(game, "dismantle").reason == "You can't make that right now.");
    rig.runtime->blackboard().setBool("workshop_open", true);
    const CraftResult parts = crafter.craft(game, "dismantle");
    CHECK(parts.ok && parts.outputs.size() == 2 && inventory.count(ItemId("scrap")) == 3 &&
          inventory.count(ItemId("spring")) == 1);
    // What does not fit lies at the crafter's feet.
    inventory.clear(game);
    inventory.addItem(game, "cell_key");
    for (int i = 0; i < 5; ++i)
        inventory.addItem(game, "junk_a", 20);
    CHECK(inventory.freeSlots() == 0);
    CHECK(crafter.craft(game, "dismantle").ok && inventory.count(ItemId("scrap")) == 3 &&
          inventory.count(ItemId("spring")) == 0);
    rig.step(2);
    const auto &spatial = rig.runtime->services().get<SpatialIndexService>();
    CHECK(spatial.near("pickup", rig.entity(rig.hero).worldPosition(), 1.0F).size() >= 1);

    // Forgetting and saving what was learned.
    CHECK(crafter.forget("fake_key") && !crafter.forget("fake_key") &&
          !crafter.knows(game, "fake_key"));
    crafter.learn(game, "fake_key");
    const Json saved = crafter.saveState();
    crafter.forget("fake_key");
    CHECK(crafter.loadState(game, saved) && crafter.knows(game, "fake_key"));
    CHECK(crafter.loadState(game, J(R"({"known":["fake_key","ghost"]})")) &&
          crafter.known.size() == 1);
    // A crafter added later gets an inventory of its own (Crafter depends on it) and an empty one
    // makes nothing.
    Entity &empty = rig.runtime->scene().createEntity("Empty");
    auto &lonely = empty.add<Crafter>();
    CHECK(entity_hasInventory(empty) &&
          lonely.check(game, "bandage").reason == "You need 2 x something cloth.");
}

void craftingRules() {
    Rig rig;
    Entity &hero = rig.person("Hero", 6);
    rig.hero = hero.id();
    Entity &console = rig.scene->createEntity("Console");
    CHECK(console.add<RuleSet>().setRulesJson(J(R"([
      {"id":"can","when":"can","if":{"all":[{"type":"CanCraft","recipe":"bandage"},{"type":"KnowsRecipe","recipe":"bandage"}]},
       "then":{"type":"SetVariable","name":"can","value":true},"else":{"type":"SetVariable","name":"can","value":false}},
      {"id":"learn","when":"learn","then":{"type":"LearnRecipe","recipe":"fake_key"}},
      {"id":"make","when":"make","then":{"type":"Craft","recipe":"bandage"}},
      {"id":"knows","when":"knows","if":{"type":"KnowsRecipe","recipe":"fake_key"},
       "then":{"type":"SetVariable","name":"knows","value":true},"else":{"type":"SetVariable","name":"knows","value":false}}])")));
    const EntityId consoleId = console.id();
    rig.start();
    GameContext &game = rig.game();
    Blackboard &board = rig.runtime->blackboard();
    const auto fire = [&](const char *name) {
        rig.runtime->events().emit(GameEvent(name, consoleId, rig.hero));
        rig.step();
    };
    fire("can");
    CHECK(board.has("can") && !board.flag("can"));
    rig.inventory(rig.hero).addItem(game, "cloth_a", 2);
    fire("can");
    CHECK(board.flag("can"));
    fire("make");
    CHECK(rig.inventory(rig.hero).count(ItemId("bandage")) == 2 &&
          rig.inventory(rig.hero).count(ItemId("cloth_a")) == 0);
    fire("knows");
    CHECK(!board.flag("knows"));
    fire("learn");
    fire("knows");
    CHECK(board.flag("knows"));
    // A character without the Crafter component, and unknown recipes, fail without trouble.
    RuleContext ctx(game);
    ctx.actor = consoleId;
    ctx.origin = "test";
    CHECK(execute(Action::fromJson(J(R"({"type":"Craft","recipe":"bandage"})")).value(), ctx) ==
          ActionResult::Failed);
    ctx.actor = rig.hero;
    CHECK(execute(Action::fromJson(J(R"({"type":"Craft","recipe":"nothing"})")).value(), ctx) ==
          ActionResult::Failed);
    CHECK(execute(Action::fromJson(J(R"({"type":"LearnRecipe","recipe":"fake_key"})")).value(),
                  ctx) == ActionResult::Failed);

    // The components check their own names.
    GameData data;
    std::vector<DataProblem> problems;
    data.add(J(worldData), "w.ykdata", problems);
    CheckContext context;
    context.known = [&](std::string_view kind, std::string_view id) {
        return data.known(kind, id);
    };
    auto &crafter = *rig.entity(rig.hero).get<Crafter>();
    crafter.known = {"fake_key", "ghost"};
    std::vector<std::string> found;
    crafter.type().check(rig.entity(rig.hero), crafter, context, found);
    CHECK(found.size() == 1 && has(found[0], "'ghost', which is not defined"));
    Entity &station = rig.runtime->scene().createEntity("S");
    auto &place = station.add<CraftingStation>();
    place.station.clear();
    found.clear();
    place.type().check(station, place, context, found);
    CHECK(found.size() == 1 && has(found[0], "no kind of station"));
}
} // namespace

int main() {
    setLogStderrEnabled(false);
    lootDefinitions();
    lootFiles();
    rolling();
    containersFill();
    pools();
    autoPools();
    lootRules();
    recipeDefinitions();
    recipeFiles();
    crafting();
    craftingRules();
    return yk::test::finish("loot_crafting");
}
