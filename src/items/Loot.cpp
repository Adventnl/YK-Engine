#include "yk/items/Loot.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/items/Inventory.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/runtime/Random.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
// ---- Definitions
// ---------------------------------------------------------------------------------
Result<IntRange> IntRange::fromJson(const Json &json, const char *what, int lowest) {
    IntRange range;
    if (json.isNull())
        return range;
    int low = 0, high = 0;
    if (json.isNumber()) {
        low = high = static_cast<int>(json.asNumber());
    } else if (json.isArray() && json.size() == 2 && json.at(0).isNumber() &&
               json.at(1).isNumber()) {
        low = static_cast<int>(json.at(0).asNumber());
        high = static_cast<int>(json.at(1).asNumber());
    } else if (json.isObject() && json.get("min").isNumber() && json.get("max").isNumber()) {
        low = static_cast<int>(json.get("min").asNumber());
        high = static_cast<int>(json.get("max").asNumber());
    } else {
        return Error{std::string("'") + what +
                     "' must be a number, [low, high] or {\"min\": low, \"max\": high}"};
    }
    if (low < lowest || high < low)
        return Error{std::string("'") + what + "' must run from " + std::to_string(lowest) +
                     " or more up to a number at least as big"};
    range.min = low;
    range.max = high;
    return range;
}

Json IntRange::toJson() const {
    if (min == max)
        return Json(min);
    Json json = Json::object();
    json.set("min", min);
    json.set("max", max);
    return json;
}

Result<LootEntry> LootEntry::fromJson(const Json &json) {
    if (!json.isObject())
        return Error{"an entry must be an object like {\"item\": \"coin\", \"count\": [1, 5], "
                     "\"weight\": 10}"};
    LootEntry entry;
    const int kinds = (json.contains("item") ? 1 : 0) + (json.contains("table") ? 1 : 0) +
                      (json.contains("tag") ? 1 : 0) + (json.get("nothing").asBool(false) ? 1 : 0);
    if (kinds != 1)
        return Error{"an entry names exactly one of 'item', 'table', 'tag' or \"nothing\": true"};
    if (json.contains("item")) {
        entry.kind = Kind::Item;
        entry.item = data::optionalString(json, "item");
    } else if (json.contains("table")) {
        entry.kind = Kind::Table;
        entry.table = data::optionalString(json, "table");
    } else if (json.contains("tag")) {
        entry.kind = Kind::Category;
        entry.tag = data::optionalString(json, "tag");
    } else {
        entry.kind = Kind::Nothing;
    }
    if (entry.kind == Kind::Item && entry.item.empty())
        return Error{"'item' is empty"};
    if (entry.kind == Kind::Table && entry.table.empty())
        return Error{"'table' is empty"};
    if (entry.kind == Kind::Category && entry.tag.empty())
        return Error{"'tag' is empty"};
    auto count = IntRange::fromJson(json.get("count"), "count", 1);
    if (!count)
        return Error{count.error()};
    entry.count = count.value();
    auto weight = data::number(json, "weight", 1.0, 0.0001, 1.0e9);
    if (!weight)
        return Error{weight.error()};
    entry.weight = weight.value();
    entry.unique = json.get("unique").asBool(false);
    return entry;
}

Json LootEntry::toJson() const {
    Json json = Json::object();
    switch (kind) {
    case Kind::Item:
        json.set("item", item);
        break;
    case Kind::Table:
        json.set("table", table);
        break;
    case Kind::Category:
        json.set("tag", tag);
        break;
    case Kind::Nothing:
        json.set("nothing", true);
        break;
    }
    if (!(count == IntRange{}))
        json.set("count", count.toJson());
    if (weight != 1.0)
        json.set("weight", weight);
    if (unique)
        json.set("unique", true);
    return json;
}

namespace {
Result<std::vector<LootEntry>> entriesFromJson(const Json &json, const char *what) {
    std::vector<LootEntry> list;
    if (json.isNull())
        return list;
    if (!json.isArray())
        return Error{std::string("'") + what + "' must be a list"};
    for (std::size_t i = 0; i < json.size(); ++i) {
        auto entry = LootEntry::fromJson(json.at(i));
        if (!entry)
            return Error{std::string(what) + " " + std::to_string(i + 1) + ": " + entry.error()};
        list.push_back(std::move(entry.value()));
    }
    return list;
}
Json entriesToJson(const std::vector<LootEntry> &entries) {
    Json list = Json::array();
    for (const LootEntry &entry : entries)
        list.push(entry.toJson());
    return list;
}
} // namespace

