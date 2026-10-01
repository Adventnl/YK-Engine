#include "yk/items/Inventory.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/world/SpatialIndex.hpp"
#include "yk/world/WorldLevels.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
namespace {
const ItemDefinition *definitionOf(GameContext &context, const ItemId &item) {
    return gameData(context).items.items.find(item.str());
}
} // namespace

void Inventory::describe(TypeBuilder<Inventory> &type) {
    type.category("Items")
        .description(
            "Slots that hold stacks of items, and named equipment slots (Outfit, Weapon...) "
            "for what is worn or wielded. Characters, desks, lockers and shops all use it. "
            "Items are defined in the project's item files.")
        .updatePhase(UpdatePhase::PostSimulation);
    type.field("slots", &Inventory::slots).range(0, 500, 1).tooltip("How many stacks it holds.");
    type.field("equipmentSlots", &Inventory::equipmentSlots)
        .tooltip("Names of the places an item can be worn or wielded: Outfit, Weapon, Tool...");
    type.field("owner", &Inventory::owner).tooltip("Whose it is (a character's persistent id).");
    type.field("startItems", &Inventory::startItems)
        .tooltip("What it holds at the start: [{\"item\": \"screwdriver\", \"count\": 1, "
                 "\"equipped\": false}].");
    type.field("collectRadius", &Inventory::collectRadius)
        .range(0, 20, 0.1)
        .tooltip("Above 0: picks up items lying within this distance by itself.");
    type.check([](const Entity &entity, const Inventory &inventory, const CheckContext &context,
                  std::vector<std::string> &problems) {
        for (std::size_t i = 0; i < inventory.equipmentSlots.size(); ++i) {
            if (inventory.equipmentSlots[i].empty())
                problems.push_back("an equipment slot has no name");
            for (std::size_t k = 0; k < i; ++k)
                if (inventory.equipmentSlots[k] == inventory.equipmentSlots[i])
                    problems.push_back("two equipment slots are called '" +
                                       inventory.equipmentSlots[i] + "'");
        }
        if (!inventory.equipmentSlots.empty() && !entity.get<StatusEffects>())
            problems.push_back(
                "has equipment slots but no StatusEffects, so what is worn changes nothing");
        if (!inventory.startItems.isArray()) {
            problems.push_back(
                "'startItems' must be a list like [{\"item\": \"screwdriver\", \"count\": 1}]");
            return;
        }
        int total = 0;
        for (std::size_t i = 0; i < inventory.startItems.size(); ++i) {
            auto stack = ItemStack::fromJson(inventory.startItems.at(i));
            if (!stack) {
                problems.push_back("starting item " + std::to_string(i + 1) + ": " + stack.error());
                continue;
            }
            total += inventory.startItems.at(i).get("equipped").asBool(false) ? 0 : 1;
            if (context.known && !context.known("item", stack.value().item.str()))
                problems.push_back("starts with the item '" + stack.value().item.str() +
                                   "', which is not defined");
        }
        if (total > inventory.slots)
            problems.push_back("starts with more stacks than it has slots");
    });
}

const ItemStack &Inventory::slot(int index) const {
    return index >= 0 && index < size() ? slots_[static_cast<std::size_t>(index)] : none_;
}

const ItemStack *Inventory::equipped(std::string_view equipmentSlot) const {
    for (const auto &[name, stack] : worn_)
        if (name == equipmentSlot)
            return stack.empty() ? nullptr : &stack;
    return nullptr;
}

int Inventory::count(const ItemId &item) const {
    int total = 0;
    for (const ItemStack &stack : slots_)
        if (stack.item == item)
            total += stack.count;
    for (const auto &[name, stack] : worn_) {
        (void)name;
        if (stack.item == item)
            total += stack.count;
    }
    return total;
}

int Inventory::countWithTag(GameContext &context, std::string_view tag) const {
    int total = 0;
    const auto add = [&](const ItemStack &stack) {
        if (stack.empty())
            return;
        const ItemDefinition *def = definitionOf(context, stack.item);
        if (def && def->hasTag(tag))
            total += stack.count;
    };
    for (const ItemStack &stack : slots_)
        add(stack);
    for (const auto &[name, stack] : worn_) {
        (void)name;
        add(stack);
    }
    return total;
}

