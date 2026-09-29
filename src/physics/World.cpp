#include "WorldImpl.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <numbers>
#include <type_traits>

namespace yk::physics {
namespace {
constexpr float minimumDimension = 0.005F;
bool positiveDimension(float value) {
    return bounded(value) && value >= minimumDimension;
}
bool validPose(Pose pose) {
    return bounded(pose.position) && bounded(pose.angleRadians);
}
Error invalidHandle() {
    return {"Invalid, destroyed, or foreign physics handle"};
}
b2BodyType native(BodyType type) {
    switch (type) {
    case BodyType::Static:
        return b2_staticBody;
    case BodyType::Kinematic:
        return b2_kinematicBody;
    case BodyType::Dynamic:
        return b2_dynamicBody;
    }
    return b2_staticBody;
}
} // namespace

World::World() : impl_(std::make_unique<Impl>()) {}
World::~World() = default;
Result<std::unique_ptr<World>> World::create(const WorldConfig &config) {
    if (!bounded(config.gravity) || !std::isfinite(config.fixedSeconds) ||
        config.fixedSeconds < 0.0001 || config.fixedSeconds > 0.1 ||
        !std::isfinite(config.maxFrameSeconds) || config.maxFrameSeconds < config.fixedSeconds ||
        config.maxFrameSeconds > 1.0 || config.maxStepsPerAdvance == 0 ||
        config.maxStepsPerAdvance > 1024 || config.subSteps < 1 || config.subSteps > 64)
        return Error{"Invalid physics world timing, gravity, or solver configuration"};
    auto result = std::unique_ptr<World>(new World());
    result->impl_->config = config;
    auto definition = b2DefaultWorldDef();
    definition.gravity = physics::native(config.gravity);
    definition.enableSleep = config.enableSleep;
    definition.enableContinuous = config.continuousCollision;
    result->impl_->world = b2CreateWorld(&definition);
    if (B2_IS_NULL(result->impl_->world))
        return Error{"Create physics world failed"};
    return result;
}
bool World::valid(BodyHandle body) const {
    return impl_->owns(body) && impl_->bodies.contains(body.serial_);
}
bool World::valid(ShapeHandle shape) const {
    return impl_->owns(shape) && impl_->shapes.contains(shape.serial_);
}
bool World::valid(JointHandle joint) const {
    return impl_->owns(joint) && impl_->joints.contains(joint.serial_);
}
Result<BodyHandle> World::createBody(const BodyDef &definition) {
    impl_->assertThread();
    if ((definition.type != BodyType::Static && definition.type != BodyType::Kinematic &&
         definition.type != BodyType::Dynamic) ||
        !validPose(definition.pose) || !bounded(definition.linearVelocity) ||
        !bounded(definition.angularVelocity) || !nonnegative(definition.linearDamping) ||
        !nonnegative(definition.angularDamping) || !bounded(definition.gravityScale))
        return Error{"Invalid physics body definition"};
    auto body = b2DefaultBodyDef();
    body.type = native(definition.type);
    body.position = physics::native(definition.pose.position);
    body.rotation = b2MakeRot(definition.pose.angleRadians);
    body.linearVelocity = physics::native(definition.linearVelocity);
    body.angularVelocity = definition.angularVelocity;
    body.linearDamping = definition.linearDamping;
    body.angularDamping = definition.angularDamping;
    body.gravityScale = definition.gravityScale;
    body.fixedRotation = definition.fixedRotation;
    body.isBullet = definition.bullet;
    body.enableSleep = definition.enableSleep;
    const auto id = b2CreateBody(impl_->world, &body);
    const auto handle = impl_->handle<BodyTag>();
    impl_->bodies.emplace(handle.serial_, Impl::Body{id, definition.pose, {}, {}});
    return handle;
}
Result<ShapeHandle> World::createShape(BodyHandle body, const Geometry &geometry,
                                       const ShapeDef &definition) {
    if (!valid(body))
        return invalidHandle();
    if (!nonnegative(definition.density) || !nonnegative(definition.friction) ||
        !nonnegative(definition.restitution) || definition.restitution > 1 ||
        !validFilter(definition.filter))
        return Error{"Invalid density, friction, restitution, or collision group"};
    auto shape = b2DefaultShapeDef();
    shape.density = definition.density;
    shape.material.friction = definition.friction;
    shape.material.restitution = definition.restitution;
    shape.filter = physics::native(definition.filter);
    shape.isSensor = definition.sensor;
    shape.enableSensorEvents = true;
    shape.enableContactEvents = true;
    shape.enableHitEvents = true;
    auto &record = impl_->bodies.at(body.serial_);
    const auto createNativeShape = [&] {
        return std::visit(
            [&](const auto &geometryValue) -> b2ShapeId {
                using T = std::decay_t<decltype(geometryValue)>;
                if constexpr (std::is_same_v<T, Circle>) {
                    if (!positiveDimension(geometryValue.radius) || !bounded(geometryValue.center))
                        return {};
                    const b2Circle circle{physics::native(geometryValue.center),
                                          geometryValue.radius};
                    return b2CreateCircleShape(record.nativeId, &shape, &circle);
                } else if constexpr (std::is_same_v<T, Box>) {
                    if (!positiveDimension(geometryValue.halfExtents.x) ||
                        !positiveDimension(geometryValue.halfExtents.y) ||
                        !bounded(geometryValue.center) || !bounded(geometryValue.angleRadians))
                        return {};
                    const auto box =
                        b2MakeOffsetBox(geometryValue.halfExtents.x, geometryValue.halfExtents.y,
                                        physics::native(geometryValue.center),
                                        b2MakeRot(geometryValue.angleRadians));
                    return b2CreatePolygonShape(record.nativeId, &shape, &box);
                } else if constexpr (std::is_same_v<T, Capsule> || std::is_same_v<T, Segment>) {
                    if (!bounded(geometryValue.first) || !bounded(geometryValue.second) ||
                        std::hypot(geometryValue.second.x - geometryValue.first.x,
                                   geometryValue.second.y - geometryValue.first.y) <
                            minimumDimension)
                        return {};
                    if constexpr (std::is_same_v<T, Capsule>) {
                        if (!positiveDimension(geometryValue.radius))
                            return {};
                        const b2Capsule capsule{physics::native(geometryValue.first),
                                                physics::native(geometryValue.second),
                                                geometryValue.radius};
                        return b2CreateCapsuleShape(record.nativeId, &shape, &capsule);
                    } else {
                        if (definition.sensor || b2Body_GetType(record.nativeId) == b2_dynamicBody)
                            return {};
                        const b2Segment segment{physics::native(geometryValue.first),
                                                physics::native(geometryValue.second)};
                        return b2CreateSegmentShape(record.nativeId, &shape, &segment);
                    }
                } else {
                    if (geometryValue.vertices.size() < 3 ||
                        geometryValue.vertices.size() > B2_MAX_POLYGON_VERTICES)
                        return {};
                    std::array<b2Vec2, B2_MAX_POLYGON_VERTICES> points{};
                    for (std::size_t i = 0; i < geometryValue.vertices.size(); ++i) {
                        if (!bounded(geometryValue.vertices[i]))
                            return {};
                        points[i] = physics::native(geometryValue.vertices[i]);
                    }
                    const auto hull = b2ComputeHull(
                        points.data(), static_cast<int>(geometryValue.vertices.size()));
                    if (hull.count != static_cast<int>(geometryValue.vertices.size()) ||
                        !b2ValidateHull(&hull))
                        return {};
                    const auto polygon = b2MakePolygon(&hull, 0);
                    return b2CreatePolygonShape(record.nativeId, &shape, &polygon);
                }
            },
            geometry);
    };
    auto id = createNativeShape();
    if (B2_IS_NULL(id))
        return Error{"Invalid shape geometry (minimum dimension 0.005 m; polygons must be convex)"};
    if (impl_->nativeShapes.contains(b2StoreShapeId(id))) {
        // A native 16-bit generation may wrap while an old end event is pending.
        // Skip that identity before exposing it; the unstepped shape has no events.
        b2DestroyShape(id, true);
        id = createNativeShape();
        assert(B2_IS_NON_NULL(id) && !impl_->nativeShapes.contains(b2StoreShapeId(id)));
    }
    const auto handle = impl_->handle<ShapeTag>();
    impl_->shapes.emplace(handle.serial_, Impl::Shape{id, body, impl_->ticks});
    impl_->nativeShapes.insert_or_assign(b2StoreShapeId(id), handle);
    record.shapes.push_back(handle);
    return handle;
}
Status World::destroy(ShapeHandle shape) {
    if (!valid(shape))
        return invalidHandle();
    const auto record = impl_->shapes.at(shape.serial_);
    impl_->retireShape(record);
    b2DestroyShape(record.nativeId, true);
    std::erase(impl_->bodies.at(record.body.serial_).shapes, shape);
    impl_->shapes.erase(shape.serial_);
    return success();
}
Status World::destroy(JointHandle joint) {
    if (!valid(joint))
        return invalidHandle();
    const auto record = impl_->joints.at(joint.serial_);
    b2DestroyJoint(record.nativeId);
    std::erase(impl_->bodies.at(record.first.serial_).joints, joint);
    std::erase(impl_->bodies.at(record.second.serial_).joints, joint);
    impl_->joints.erase(joint.serial_);
    return success();
}
Status World::destroy(BodyHandle body) {
    if (!valid(body))
        return invalidHandle();
    auto &record = impl_->bodies.at(body.serial_);
    while (!record.joints.empty()) {
        auto status = destroy(record.joints.back());
        if (!status)
            return status;
    }
    for (const auto &shape : record.shapes) {
        impl_->retireShape(impl_->shapes.at(shape.serial_));
        impl_->shapes.erase(shape.serial_);
    }
    b2DestroyBody(record.nativeId);
    impl_->bodies.erase(body.serial_);
    return success();
}
Result<BodyState> World::state(BodyHandle body) const {
    if (!valid(body))
        return invalidHandle();
    const auto id = impl_->bodies.at(body.serial_).nativeId;
    return BodyState{
        poseOf(id),          vector(b2Body_GetLinearVelocity(id)), b2Body_GetAngularVelocity(id),
        b2Body_GetMass(id),  b2Body_GetRotationalInertia(id),      b2Body_IsAwake(id),
        b2Body_IsEnabled(id)};
}
Result<BodyHandle> World::bodyOf(ShapeHandle shape) const {
    if (!valid(shape))
        return invalidHandle();
    return impl_->shapes.at(shape.serial_).body;
}
Status World::setPose(BodyHandle body, Pose pose) {
    if (!valid(body))
        return invalidHandle();
    if (!validPose(pose))
        return Error{"Invalid body pose"};
    auto &record = impl_->bodies.at(body.serial_);
    b2Body_SetTransform(record.nativeId, physics::native(pose.position),
                        b2MakeRot(pose.angleRadians));
    b2Body_SetAwake(record.nativeId, true);
    record.previous = poseOf(record.nativeId);
    return success();
}
Status World::setVelocity(BodyHandle body, Vec2 linear, float angular) {
    if (!valid(body))
        return invalidHandle();
    if (!bounded(linear) || !bounded(angular))
        return Error{"Invalid body velocity"};
    const auto id = impl_->bodies.at(body.serial_).nativeId;
    if (b2Body_GetType(id) == b2_staticBody)
        return Error{"Static bodies cannot have velocity"};
    b2Body_SetLinearVelocity(id, physics::native(linear));
    b2Body_SetAngularVelocity(id, angular);
    return success();
}
Status World::setEnabled(BodyHandle body, bool enabled) {
    if (!valid(body))
        return invalidHandle();
    const auto id = impl_->bodies.at(body.serial_).nativeId;
    if (enabled) {
        b2Body_Enable(id);
        b2Body_SetAwake(id, true);
    } else
        b2Body_Disable(id);
    return success();
}
Status World::setAwake(BodyHandle body, bool awake) {
    if (!valid(body))
        return invalidHandle();
    b2Body_SetAwake(impl_->bodies.at(body.serial_).nativeId, awake);
    return success();
}
Status World::setFilter(ShapeHandle shape, CollisionFilter filter) {
    if (!valid(shape))
        return invalidHandle();
    if (!validFilter(filter))
        return Error{"Collision group must fit a signed 16-bit integer"};
    b2Shape_SetFilter(impl_->shapes.at(shape.serial_).nativeId, physics::native(filter));
    return success();
}
Status World::setGravity(Vec2 gravity) {
    impl_->assertThread();
    if (!bounded(gravity))
        return Error{"Invalid gravity"};
    b2World_SetGravity(impl_->world, physics::native(gravity));
    impl_->config.gravity = gravity;
    return success();
}
Status World::setGravityScale(BodyHandle body, float scale) {
    if (!valid(body))
        return invalidHandle();
    if (!bounded(scale))
        return Error{"Invalid gravity scale"};
    b2Body_SetGravityScale(impl_->bodies.at(body.serial_).nativeId, scale);
    return success();
}
Status World::setFriction(ShapeHandle shape, float friction) {
    if (!valid(shape))
        return invalidHandle();
    if (!nonnegative(friction))
        return Error{"Friction must be finite and nonnegative"};
    b2Shape_SetFriction(impl_->shapes.at(shape.serial_).nativeId, friction);
    return success();
}
Status World::setRestitution(ShapeHandle shape, float restitution) {
    if (!valid(shape))
        return invalidHandle();
    if (!nonnegative(restitution) || restitution > 1)
        return Error{"Restitution must be within [0, 1]"};
    b2Shape_SetRestitution(impl_->shapes.at(shape.serial_).nativeId, restitution);
    return success();
}
Status World::applyForce(BodyHandle body, Vec2 force, std::optional<Vec2> worldPoint) {
    if (!valid(body))
        return invalidHandle();
    if (!bounded(force) || (worldPoint && !bounded(*worldPoint)))
        return Error{"Invalid force or world point"};
    const auto id = impl_->bodies.at(body.serial_).nativeId;
    if (b2Body_GetType(id) != b2_dynamicBody)
        return Error{"Forces require a dynamic body"};
    if (worldPoint)
        b2Body_ApplyForce(id, physics::native(force), physics::native(*worldPoint), true);
    else
        b2Body_ApplyForceToCenter(id, physics::native(force), true);
    return success();
}
Status World::applyImpulse(BodyHandle body, Vec2 impulse, std::optional<Vec2> worldPoint) {
    if (!valid(body))
        return invalidHandle();
    if (!bounded(impulse) || (worldPoint && !bounded(*worldPoint)))
        return Error{"Invalid impulse or world point"};
    const auto id = impl_->bodies.at(body.serial_).nativeId;
    if (b2Body_GetType(id) != b2_dynamicBody)
        return Error{"Impulses require a dynamic body"};
    if (worldPoint)
        b2Body_ApplyLinearImpulse(id, physics::native(impulse), physics::native(*worldPoint), true);
    else
        b2Body_ApplyLinearImpulseToCenter(id, physics::native(impulse), true);
    return success();
}
Status World::applyTorque(BodyHandle body, float torque) {
    if (!valid(body))
        return invalidHandle();
    if (!bounded(torque))
        return Error{"Invalid torque"};
    const auto id = impl_->bodies.at(body.serial_).nativeId;
    if (b2Body_GetType(id) != b2_dynamicBody)
        return Error{"Torque requires a dynamic body"};
    b2Body_ApplyTorque(id, torque, true);
    return success();
}
void World::Impl::collectEvents() {
    const auto contactEvents = b2World_GetContactEvents(world);
    for (int i = 0; i < contactEvents.beginCount; ++i) {
        const auto &event = contactEvents.beginEvents[i];
        const Vec2 point =
            event.manifold.pointCount > 0 ? vector(event.manifold.points[0].point) : Vec2{};
        events.push_back({EventType::ContactBegin, shapeHandle(event.shapeIdA),
                          shapeHandle(event.shapeIdB), point, vector(event.manifold.normal), 0,
                          ticks});
    }
    for (int i = 0; i < contactEvents.endCount; ++i) {
        const auto &event = contactEvents.endEvents[i];
        events.push_back({EventType::ContactEnd,
                          shapeHandle(event.shapeIdA),
                          shapeHandle(event.shapeIdB),
                          {},
                          {},
                          0,
                          ticks});
    }
    for (int i = 0; i < contactEvents.hitCount; ++i) {
        const auto &event = contactEvents.hitEvents[i];
        events.push_back({EventType::ContactHit, shapeHandle(event.shapeIdA),
                          shapeHandle(event.shapeIdB), vector(event.point), vector(event.normal),
                          event.approachSpeed, ticks});
    }
    const auto sensorEvents = b2World_GetSensorEvents(world);
    for (int i = 0; i < sensorEvents.beginCount; ++i) {
        const auto &event = sensorEvents.beginEvents[i];
        events.push_back({EventType::SensorBegin,
                          shapeHandle(event.sensorShapeId),
                          shapeHandle(event.visitorShapeId),
                          {},
                          {},
                          0,
                          ticks});
    }
    for (int i = 0; i < sensorEvents.endCount; ++i) {
        const auto &event = sensorEvents.endEvents[i];
        events.push_back({EventType::SensorEnd,
                          shapeHandle(event.sensorShapeId),
                          shapeHandle(event.visitorShapeId),
                          {},
                          {},
                          0,
                          ticks});
    }
    for (const auto id : retiredShapes) {
        const auto entry = nativeShapes.find(id);
        if (entry != nativeShapes.end() && !shapes.contains(entry->second.serial_))
            nativeShapes.erase(entry);
    }
    retiredShapes.clear();
}
Result<StepResult> World::advance(double elapsedSeconds) {
    impl_->assertThread();
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0)
        return Error{"Elapsed physics time must be finite and nonnegative"};
    const auto &config = impl_->config;
    StepResult result;
    const double accepted = std::min(elapsedSeconds, config.maxFrameSeconds);
    result.droppedSeconds = elapsedSeconds - accepted;
    impl_->accumulator += accepted;
    impl_->events.clear();
    constexpr double timeTolerance = 1e-12;
    while (impl_->accumulator + timeTolerance >= config.fixedSeconds &&
           result.steps < config.maxStepsPerAdvance) {
        for (auto &[serial, body] : impl_->bodies) {
            (void)serial;
            body.previous = poseOf(body.nativeId);
        }
        b2World_Step(impl_->world, static_cast<float>(config.fixedSeconds), config.subSteps);
        ++impl_->ticks;
        impl_->collectEvents();
        impl_->accumulator = std::max(0.0, impl_->accumulator - config.fixedSeconds);
        ++result.steps;
    }
    if (impl_->accumulator + timeTolerance >= config.fixedSeconds) {
        const double droppedTicks =
            std::floor((impl_->accumulator + timeTolerance) / config.fixedSeconds);
        const double dropped = droppedTicks * config.fixedSeconds;
        impl_->accumulator = std::max(0.0, impl_->accumulator - dropped);
        result.droppedSeconds += dropped;
    }
    result.simulatedSeconds = static_cast<double>(result.steps) * config.fixedSeconds;
    impl_->alpha = std::min(std::nextafter(1.0F, 0.0F),
                            static_cast<float>(impl_->accumulator / config.fixedSeconds));
    result.interpolationAlpha = impl_->alpha;
    return result;
}
Result<Pose> World::interpolatedPose(BodyHandle body) const {
    if (!valid(body))
        return invalidHandle();
    const auto &record = impl_->bodies.at(body.serial_);
    const auto current = poseOf(record.nativeId);
    const float angle = std::remainder(current.angleRadians - record.previous.angleRadians,
                                       2 * std::numbers::pi_v<float>);
    return Pose{record.previous.position +
                    (current.position - record.previous.position) * impl_->alpha,
                record.previous.angleRadians + angle * impl_->alpha};
}
std::span<const Event> World::events() const {
    impl_->assertThread();
    return impl_->events;
}
WorldStats World::stats() const {
    impl_->assertThread();
    return {impl_->bodies.size(), impl_->shapes.size(), impl_->joints.size(),
            b2World_GetAwakeBodyCount(impl_->world), impl_->ticks};
}
} // namespace yk::physics
