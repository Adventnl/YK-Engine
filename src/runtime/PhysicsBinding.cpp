#include "RuntimeImpl.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
namespace {
physics::BodyType toPhysicsType(RigidBodyType type) {
    switch (type) {
    case RigidBodyType::Static:
        return physics::BodyType::Static;
    case RigidBodyType::Kinematic:
        return physics::BodyType::Kinematic;
    case RigidBodyType::Dynamic:
        return physics::BodyType::Dynamic;
    }
    return physics::BodyType::Static;
}
physics::Pose poseOf(const Transform2D &world) {
    return {world.position, degreesToRadians(world.rotationDegrees)};
}
// The nearest entity at or above `entity` that carries a RigidBody.
Entity *bodyOwner(Entity &entity) {
    for (Entity *node = &entity; node; node = node->parent())
        if (node->has<RigidBody>())
            return node;
    return nullptr;
}
// Collider geometry in the body's unscaled local frame, sized in world units.
std::optional<physics::Geometry> makeGeometry(const Collider &collider,
                                              const Transform2D &colliderWorld,
                                              const Transform2D &bodyWorld) {
    const Vec2 scale{std::fabs(colliderWorld.scale.x), std::fabs(colliderWorld.scale.y)};
    const Vec2 extent = hadamard(collider.size, scale);
    if (extent.x <= 0.0F || extent.y <= 0.0F)
        return std::nullopt;
    const Vec2 worldCenter = transformPoint(colliderWorld, collider.offset);
    const Vec2 center =
        rotated(worldCenter - bodyWorld.position, -degreesToRadians(bodyWorld.rotationDegrees));
    const float angle = degreesToRadians(colliderWorld.rotationDegrees - bodyWorld.rotationDegrees);
    switch (collider.shape) {
    case ColliderShape::Box:
        return physics::Box{extent * 0.5F, center, angle};
    case ColliderShape::Circle:
        return physics::Circle{0.5F * collider.size.x * std::max(scale.x, scale.y), center};
    case ColliderShape::Capsule: {
        const float radius = extent.x * 0.5F;
        const float half = (extent.y - extent.x) * 0.5F;
        if (half <= 0.005F)
            return physics::Circle{radius, center};
        const Vec2 axis = rotated({0.0F, half}, angle);
        return physics::Capsule{center - axis, center + axis, radius};
    }
    }
    return std::nullopt;
}
bool contains(const std::vector<EntityId> &list, EntityId id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}
} // namespace

Status GameRuntime::Impl::buildWorld() {
    physics::WorldConfig config;
    config.gravity = scene->settings.gravity;
    config.fixedSeconds = options.fixedSeconds;
    auto created = physics::World::create(config);
    if (!created)
        return Error{created.error()};
    world = std::move(created.value());
    bodies.clear();
    entityByBody.clear();
    shapes.clear();
    shapesByEntity.clear();
    triggerShapes.clear();
    overlaps.clear();
    bindEntities(scene->hierarchyOrder());
    return success();
}

GameRuntime::Impl::BodyRecord *GameRuntime::Impl::ensureBody(Entity &owner, bool implicit) {
    if (const auto found = bodies.find(owner.id()); found != bodies.end())
        return &found->second;
    const auto *rigid = owner.get<RigidBody>();
    physics::BodyDef definition;
    definition.type = rigid ? toPhysicsType(rigid->type) : physics::BodyType::Static;
    definition.pose = poseOf(owner.worldTransform());
    if (rigid) {
        definition.linearDamping = rigid->linearDamping;
        definition.angularDamping = rigid->angularDamping;
        definition.gravityScale = rigid->gravityScale;
        definition.fixedRotation = rigid->fixedRotation;
        definition.bullet = rigid->bullet;
        definition.enableSleep = rigid->allowSleep;
    }
    auto body = world->createBody(definition);
    if (!body) {
        log(LogLevel::Warning, "physics", "'" + owner.name() + "': " + body.error());
        return nullptr;
    }
    BodyRecord record;
    record.body = body.value();
    record.entity = owner.id();
    record.type = definition.type;
    record.implicit = implicit && !rigid;
    entityByBody[record.body.serial()] = owner.id();
    return &bodies.emplace(owner.id(), record).first->second;
}