int Inventory::bestDurability(const ItemId &item) const {
    int best = -1;
    for (const ItemStack &stack : slots_)
        if (stack.item == item)
            best = std::max(best, stack.durability);
    for (const auto &[name, stack] : worn_) {
        (void)name;
        if (stack.item == item)
            best = std::max(best, stack.durability);
    }
    return best;
}

int Inventory::freeSlots() const {
    return static_cast<int>(std::count_if(slots_.begin(), slots_.end(),
                                          [](const ItemStack &stack) { return stack.empty(); }));
}

int Inventory::firstFree() const {
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].empty())
            return static_cast<int>(i);
    return -1;
}

int Inventory::stackLimit(GameContext &context, const ItemId &item) const {
    const ItemDefinition *def = definitionOf(context, item);
    return def ? def->stackSize : 1;
}

int Inventory::canAdd(GameContext &context, const ItemStack &stack) const {
    if (stack.empty())
        return 0;
    const int limit = stackLimit(context, stack.item);
    int room = 0;
    for (const ItemStack &held : slots_) {
        if (held.empty())
            room += limit;
        else if (held.matches(stack) && held.count < limit)
            room += limit - held.count;
    }
    return std::min(room, stack.count);
}

void Inventory::changed(GameContext &context) {
    ++revision_;
    context.events().emit(GameEvent("inventory.changed", entity().id()));
}

void Inventory::announce(GameContext &context, const char *event, const ItemStack &stack,
                         const std::string &slotName) {
    Json data = Json::object();
    data.set("item", stack.item.str());
    data.set("count", stack.count);
    if (!slotName.empty())
        data.set("slot", slotName);
    context.events().emit(GameEvent(event, entity().id(), {}, std::move(data)));
}

// ---- Starting, saving
// -----------------------------------------------------------------------------
void Inventory::onStart(GameContext &context) {
    slots_.assign(static_cast<std::size_t>(std::max(slots, 0)), ItemStack{});
    worn_.clear();
    for (const std::string &name : equipmentSlots)
        worn_.push_back({name, ItemStack{}});
    readyAt_.clear();
    started_ = true;
    const ItemCatalog &catalog = gameData(context).items;
    if (!startItems.isArray()) {
        if (!startItems.isNull())
            log(LogLevel::Warning, "items",
                "'" + entity().name() + "': 'startItems' must be a list, so it starts empty");
        return;
    }
    for (std::size_t i = 0; i < startItems.size(); ++i) {
        auto parsed = ItemStack::fromJson(startItems.at(i));
        if (!parsed) {
            log(LogLevel::Warning, "items",
                "'" + entity().name() + "': starting item " + std::to_string(i + 1) + ": " +
                    parsed.error());
            continue;
        }
        ItemStack stack = parsed.value();
        if (!catalog.items.contains(stack.item.str())) {
            log(LogLevel::Warning, "items",
                "'" + entity().name() + "' starts with the item '" + stack.item.str() +
                    "', which is not defined");
            continue;
        }
        if (stack.durability < 0)
            stack.durability = catalog.make(stack.item.str()).durability;
        if (const int left = add(context, stack); left > 0)
            log(LogLevel::Warning, "items",
                "'" + entity().name() + "' has no room for its starting " + stack.item.str());
        else if (startItems.at(i).get("equipped").asBool(false)) {
            for (int index = 0; index < size(); ++index)
                if (slots_[static_cast<std::size_t>(index)].item == stack.item) {
                    if (!equip(context, index))
                        log(LogLevel::Warning, "items",
                            "'" + entity().name() + "' cannot wear its starting " +
                                stack.item.str());
                    break;
                }
        }
    }
}

void Inventory::onDestroy(GameContext &) {
    started_ = false;
}

