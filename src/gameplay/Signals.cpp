#include "yk/gameplay/Gameplay.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace yk {
const std::vector<std::string> &signalLogicNames() {
    static const std::vector<std::string> names{"Any", "All"};
    return names;
}

void SignalReceiver::setSignal(EntityId source, bool active) {
    inputs_[source] = active;
}
bool SignalReceiver::signalActive() const {
    bool combined = false;
    if (!inputs_.empty()) {
        const auto active = [](const auto &input) { return input.second; };
        combined = logic == SignalLogic::Any ? std::any_of(inputs_.begin(), inputs_.end(), active)
                                             : std::all_of(inputs_.begin(), inputs_.end(), active);
    }
    return combined != invert;
}
void sendSignal(Scene &scene, EntityId source, const std::vector<EntityRef> &targets, bool active) {
    for (const EntityRef reference : targets)
        if (Entity *target = scene.find(reference))
            for (SignalReceiver *receiver : target->getAll<SignalReceiver>())
                receiver->setSignal(source, active);
}

bool matchesActivator(const Entity &entity, const std::vector<std::string> &tags) {
    if (!tags.empty())
        return std::any_of(tags.begin(), tags.end(),
                           [&](const std::string &tag) { return entity.hasTag(tag); });
    const auto *body = entity.get<RigidBody>();
    return body && body->type != RigidBodyType::Static;
}

void spawnEffect(GameContext &context, const AssetRef &prefab, Vec2 worldPosition) {
    if (!prefab.path.empty())
        context.spawnPrefab(prefab.path, worldPosition); // Failure is reported once by the runtime.
}

namespace {
void moveKinematicTo(GameContext &context, Entity &entity, Vec2 worldTarget,
                     std::optional<float> worldRotationDegrees) {
    const auto body = context.bodyOf(entity.id());
    const auto *rigid = entity.get<RigidBody>();
    const Transform2D current = entity.worldTransform();
    if (!body || !rigid || rigid->type == RigidBodyType::Static) {
        Transform2D moved = current;
        moved.position = worldTarget;
        if (worldRotationDegrees)
            moved.rotationDegrees = *worldRotationDegrees;
        entity.setWorldTransform(moved);
        if (body)
            context.teleport(entity, worldTarget); // Puts the body at the entity's new pose.
        return;
    }
    // Velocities that arrive exactly at the target after one tick, so the solver carries whatever
    // rests on the body (and turns it about the body's origin when a rotation is asked for).
    float angular = 0.0F;
    if (worldRotationDegrees)
        angular = degreesToRadians(
                      std::remainder(*worldRotationDegrees - current.rotationDegrees, 360.0F)) /
                  context.fixedDelta();
    context.physics().setVelocity(*body, (worldTarget - current.position) / context.fixedDelta(),
                                  angular);
}
} // namespace

void moveKinematic(GameContext &context, Entity &entity, Vec2 worldTarget) {
    moveKinematicTo(context, entity, worldTarget, std::nullopt);
}
void moveKinematic(GameContext &context, Entity &entity, Vec2 worldTarget,
                   float worldRotationDegrees) {
    moveKinematicTo(context, entity, worldTarget, worldRotationDegrees);
}
} // namespace yk