Result<LootTable> LootTable::fromJson(const Json &json, std::vector<std::string> &warnings) {
    if (!json.isObject())
        return Error{"a loot table must be an object"};
    LootTable table;
    auto id = data::requiredString(json, "id");
    if (!id)
        return Error{id.error()};
    table.id = id.value();
    if (!data::validId(table.id))
        return Error{"'" + table.id + "' is not a usable id (letters, digits, '_' and '-')"};
    auto rolls = IntRange::fromJson(json.get("rolls"), "rolls", 0);
    if (!rolls)
        return Error{rolls.error()};
    table.rolls = rolls.value();
    auto entries = entriesFromJson(json.get("entries"), "entry");
    if (!entries)
        return Error{entries.error()};
    table.entries = std::move(entries.value());
    auto guaranteed = entriesFromJson(json.get("guaranteed"), "guaranteed");
    if (!guaranteed)
        return Error{guaranteed.error()};
    table.guaranteed = std::move(guaranteed.value());
    data::warnUnknown(json, {"id", "rolls", "entries", "guaranteed"}, warnings);
    return table;
}

Json LootTable::toJson() const {
    Json json = Json::object();
    json.set("id", id);
    json.set("rolls", rolls.toJson());
    json.set("entries", entriesToJson(entries));
    if (!guaranteed.empty())
        json.set("guaranteed", entriesToJson(guaranteed));
    return json;
}

Result<LootPool> LootPool::fromJson(const Json &json, std::vector<std::string> &warnings) {
    if (!json.isObject())
        return Error{"a loot pool must be an object"};
    LootPool pool;
    auto id = data::requiredString(json, "id");
    if (!id)
        return Error{id.error()};
    pool.id = id.value();
    if (!data::validId(pool.id))
        return Error{"'" + pool.id + "' is not a usable id (letters, digits, '_' and '-')"};
    auto items = entriesFromJson(json.get("items"), "item");
    if (!items)
        return Error{items.error()};
    pool.items = std::move(items.value());
    if (pool.items.empty())
        return Error{"a pool needs 'items': what must be dealt out"};
    for (const LootEntry &entry : pool.items)
        if (entry.kind == LootEntry::Kind::Nothing)
            return Error{"a pool deals real things: \"nothing\" has no place in it"};
    auto group = data::requiredString(json, "group");
    if (!group)
        return Error{group.error() + " (the container group the items are dealt among)"};
    pool.group = group.value();
    auto per = data::number(json, "perContainer", 0.0, 0.0, 1000.0);
    if (!per)
        return Error{per.error()};
    pool.perContainer = static_cast<int>(per.value());
    pool.autoStart = json.get("autoStart").asBool(false);
    data::warnUnknown(json, {"id", "items", "group", "perContainer", "autoStart"}, warnings);
    return pool;
}

Json LootPool::toJson() const {
    Json json = Json::object();
    json.set("id", id);
    json.set("items", entriesToJson(items));
    json.set("group", group);
    if (perContainer != 0)
        json.set("perContainer", perContainer);
    if (autoStart)
        json.set("autoStart", true);
    return json;
}

void LootCatalog::load(const Json &document, const std::string &file,
                       std::vector<DataProblem> &problems) {
    if (document.contains("lootTables"))
        tables.load(document.get("lootTables"), file, problems, "loot table");
    if (document.contains("lootPools"))
        pools.load(document.get("lootPools"), file, problems, "loot pool");
}

void LootCatalog::check(const ItemCatalog &items, std::vector<DataProblem> &problems) const {
    const auto checkEntry = [&](const std::string &file, const std::string &where,
                                const LootEntry &entry) {
        switch (entry.kind) {
        case LootEntry::Kind::Item:
            if (!items.items.contains(entry.item))
                problems.push_back(
                    {file, where + "the item '" + entry.item + "' is not defined", true});
            break;
        case LootEntry::Kind::Table:
            if (!tables.contains(entry.table))
                problems.push_back(
                    {file, where + "the table '" + entry.table + "' is not defined", true});
            break;
        case LootEntry::Kind::Category:
            if (items.withTag(entry.tag).empty())
                problems.push_back(
                    {file, where + "no item has the tag '" + entry.tag + "'", false});
            break;
        case LootEntry::Kind::Nothing:
            break;
        }
    };
    for (const LootTable &table : tables.all()) {
        const std::string where = "loot table '" + table.id + "': ";
        for (const LootEntry &entry : table.entries)
            checkEntry(table.file, where, entry);
        for (const LootEntry &entry : table.guaranteed)
            checkEntry(table.file, where + "guaranteed: ", entry);
        if (table.entries.empty() && table.guaranteed.empty())
            problems.push_back(
                {table.file, where + "has no entries, so it never gives anything", false});
        if (!table.entries.empty() && table.rolls.max == 0)
            problems.push_back(
                {table.file, where + "has entries but never rolls them ('rolls' is 0)", false});
        // A table that contains itself (through others) would never end.
        std::set<std::string> seen;
        const std::function<bool(const LootTable &)> loops = [&](const LootTable &current) {
            for (const auto *list : {&current.entries, &current.guaranteed})
                for (const LootEntry &entry : *list) {
                    if (entry.kind != LootEntry::Kind::Table)
                        continue;
                    if (entry.table == table.id)
                        return true;
                    const LootTable *next = tables.find(entry.table);
                    if (next && seen.insert(next->id).second && loops(*next))
                        return true;
                }
            return false;
        };
        if (loops(table))
            problems.push_back({table.file, where + "contains itself through nested tables", true});
    }
    for (const LootPool &pool : pools.all()) {
        const std::string where = "loot pool '" + pool.id + "': ";
        for (const LootEntry &entry : pool.items) {
            if (entry.kind == LootEntry::Kind::Table)
                problems.push_back(
                    {pool.file, where + "deals items, not tables ('" + entry.table + "')", true});
            else
                checkEntry(pool.file, where, entry);
        }
    }
}