void Inventory::onFixedUpdate(GameContext &context, float) {
    if (collectRadius <= 0.0F)
        return;
    if (const auto *health = entity().get<Health>(); health && !health->active())
        return;
    if (const auto *effects = entity().get<StatusEffects>();
        effects && (effects->hasFlag("no_pickup") || effects->hasFlag("no_act")))
        return;
    auto *spatial = context.services().find<SpatialIndexService>();
    if (!spatial || spatial->count("pickup") == 0)
        return;
    const int level = levelOf(entity());
    for (const SpatialHash::Hit &hit :
         spatial->near("pickup", entity().worldPosition(), collectRadius, level)) {
        Entity *found = context.scene().find(hit.id);
        auto *pickup = found ? found->get<Pickup>() : nullptr;
        if (pickup && pickup->autoCollect)
            pickup->collect(context, entity());
        if (freeSlots() == 0)
            break;
    }
}

Json Inventory::saveState() const {
    Json state = Json::object();
    Json list = Json::array();
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (!slots_[i].empty()) {
            Json item = slots_[i].toJson();
            item.set("slot", static_cast<int>(i));
            list.push(item);
        }
    state.set("slots", list);
    Json worn = Json::object();
    for (const auto &[name, stack] : worn_)
        if (!stack.empty())
            worn.set(name, stack.toJson());
    state.set("worn", worn);
    Json ready = Json::object();
    for (const auto &[item, at] : readyAt_)
        ready.set(item, at);
    state.set("ready", ready);
    return state;
}

Status Inventory::loadState(GameContext &context, const Json &state) {
    const ItemCatalog &catalog = gameData(context).items;
    slots_.assign(static_cast<std::size_t>(std::max(slots, 0)), ItemStack{});
    for (auto &[name, stack] : worn_)
        stack = {};
    readyAt_.clear();
    const Json &list = state.get("slots");
    for (std::size_t i = 0; i < list.size(); ++i) {
        auto stack = ItemStack::fromJson(list.at(i));
        if (!stack)
            return Error{"inventory slot " + std::to_string(i + 1) + ": " + stack.error()};
        if (!catalog.items.contains(stack.value().item.str())) {
            log(LogLevel::Warning, "items",
                "a saved '" + stack.value().item.str() + "' no longer exists and was dropped");
            continue;
        }
        const auto index = static_cast<std::size_t>(list.at(i).get("slot").asInt(-1));
        if (index >= slots_.size())
            return Error{"inventory slot " + std::to_string(i + 1) + " is outside the " +
                         std::to_string(slots_.size()) + " slots"};
        slots_[index] = std::move(stack.value());
    }
    const Json &worn = state.get("worn");
    for (std::size_t i = 0; i < worn.size(); ++i) {
        auto stack = ItemStack::fromJson(worn.valueAt(i));
        if (!stack)
            return Error{"worn item '" + worn.keyAt(i) + "': " + stack.error()};
        if (!catalog.items.contains(stack.value().item.str()))
            continue;
        for (auto &[name, held] : worn_)
            if (name == worn.keyAt(i))
                held = std::move(stack.value());
    }
    const Json &ready = state.get("ready");
    for (std::size_t i = 0; i < ready.size(); ++i)
        readyAt_[ready.keyAt(i)] = ready.valueAt(i).asNumber();
    ++revision_;
    return success();
}

// ---- Changing
// ---------------------------------------------------------------------------------------
int Inventory::add(GameContext &context, ItemStack stack) {
    if (stack.empty())
        return 0;
    const int limit = stackLimit(context, stack.item);
    int left = stack.count;
    for (ItemStack &held : slots_) {
        if (left == 0)
            break;
        if (!held.empty() && held.matches(stack) && held.count < limit) {
            const int moved = std::min(left, limit - held.count);
            held.count += moved;
            left -= moved;
        }
    }
    for (ItemStack &held : slots_) {
        if (left == 0)
            break;
        if (held.empty()) {
            held = stack;
            held.count = std::min(left, limit);
            left -= held.count;
        }
    }
    const int added = stack.count - left;
    if (added > 0) {
        ItemStack report = stack;
        report.count = added;
        announce(context, "item.added", report);
        changed(context);
    }
    return left;
}

