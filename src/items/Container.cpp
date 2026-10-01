#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/items/Inventory.hpp"
#include "yk/items/Loot.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameContext.hpp"
#include <algorithm>

namespace yk {
void Container::describe(TypeBuilder<Container> &type) {
    type.category("Items")
        .description("A locker, desk, chest or body: it holds an Inventory (on the same entity) "
                     "and says who may open it, how long searching takes and which slots stay "
                     "hidden until it is searched.")
        .dependsOn("Inventory");
    type.field("owner", &Container::owner)
        .tooltip(
            "Whose it is: a character's persistent id or a faction id. Taking from it is theft "
            "for the rules that care.");
    type.field("group", &Container::group)
        .tooltip("The kind of container it is, for loot pools that deal items among a group "
                 "(\"desks\", \"lockers\").");
    type.field("lootTable", &Container::lootTable)
        .ref("loot")
        .tooltip("A loot table rolled into it when the game starts.");
    type.field("lootSeed", &Container::lootSeed)
        .range(0, 1000000000, 1)
        .tooltip("Not 0: the same table gives this container the same loot in every game.");
    type.field("locked", &Container::locked);
    type.field("unlockToken", &Container::unlockToken)
        .tooltip("The permission token that opens it while locked: a key item's \"grants\".");
    type.field("openRequires", &Container::openRequires)
        .tooltip("A condition that must hold for whoever opens it, e.g. "
                 "{\"type\": \"HasItem\", \"item\": \"warden_pass\"}. Empty: anyone.");
    type.field("searchSeconds", &Container::searchSeconds).range(0, 120, 0.5);
    type.field("hiddenSlots", &Container::hiddenSlots)
        .range(0, 100, 1)
        .tooltip("The last this-many slots are not shown until the container has been searched.");
    type.field("searched", &Container::searched_).readOnly();
    type.check([](const Entity &entity, const Container &container, const CheckContext &context,
                  std::vector<std::string> &problems) {
        const auto *inventory = entity.get<Inventory>();
        if (inventory && container.hiddenSlots > inventory->slots)
            problems.push_back("hides more slots than its inventory has");
        if (container.locked && container.unlockToken.empty() && container.openRequires.isNull())
            problems.push_back(
                "is locked but names no token or condition that opens it, so only a rule can");
        if (!container.lootTable.empty() && context.known &&
            !context.known("loot", container.lootTable))
            problems.push_back("its loot table '" + container.lootTable + "' is not defined");
        if (container.openRequires.isNull())
            return;
        auto condition = Condition::fromJson(container.openRequires);
        ComponentRuleReport report(problems, context);
        if (!condition) {
            report.error("'openRequires' is not valid: " + condition.error());
            return;
        }
        if (const RuleCatalog *catalog = entity.scene().registry().extension<RuleCatalog>())
            catalog->check(condition.value(), report);
    });
}

void Container::onStart(GameContext &context) {
    openBy_.clear();
    context.services().get<LootService>(); // Also deals the pools when the scene starts.
    generateLoot(context);
}

bool Container::generateLoot(GameContext &context) {
    if (generated_ || lootTable.empty())
        return false;
    generated_ = true;
    context.services().get<LootService>().fill(context, entity());
    return true;
}
void Container::onDestroy(GameContext &) {
    openBy_.clear();
}

bool Container::canOpen(GameContext &context, const Entity &who, std::string *whyNot) const {
    const auto refuse = [&](const char *reason) {
        if (whyNot)
            *whyNot = reason;
        return false;
    };
    if (locked && (unlockToken.empty() || !holdsToken(context, who, unlockToken)))
        return refuse("It is locked.");
    if (!openRequires.isNull()) {
        auto condition = Condition::fromJson(openRequires);
        if (!condition)
            return refuse("It cannot be opened.");
        RuleContext rules(context);
        rules.self = entity().id();
        rules.actor = who.id();
        rules.target = entity().id();
        rules.origin = "container '" + entity().name() + "'";
        if (!evaluate(condition.value(), rules))
            return refuse("You are not allowed to open that.");
    }
    return true;
}

bool Container::openBy(EntityId who) const {
    return std::find(openBy_.begin(), openBy_.end(), who) != openBy_.end();
}

bool Container::open(GameContext &context, Entity &who) {
    if (!entity().get<Inventory>() || !canOpen(context, who))
        return false;
    if (!openBy(who.id()))
        openBy_.push_back(who.id());
    context.events().emit(GameEvent("container.opened", entity().id(), who.id()));
    return true;
}

void Container::close(GameContext &context, Entity &who) {
    const auto found = std::find(openBy_.begin(), openBy_.end(), who.id());
    if (found == openBy_.end())
        return;
    openBy_.erase(found);
    context.events().emit(GameEvent("container.closed", entity().id(), who.id()));
}

void Container::setLocked(GameContext &context, bool value) {
    if (locked == value)
        return;
    locked = value;
    context.events().emit(
        GameEvent(value ? "container.locked" : "container.unlocked", entity().id()));
}

void Container::search(GameContext &context, Entity &who) {
    searched_ = true;
    context.events().emit(GameEvent("container.searched", entity().id(), who.id()));
}

int Container::visibleSlots() const {
    const auto *inventory = entity().get<Inventory>();
    if (!inventory)
        return 0;
    return searched_ ? inventory->size() : std::max(0, inventory->size() - hiddenSlots);
}

int Container::take(GameContext &context, Entity &who, int slotIndex) {
    auto *from = entity().get<Inventory>();
    auto *to = who.get<Inventory>();
    if (!from || !to || !openBy(who.id()) || slotIndex < 0 || slotIndex >= visibleSlots())
        return 0;
    ItemStack moving = from->take(context, slotIndex, 1 << 20);
    if (moving.empty())
        return 0;
    const int total = moving.count;
    ItemStack kept = moving;
    const int left = to->add(context, moving);
    if (left > 0) { // No room for all of it: the rest goes back where it was.
        kept.count = left;
        from->put(context, slotIndex, kept);
    }
    const int moved = total - left;
    if (moved > 0) {
        Json data = Json::object();
        data.set("item", moving.item.str());
        data.set("count", moved);
        data.set("owner", !moving.owner.empty() ? moving.owner : owner);
        context.events().emit(
            GameEvent("container.taken", entity().id(), who.id(), std::move(data)));
    }
    return moved;
}

int Container::put(GameContext &context, Entity &who, int whoSlot) {
    auto *to = entity().get<Inventory>();
    auto *from = who.get<Inventory>();
    if (!from || !to || !openBy(who.id()))
        return 0;
    ItemStack moving = from->take(context, whoSlot, 1 << 20);
    if (moving.empty())
        return 0;
    const int total = moving.count;
    ItemStack kept = moving;
    const int left = to->add(context, moving);
    if (left > 0) {
        kept.count = left;
        from->put(context, whoSlot, kept);
    }
    const int moved = total - left;
    if (moved > 0) {
        Json data = Json::object();
        data.set("item", moving.item.str());
        data.set("count", moved);
        context.events().emit(
            GameEvent("container.stored", entity().id(), who.id(), std::move(data)));
    }
    return moved;
}

Json Container::saveState() const {
    Json state = Json::object();
    state.set("locked", locked);
    state.set("searched", searched_);
    state.set("generated", generated_);
    return state;
}
Status Container::loadState(GameContext &, const Json &state) {
    locked = state.get("locked").asBool(locked);
    searched_ = state.get("searched").asBool(false);
    generated_ = state.get("generated").asBool(generated_);
    return success();
}
} // namespace yk
