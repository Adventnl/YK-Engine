#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/items/Inventory.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/world/SpatialIndex.hpp"
#include "yk/world/WorldLevels.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
void Pickup::describe(TypeBuilder<Pickup> &type) {
    type.category("Items")
        .description("An item lying in the world. Characters with an Inventory that has a collect "
                     "radius pick it up by walking close; others by an interaction that collects "
                     "it. It must be on the same level. Dropped items merge with matching ones.")
        .updatePhase(UpdatePhase::PostSimulation);
    type.field("item", &Pickup::item).ref("item").tooltip("What it is.");
    type.field("count", &Pickup::count).range(1, 9999, 1);
    type.field("autoCollect", &Pickup::autoCollect)
        .tooltip("Picked up by anyone who walks close. Off: only by an interaction.");
    type.field("lifetime", &Pickup::lifetime)
        .range(0, 86400, 1)
        .tooltip("Seconds it stays on the ground; 0 forever.");
    type.field("bob", &Pickup::bob).tooltip("Hovers up and down to catch the eye.");
    type.check([](const Entity &, const Pickup &pickup, const CheckContext &context,
                  std::vector<std::string> &problems) {
        if (pickup.item.empty())
            problems.push_back("names no item");
        else if (context.known && !context.known("item", pickup.item))
            problems.push_back("names the item '" + pickup.item + "', which is not defined");
    });
}

void Pickup::onStart(GameContext &context) {
    if (!placed_) {
        stack_ = gameData(context).items.make(item, count);
        if (stack_.empty() && !item.empty())
            log(LogLevel::Warning, "items",
                "'" + entity().name() + "' is a pickup of the item '" + item +
                    "', which is not defined");
    }
    placed_ = true;
    age_ = 0.0;
    if (const auto *sprite = entity().get<SpriteRenderer>())
        baseY_ = sprite->offset.y;
    context.services().get<SpatialIndexService>().track(entity(), "pickup", 0.0F, false);
}

void Pickup::onDestroy(GameContext &context) {
    if (auto *spatial = context.services().find<SpatialIndexService>())
        spatial->untrack(entity().id(), "pickup");
}

void Pickup::onFixedUpdate(GameContext &context, float seconds) {
    age_ += static_cast<double>(seconds);
    if (lifetime > 0.0F && age_ >= static_cast<double>(lifetime)) {
        context.destroyLater(entity().id());
        return;
    }
    if (bob)
        if (auto *sprite = entity().get<SpriteRenderer>())
            sprite->offset.y = baseY_ + 0.06F * static_cast<float>(std::sin(age_ * 3.0));
}

void Pickup::setStack(GameContext &context, ItemStack stack) {
    stack_ = std::move(stack);
    placed_ = true;
    item = stack_.item.str();
    count = stack_.count;
    if (auto *spatial = context.services().find<SpatialIndexService>())
        spatial->refresh(entity(), "pickup");
}

bool Pickup::collect(GameContext &context, Entity &collector) {
    auto *inventory = collector.get<Inventory>();
    if (!inventory || stack_.empty())
        return false;
    const int mine = levelOf(entity()), theirs = levelOf(collector);
    if (mine != everyLevel && theirs != everyLevel && mine != theirs)
        return false;
    const int total = stack_.count;
    const int left = inventory->add(context, stack_);
    const int moved = total - left;
    if (moved <= 0)
        return false;
    Json data = Json::object();
    data.set("item", stack_.item.str());
    data.set("count", moved);
    context.events().emit(
        GameEvent("item.picked_up", collector.id(), entity().id(), std::move(data)));
    stack_.count = left;
    count = left;
    if (left == 0)
        context.destroyLater(entity().id());
    return true;
}

EntityId Pickup::place(GameContext &context, const ItemStack &stack, Vec2 at, const Entity *from) {
    if (stack.empty())
        return {};
    const ItemDefinition *def = gameData(context).items.items.find(stack.item.str());
    if (!def)
        return {};
    const int level = from ? levelOf(*from) : 0;
    // A matching stack already lying there takes it, up to its stack size.
    if (auto *spatial = context.services().find<SpatialIndexService>())
        for (const SpatialHash::Hit &hit : spatial->near("pickup", at, mergeDistance, level)) {
            Entity *other = context.scene().find(hit.id);
            auto *pickup = other ? other->get<Pickup>() : nullptr;
            if (pickup && pickup->stack_.matches(stack) &&
                pickup->stack_.count + stack.count <= def->stackSize) {
                pickup->stack_.count += stack.count;
                pickup->count = pickup->stack_.count;
                return hit.id;
            }
        }
    Scene &scene = context.scene();
    Entity *made = nullptr;
    if (!def->worldPrefab.empty()) {
        auto spawned = context.spawnPrefab(def->worldPrefab, at);
        made = spawned ? scene.find(spawned.value()) : nullptr;
        if (made && !made->get<Pickup>()) {
            log(LogLevel::Warning, "items",
                "the world prefab '" + def->worldPrefab + "' of '" + def->id +
                    "' has no Pickup component, so a generic one is used");
            context.destroyLater(made->id());
            made = nullptr;
        } else if (!spawned) {
            log(LogLevel::Warning, "items",
                "the world prefab of '" + def->id + "': " + spawned.error());
        }
    }
    if (!made) {
        made = &scene.createEntity("Pickup: " + def->displayName);
        made->add<Pickup>();
        auto &sprite = made->add<SpriteRenderer>();
        sprite.texture.path = def->icon;
        sprite.size = {0.5F, 0.5F};
        if (def->icon.empty())
            sprite.color = {230, 200, 90, 255};
    }
    made->setWorldPosition(at);
    if (from && !scene.settings.levels.empty()) {
        auto &layer = made->get<WorldLayer>() ? *made->get<WorldLayer>() : made->add<WorldLayer>();
        layer.level = scene.settings.levels.idOf(level);
    }
    made->get<Pickup>()->setStack(context, stack);
    return made->id();
}

Json Pickup::saveState() const {
    Json state = Json::object();
    state.set("stack", stack_.toJson());
    state.set("age", age_);
    return state;
}

Status Pickup::loadState(GameContext &context, const Json &state) {
    auto stack = ItemStack::fromJson(state.get("stack"));
    if (!stack)
        return Error{"pickup: " + stack.error()};
    if (!gameData(context).items.items.contains(stack.value().item.str())) {
        log(LogLevel::Warning, "items",
            "a saved pickup of '" + stack.value().item.str() + "' no longer exists");
        stack_ = {};
        context.destroyLater(entity().id());
        return success();
    }
    setStack(context, std::move(stack.value()));
    age_ = state.get("age").asNumber(0.0);
    return success();
}
} // namespace yk