int Inventory::addItem(GameContext &context, std::string_view item, int count) {
    ItemStack stack = gameData(context).items.make(item, count);
    if (stack.empty())
        return count;
    return add(context, std::move(stack));
}

int Inventory::remove(GameContext &context, const ItemId &item, int count) {
    int removed = 0;
    for (ItemStack &held : slots_) {
        if (removed >= count)
            break;
        if (held.item == item && !held.empty()) {
            const int taken = std::min(count - removed, held.count);
            held.count -= taken;
            removed += taken;
            if (held.count == 0)
                held = {};
        }
    }
    for (auto &[name, held] : worn_) {
        if (removed >= count)
            break;
        if (held.item == item && !held.empty()) {
            applyEquipment(context, held, false);
            announce(context, "item.unequipped", held, name);
            removed += held.count;
            held = {};
        }
    }
    if (removed > 0) {
        ItemStack report;
        report.item = item;
        report.count = removed;
        announce(context, "item.removed", report);
        changed(context);
    }
    return removed;
}

ItemStack Inventory::take(GameContext &context, int slotIndex, int count) {
    if (slotIndex < 0 || slotIndex >= size() || count <= 0)
        return {};
    ItemStack &held = slots_[static_cast<std::size_t>(slotIndex)];
    if (held.empty())
        return {};
    ItemStack taken = held;
    taken.count = std::min(count, held.count);
    held.count -= taken.count;
    if (held.count == 0)
        held = {};
    announce(context, "item.removed", taken);
    changed(context);
    return taken;
}

bool Inventory::put(GameContext &context, int slotIndex, ItemStack &stack) {
    if (slotIndex < 0 || slotIndex >= size() || stack.empty())
        return false;
    ItemStack &held = slots_[static_cast<std::size_t>(slotIndex)];
    const int limit = stackLimit(context, stack.item);
    if (held.empty()) {
        held = stack;
        held.count = std::min(stack.count, limit);
        stack.count -= held.count;
    } else if (held.matches(stack) && held.count < limit) {
        const int moved = std::min(stack.count, limit - held.count);
        held.count += moved;
        stack.count -= moved;
    } else {
        return false;
    }
    if (stack.count <= 0)
        stack = {};
    ItemStack report = held;
    announce(context, "item.added", report);
    changed(context);
    return true;
}

bool Inventory::move(GameContext &context, int from, int to) {
    if (from == to || from < 0 || to < 0 || from >= size() || to >= size())
        return false;
    ItemStack &source = slots_[static_cast<std::size_t>(from)];
    ItemStack &target = slots_[static_cast<std::size_t>(to)];
    if (source.empty())
        return false;
    const int limit = stackLimit(context, source.item);
    if (!target.empty() && target.matches(source) && target.count < limit) {
        const int moved = std::min(source.count, limit - target.count);
        target.count += moved;
        source.count -= moved;
        if (source.count == 0)
            source = {};
    } else {
        std::swap(source, target);
    }
    changed(context);
    return true;
}

bool Inventory::split(GameContext &context, int slotIndex, int count) {
    if (slotIndex < 0 || slotIndex >= size())
        return false;
    ItemStack &held = slots_[static_cast<std::size_t>(slotIndex)];
    const int free = firstFree();
    if (held.empty() || count <= 0 || count >= held.count || free < 0)
        return false;
    ItemStack part = held;
    part.count = count;
    held.count -= count;
    slots_[static_cast<std::size_t>(free)] = part;
    changed(context);
    return true;
}

void Inventory::applyEquipment(GameContext &context, const ItemStack &stack, bool on) {
    const ItemDefinition *def = definitionOf(context, stack.item);
    auto *effects = entity().get<StatusEffects>();
    if (!def || !def->equip || !effects)
        return;
    const auto each = [&](const std::string &id) {
        if (on)
            effects->apply(context, id, 0.0, entity().id());
        else
            effects->remove(context, id);
    };
    each(equipEffectId(def->id));
    for (const std::string &id : def->equip->effects)
        each(id);
}

