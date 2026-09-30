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

bool hasTriggerCollider(const Entity &entity) {
    const auto colliders = entity.getAll<Collider>();
    return std::any_of(colliders.begin(), colliders.end(),
                       [](const Collider *collider) { return collider->isTrigger; });
}

void spawnEffect(GameContext &context, const AssetRef &prefab, Vec2 worldPosition) {
    if (!prefab.path.empty())
        context.spawnPrefab(prefab.path, worldPosition); // Failure is reported once by the runtime.
}

bool pathBlocked(GameContext &context, const Entity &entity, Vec2 direction) {
    const auto body = context.bodyOf(entity.id());
    const float span = length(direction);
    if (!body || span < 1e-6F)
        return false;
    const Vec2 heading = direction / span;
    auto &world = context.physics();
    const auto contacts = world.contacts(*body);
    if (!contacts)
        return false;
    constexpr float skin = 0.04F;
    const auto closer = [](const physics::Contact &contact, float distance) {
        float closest = 1e9F;
        for (const physics::ContactPoint &point : contact.points)
            closest = std::min(closest, point.separation);
        return closest < distance;
    };
    for (const physics::Contact &contact : contacts.value()) {
        const auto firstBody = world.bodyOf(contact.first);
        const bool entityFirst = firstBody && firstBody.value() == *body;
        const Vec2 toOther = entityFirst ? contact.normal : -contact.normal;
        if (dot(toOther, heading) < 0.7F || !closer(contact, skin))
            continue; // Not on the leading face.
        const auto otherBody = world.bodyOf(entityFirst ? contact.second : contact.first);
        if (!otherBody)
            continue;
        const Entity *owner = context.entityOfBody(otherBody.value());
        const auto *rigid = owner ? owner->get<RigidBody>() : nullptr;
        if (!rigid || rigid->type != RigidBodyType::Dynamic)
            continue; // Only what can be crushed is protected.
        // Is that body pressed against something else from the far side?
        const auto others = world.contacts(otherBody.value());
        if (!others)
            continue;
        for (const physics::Contact &farther : others.value()) {
            const auto owner1 = world.bodyOf(farther.first);
            const bool bodyFirst = owner1 && owner1.value() == otherBody.value();
            const auto partner = world.bodyOf(bodyFirst ? farther.second : farther.first);
            if (!partner || partner.value() == *body || !closer(farther, skin))
                continue;
            // From the partner toward the squeezed body: opposite to where the entity is going.
            const Vec2 fromPartner = bodyFirst ? -farther.normal : farther.normal;
            if (dot(fromPartner, heading) < -0.7F)
                return true;
        }
    }
    return false;
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