void GameRuntime::Impl::bindEntities(const std::vector<EntityId> &ids) {
    for (const EntityId id : ids) // Bodies first so colliders can find their owners.
        if (Entity *entity = scene->find(id); entity && entity->has<RigidBody>())
            ensureBody(*entity, false);
    for (const EntityId id : ids) {
        Entity *entity = scene->find(id);
        if (!entity)
            continue;
        for (const Collider *collider : entity->getAll<Collider>()) {
            Entity *owner = bodyOwner(*entity);
            BodyRecord *record = ensureBody(owner ? *owner : *entity, true);
            if (!record)
                continue;
            Entity &bodyEntity = *scene->find(record->entity);
            const auto geometry =
                makeGeometry(*collider, entity->worldTransform(), bodyEntity.worldTransform());
            if (!geometry) {
                log(LogLevel::Warning, "physics", "'" + entity->name() + "': collider has no area");
                continue;
            }
            physics::ShapeDef definition;
            definition.density = collider->isTrigger ? 0.0F : collider->density;
            definition.friction = collider->friction;
            definition.restitution = collider->restitution;
            definition.sensor = collider->isTrigger;
            definition.filter.categoryBits = options.layers.categoryBits(collider->layer);
            definition.filter.maskBits = options.layers.maskBits(collider->layer);
            if (options.layers.indexOf(collider->layer) < 0)
                log(LogLevel::Warning, "physics",
                    "'" + entity->name() + "': unknown collision layer '" + collider->layer +
                        "', using layer 0");
            auto shape = world->createShape(record->body, *geometry, definition);
            if (!shape) {
                log(LogLevel::Warning, "physics", "'" + entity->name() + "': " + shape.error());
                continue;
            }
            ShapeRecord shapeRecord;
            shapeRecord.handle = shape.value();
            shapeRecord.entity = entity->id();
            shapeRecord.bodyEntity = record->entity;
            shapeRecord.collider = collider;
            shapeRecord.filter = definition.filter;
            shapeRecord.trigger = collider->isTrigger;
            shapes.emplace(shape.value().serial(), shapeRecord);
            shapesByEntity[entity->id()].push_back(shape.value().serial());
            if (collider->isTrigger)
                triggerShapes.push_back(shape.value().serial());
        }
    }
}

void GameRuntime::Impl::unbindEntities(const std::vector<EntityId> &ids) {
    for (const EntityId id : ids) {
        if (const auto found = shapesByEntity.find(id); found != shapesByEntity.end()) {
            for (const std::uint64_t serial : found->second) {
                const auto shape = shapes.find(serial);
                if (shape == shapes.end())
                    continue;
                if (world->valid(shape->second.handle))
                    world->destroy(shape->second.handle);
                std::erase(triggerShapes, serial);
                shapes.erase(shape);
            }
            shapesByEntity.erase(found);
        }
        overlaps.erase(id);
        if (const auto body = bodies.find(id); body != bodies.end()) {
            entityByBody.erase(body->second.body.serial());
            if (world->valid(body->second.body))
                world->destroy(
                    body->second.body); // Also invalidates attached shapes of other entities.
            bodies.erase(body);
        }
    }
    // Shapes attached to a destroyed body but owned by surviving entities are gone with it.
    for (auto shape = shapes.begin(); shape != shapes.end();) {
        if (!world->valid(shape->second.handle)) {
            std::erase(triggerShapes, shape->first);
            if (auto list = shapesByEntity.find(shape->second.entity); list != shapesByEntity.end())
                std::erase(list->second, shape->first);
            shape = shapes.erase(shape);
        } else {
            ++shape;
        }
    }
}

void GameRuntime::Impl::syncActivation() {
    for (auto &[id, record] : bodies) {
        Entity *entity = scene->find(id);
        if (!entity)
            continue;
        const auto *rigid = entity->get<RigidBody>();
        const bool wanted = entity->activeInHierarchy() && (!rigid || rigid->enabled);
        if (wanted == record.enabled)
            continue;
        record.enabled = wanted;
        world->setEnabled(record.body, wanted);
        if (wanted) { // It may have been moved while inactive.
            world->setPose(record.body, poseOf(entity->worldTransform()));
            if (record.type != physics::BodyType::Static)
                world->setVelocity(record.body, {}, 0.0F);
        }
    }
    for (auto &[serial, record] : shapes) {
        (void)serial;
        const Entity *entity = scene->find(record.entity);
        const auto body = bodies.find(record.bodyEntity);
        if (!entity || body == bodies.end())
            continue;
        const bool wanted =
            body->second.enabled && entity->activeInHierarchy() && record.collider->enabled;
        if (wanted == record.enabled)
            continue;
        record.enabled = wanted;
        world->setFilter(record.handle, wanted ? record.filter : physics::CollisionFilter{0, 0, 0});
    }
}