bool Inventory::equip(GameContext &context, int slotIndex) {
    if (slotIndex < 0 || slotIndex >= size() || slots_[static_cast<std::size_t>(slotIndex)].empty())
        return false;
    const ItemStack &candidate = slots_[static_cast<std::size_t>(slotIndex)];
    const ItemDefinition *def = definitionOf(context, candidate.item);
    if (!def || !def->equip)
        return false;
    const auto place = std::find_if(worn_.begin(), worn_.end(), [&](const auto &entry) {
        return entry.first == def->equip->slot;
    });
    if (place == worn_.end())
        return false;
    const std::vector<ItemStack> savedSlots = slots_;
    const ItemStack savedWorn = place->second;
    ItemStack one = candidate;
    one.count = 1;
    ItemStack &held = slots_[static_cast<std::size_t>(slotIndex)];
    held.count -= 1;
    if (held.count == 0)
        held = {};
    const ItemStack previous = place->second;
    if (!previous.empty()) {
        ItemStack back = previous;
        const int limit = stackLimit(context, back.item);
        bool placed = false;
        if (held.empty()) {
            held = back;
            placed = true;
        } else {
            for (ItemStack &other : slots_)
                if (!other.empty() && other.matches(back) && other.count < limit) {
                    other.count += back.count;
                    placed = true;
                    break;
                }
            if (!placed)
                if (const int free = firstFree(); free >= 0) {
                    slots_[static_cast<std::size_t>(free)] = back;
                    placed = true;
                }
        }
        if (!placed) {
            slots_ = savedSlots;
            place->second = savedWorn;
            return false;
        }
        applyEquipment(context, previous, false);
        announce(context, "item.unequipped", previous, place->first);
    }
    place->second = one;
    applyEquipment(context, one, true);
    announce(context, "item.equipped", one, place->first);
    changed(context);
    return true;
}

bool Inventory::unequip(GameContext &context, std::string_view equipmentSlot) {
    const auto place = std::find_if(worn_.begin(), worn_.end(), [&](const auto &entry) {
        return entry.first == equipmentSlot;
    });
    if (place == worn_.end() || place->second.empty())
        return false;
    const ItemStack worn = place->second;
    if (canAdd(context, worn) < worn.count)
        return false;
    const std::string slotName = place->first;
    place->second = {};
    applyEquipment(context, worn, false);
    // Back into the slots without announcing a second arrival: it was carried all along.
    ItemStack rest = worn;
    const int limit = stackLimit(context, rest.item);
    for (ItemStack &held : slots_) {
        if (rest.count == 0)
            break;
        if (!held.empty() && held.matches(rest) && held.count < limit) {
            const int moved = std::min(rest.count, limit - held.count);
            held.count += moved;
            rest.count -= moved;
        }
    }
    for (ItemStack &held : slots_) {
        if (rest.count == 0)
            break;
        if (held.empty()) {
            held = rest;
            rest.count = 0;
        }
    }
    announce(context, "item.unequipped", worn, slotName);
    changed(context);
    return true;
}

bool Inventory::use(GameContext &context, int slotIndex, EntityId target) {
    if (slotIndex < 0 || slotIndex >= size() || slots_[static_cast<std::size_t>(slotIndex)].empty())
        return false;
    const ItemStack stack = slots_[static_cast<std::size_t>(slotIndex)];
    const ItemDefinition *def = definitionOf(context, stack.item);
    if (!def || !def->use)
        return false;
    const double now = context.time();
    if (const auto ready = readyAt_.find(def->id); ready != readyAt_.end() && ready->second > now)
        return false;
    RuleContext rules(context);
    rules.self = entity().id();
    rules.actor = entity().id();
    rules.target = target;
    rules.origin = "item '" + def->id + "' used by '" + entity().name() + "'";
    if (!evaluate(def->use->condition, rules))
        return false;
    execute(def->use->actions, rules);
    if (def->use->cooldown > 0.0)
        readyAt_[def->id] = now + def->use->cooldown;
    announce(context, "item.used", stack);
    // The actions may have changed what is in the slot; use up what is there now.
    ItemStack &held = slots_[static_cast<std::size_t>(slotIndex)];
    if (def->use->durabilityCost > 0 && !held.empty() && held.item == stack.item) {
        Location where;
        where.slot = slotIndex;
        wear(context, where, def->use->durabilityCost);
    }
    if (def->use->consume && !held.empty() && held.item == stack.item) {
        held.count -= 1;
        if (held.count == 0)
            held = {};
        ItemStack gone = stack;
        gone.count = 1;
        announce(context, "item.removed", gone);
    }
    changed(context);
    return true;
}