// ---- Rolling
// -------------------------------------------------------------------------------------
namespace {
int draw(Rng &rng, const IntRange &range) {
    return rng.range(range.min, range.max);
}
void merge(std::vector<ItemStack> &stacks, ItemStack stack, const ItemCatalog &items) {
    const ItemDefinition *def = items.items.find(stack.item.str());
    const int limit = def ? def->stackSize : 1;
    for (ItemStack &held : stacks)
        if (held.matches(stack) && held.count < limit) {
            const int moved = std::min(stack.count, limit - held.count);
            held.count += moved;
            stack.count -= moved;
            if (stack.count == 0)
                return;
        }
    while (stack.count > 0) {
        ItemStack part = stack;
        part.count = std::min(stack.count, limit);
        stack.count -= part.count;
        stacks.push_back(std::move(part));
    }
}
} // namespace

void LootRoller::resolve(const LootEntry &entry, std::vector<ItemStack> &out, int depth) {
    switch (entry.kind) {
    case LootEntry::Kind::Nothing:
        return;
    case LootEntry::Kind::Item: {
        ItemStack stack = items_.make(entry.item, draw(rng_, entry.count));
        if (!stack.empty())
            merge(out, std::move(stack), items_);
        return;
    }
    case LootEntry::Kind::Category: {
        const auto candidates = items_.withTag(entry.tag);
        if (candidates.empty())
            return;
        const ItemDefinition *picked = candidates[static_cast<std::size_t>(
            rng_.range(0, static_cast<int>(candidates.size()) - 1))];
        ItemStack stack = items_.make(picked->id, draw(rng_, entry.count));
        if (!stack.empty())
            merge(out, std::move(stack), items_);
        return;
    }
    case LootEntry::Kind::Table:
        if (const LootTable *table = loot_.tables.find(entry.table))
            for (int i = 0, times = draw(rng_, entry.count); i < times; ++i)
                rollInto(*table, out, depth + 1);
        return;
    }
}

void LootRoller::rollInto(const LootTable &table, std::vector<ItemStack> &out, int depth) {
    if (depth > 8)
        return; // Validation reports tables that contain themselves; this only keeps a bad one from
                // hanging.
    for (const LootEntry &entry : table.guaranteed)
        resolve(entry, out, depth);
    std::vector<const LootEntry *> pool;
    for (const LootEntry &entry : table.entries)
        pool.push_back(&entry);
    const int rolls = draw(rng_, table.rolls);
    for (int i = 0; i < rolls && !pool.empty(); ++i) {
        double total = 0.0;
        for (const LootEntry *entry : pool)
            total += entry->weight;
        double pick = rng_.uniform() * total;
        std::size_t chosen = pool.size() - 1;
        for (std::size_t k = 0; k < pool.size(); ++k) {
            pick -= pool[k]->weight;
            if (pick < 0.0) {
                chosen = k;
                break;
            }
        }
        const LootEntry *entry = pool[chosen];
        resolve(*entry, out, depth);
        if (entry->unique)
            pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(chosen));
    }
}

std::vector<ItemStack> LootRoller::roll(std::string_view table) {
    std::vector<ItemStack> out;
    if (const LootTable *found = loot_.tables.find(table))
        rollInto(*found, out, 0);
    return out;
}

std::vector<ItemStack> LootRoller::expand(const LootPool &pool) {
    std::vector<ItemStack> out;
    for (const LootEntry &entry : pool.items) {
        std::vector<ItemStack> stacks;
        resolve(entry, stacks, 0);
        for (ItemStack &stack : stacks)
            out.push_back(std::move(stack));
    }
    return out;
}

// ---- The service
// -----------------------------------------------------------------------------------
void LootService::onStart(GameContext &context) {
    subscription_ =
        context.events().subscribe("scene_started", [this, &context](const GameEvent &) {
            for (const LootPool &pool : gameData(context).loot.pools.all())
                if (pool.autoStart)
                    scatter(context, pool.id);
        });
}