void GameRuntime::Impl::syncTransforms() {
    for (const auto &[id, record] : bodies) {
        if (record.type == physics::BodyType::Static || !record.enabled)
            continue;
        Entity *entity = scene->find(id);
        if (!entity)
            continue;
        const auto state = world->state(record.body);
        if (!state)
            continue;
        Transform2D transform = entity->worldTransform();
        transform.position = state.value().pose.position;
        transform.rotationDegrees = radiansToDegrees(state.value().pose.angleRadians);
        entity->setWorldTransform(transform);
    }
}

void GameRuntime::Impl::notifyTrigger(bool enter, Entity &owner, Entity &visitor) {
    GameContext &context = self;
    const auto call = [&](Entity &subject, Entity &counterpart) {
        if (!subject
                 .activeInHierarchy()) // A callback earlier in this tick may have deactivated it.
            return;
        std::vector<Component *> components;
        for (const auto &component : subject.components())
            components.push_back(component.get());
        for (Component *component : components) {
            if (!component->enabled)
                continue;
            if (enter)
                component->onTriggerEnter(context, counterpart);
            else
                component->onTriggerExit(context, counterpart);
        }
    };
    call(owner, visitor);
    call(visitor, owner);
}

void GameRuntime::Impl::updateTriggers() {
    std::map<EntityId, std::vector<EntityId>> next;
    for (const std::uint64_t serial : triggerShapes) {
        const auto found = shapes.find(serial);
        if (found == shapes.end() || !found->second.enabled)
            continue;
        const ShapeRecord &sensor = found->second;
        const auto visitors = world->sensorOverlaps(sensor.handle);
        if (!visitors)
            continue;
        std::vector<EntityId> &list = next[sensor.entity];
        for (const physics::ShapeHandle &visitor : visitors.value()) {
            const auto visitorRecord = shapes.find(visitor.serial());
            if (visitorRecord == shapes.end() || visitorRecord->second.entity == sensor.entity)
                continue;
            if (visitorRecord->second.trigger && !sensor.collider->detectTriggers)
                continue;
            const Entity *other = scene->find(visitorRecord->second.entity);
            if (!other || !other->activeInHierarchy() || contains(list, other->id()))
                continue;
            list.push_back(other->id());
        }
    }
    const auto previous = std::move(overlaps);
    overlaps = next;
    // Exits first, then enters; both in stable entity-id order.
    for (const auto &[owner, before] : previous) {
        const auto now = next.find(owner);
        for (const EntityId visitor : before) {
            if (now != next.end() && contains(now->second, visitor))
                continue;
            Entity *ownerEntity = scene->find(owner);
            Entity *visitorEntity = scene->find(visitor);
            if (ownerEntity && visitorEntity)
                notifyTrigger(false, *ownerEntity, *visitorEntity);
        }
    }
    for (const auto &[owner, now] : next) {
        const auto before = previous.find(owner);
        for (const EntityId visitor : now) {
            if (before != previous.end() && contains(before->second, visitor))
                continue;
            Entity *ownerEntity = scene->find(owner);
            Entity *visitorEntity = scene->find(visitor);
            if (ownerEntity && visitorEntity)
                notifyTrigger(true, *ownerEntity, *visitorEntity);
        }
    }
}

void GameRuntime::Impl::dispatchCollisions() {
    for (const physics::Event &event : world->events()) {
        if (event.type != physics::EventType::ContactBegin)
            continue;
        const auto first = shapes.find(event.first.serial());
        const auto second = shapes.find(event.second.serial());
        if (first == shapes.end() || second == shapes.end() ||
            first->second.entity == second->second.entity)
            continue;
        Entity *a = scene->find(first->second.entity);
        Entity *b = scene->find(second->second.entity);
        if (!a || !b)
            continue;
        GameContext &context = self;
        const auto call = [&](Entity &subject, Entity &other, Vec2 normal) {
            std::vector<Component *> components;
            for (const auto &component : subject.components())
                components.push_back(component.get());
            for (Component *component : components)
                if (component->enabled)
                    component->onCollisionEnter(context, other,
                                                {event.point, normal, event.approachSpeed});
        };
        call(*a, *b, event.normal);
        call(*b, *a, -event.normal);
    }
}
} // namespace yk