void Inventory::clear(GameContext &context) {
    for (auto &[name, held] : worn_)
        if (!held.empty()) {
            applyEquipment(context, held, false);
            held = {};
        }
    for (ItemStack &held : slots_)
        held = {};
    changed(context);
}

ItemStack *Inventory::stackAt(const Location &where) {
    if (where.slot >= 0 && where.slot < size())
        return &slots_[static_cast<std::size_t>(where.slot)];
    for (auto &[name, held] : worn_)
        if (!where.equipSlot.empty() && name == where.equipSlot)
            return &held;
    return nullptr;
}

std::optional<Inventory::Tool> Inventory::findTool(GameContext &context,
                                                   std::string_view action) const {
    std::optional<Tool> best;
    const auto consider = [&](const ItemStack &stack, Location where) {
        if (stack.empty() || stack.durability == 0)
            return;
        const ItemDefinition *def = definitionOf(context, stack.item);
        const double efficiency = def ? def->toolEfficiency(action) : 0.0;
        if (efficiency > 0.0 && (!best || efficiency > best->efficiency))
            best = Tool{std::move(where), stack.item, efficiency};
    };
    for (const auto &[name, stack] : worn_) {
        Location where;
        where.equipSlot = name;
        consider(stack, where);
    }
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        Location where;
        where.slot = static_cast<int>(i);
        consider(slots_[i], where);
    }
    return best;
}

bool Inventory::wear(GameContext &context, const Location &where, int amount) {
    ItemStack *held = stackAt(where);
    if (!held || held->empty() || held->durability < 0 || amount <= 0)
        return false;
    held->durability = std::max(0, held->durability - amount);
    if (held->durability > 0) {
        changed(context);
        return false;
    }
    ItemStack broken = *held;
    if (!where.equipSlot.empty())
        applyEquipment(context, broken, false);
    *held = {};
    announce(context, "item.broke", broken, where.equipSlot);
    changed(context);
    return true;
}

bool Inventory::drop(GameContext &context, int slotIndex, int count, Vec2 at) {
    ItemStack stack = take(context, slotIndex, count);
    if (stack.empty())
        return false;
    if (!Pickup::place(context, stack, at, &entity())) {
        add(context, stack);
        return false;
    }
    Json data = Json::object();
    data.set("item", stack.item.str());
    data.set("count", stack.count);
    context.events().emit(GameEvent("item.dropped", entity().id(), {}, std::move(data)));
    return true;
}

bool holdsToken(GameContext &context, const Entity &who, std::string_view token) {
    if (const auto *effects = who.get<StatusEffects>(); effects && effects->grants(token))
        return true;
    const auto *inventory = who.get<Inventory>();
    if (!inventory)
        return false;
    const ItemCatalog &catalog = gameData(context).items;
    const auto holds = [&](const ItemStack &stack) {
        if (stack.empty())
            return false;
        const ItemDefinition *def = catalog.items.find(stack.item.str());
        return def && data::has(def->grants, token);
    };
    for (int i = 0; i < inventory->size(); ++i)
        if (holds(inventory->slot(i)))
            return true;
    for (const std::string &name : inventory->equipmentSlots)
        if (const ItemStack *worn = inventory->equipped(name); worn && holds(*worn))
            return true;
    return false;
}
} // namespace yk