void LootService::onShutdown(GameContext &context) {
    if (subscription_ != 0)
        context.events().unsubscribe(subscription_);
    subscription_ = 0;
}

void LootService::fill(GameContext &context, Entity &container) {
    auto *box = container.get<Container>();
    auto *inventory = container.get<Inventory>();
    if (!box || !inventory || box->lootTable.empty())
        return;
    const GameData &data = gameData(context);
    if (!data.loot.tables.contains(box->lootTable)) {
        log(LogLevel::Warning, "loot",
            "'" + container.name() + "' names the loot table '" + box->lootTable +
                "', which is not defined");
        return;
    }
    Rng local(static_cast<std::uint64_t>(box->lootSeed) * 0x9E3779B97F4A7C15ULL + 0x51ED);
    Rng &rng = box->lootSeed != 0 ? local : context.services().get<RandomService>().rng;
    LootRoller roller(data.items, data.loot, rng);
    for (ItemStack &stack : roller.roll(box->lootTable)) {
        if (stack.owner.empty())
            stack.owner = box->owner;
        if (inventory->add(context, stack) > 0)
            log(LogLevel::Warning, "loot",
                "'" + container.name() + "' has no room for all of its loot");
    }
}

std::vector<LootService::Placement> LootService::scatter(GameContext &context,
                                                         const std::string &poolId) {
    std::vector<Placement> result;
    const GameData &data = gameData(context);
    const LootPool *pool = data.loot.pools.find(poolId);
    if (!pool) {
        log(LogLevel::Warning, "loot", "the loot pool '" + poolId + "' is not defined");
        return result;
    }
    std::vector<Entity *> candidates;
    context.scene().forEach([&](Entity &entity) {
        const auto *box = entity.get<Container>();
        if (box && box->group == pool->group && entity.get<Inventory>() &&
            entity.activeInHierarchy())
            candidates.push_back(&entity);
    });
    Rng &rng = context.services().get<RandomService>().rng;
    LootRoller roller(data.items, data.loot, rng);
    std::map<EntityId, int> dealt; // Stacks of this pool each container has been given.
    for (ItemStack &stack : roller.expand(*pool)) {
        // Containers with room, and (when the pool limits it) not yet given their share.
        std::vector<Entity *> eligible;
        for (Entity *entity : candidates)
            if ((pool->perContainer == 0 || dealt[entity->id()] < pool->perContainer) &&
                entity->get<Inventory>()->canAdd(context, stack) >= stack.count)
                eligible.push_back(entity);
        Placement placement{pool->id, stack, {}};
        if (eligible.empty()) {
            log(LogLevel::Warning, "loot",
                "the pool '" + pool->id + "' found no container of the group '" + pool->group +
                    "' with room for " + stack.item.str());
            Json info = Json::object();
            info.set("pool", pool->id);
            info.set("item", stack.item.str());
            context.events().emit(GameEvent("loot.failed", {}, {}, std::move(info)));
        } else {
            Entity *chosen = eligible[static_cast<std::size_t>(
                rng.range(0, static_cast<int>(eligible.size()) - 1))];
            if (stack.owner.empty())
                stack.owner = chosen->get<Container>()->owner;
            chosen->get<Inventory>()->add(context, stack);
            ++dealt[chosen->id()];
            placement.stack = stack;
            placement.container = chosen->id();
        }
        result.push_back(std::move(placement));
    }
    return result;
}

void registerLootRules(RuleCatalog &catalog) {
    using Kind = ParamSpec::Kind;
    catalog.addAction(
        {"ScatterLoot",
         "Items",
         "Deals a loot pool among the containers of its group (the scenario's must-exist items).",
         {ParamSpec::make("pool", Kind::Ref, true, "", "loot pool")},
         [](const Json &args, RuleContext &context) {
             const auto placed = context.game.services().get<LootService>().scatter(
                 context.game, args.get("pool").asString());
             const bool all = !placed.empty() && std::all_of(placed.begin(), placed.end(),
                                                             [](const LootService::Placement &p) {
                                                                 return p.container;
                                                             });
             return all ? ActionResult::Done : ActionResult::Failed;
         },
         nullptr});
    catalog.addAction(
        {"FillContainer",
         "Items",
         "Rolls a container's loot table into it (once); fails when it has none or already did.",
         {ParamSpec::make("entity", Kind::Entity, false, "default self")},
         [](const Json &args, RuleContext &context) {
             bool done = false;
             for (Entity *entity : context.entitiesFrom(args, "entity", "self"))
                 if (auto *box = entity->get<Container>())
                     done = box->generateLoot(context.game) || done;
             return done ? ActionResult::Done : ActionResult::Failed;
         },
         nullptr});
}
} // namespace yk
