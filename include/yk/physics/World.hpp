#pragma once
#include "yk/core/Result.hpp"
#include "yk/physics/Types.hpp"
#include <optional>
#include <span>

namespace yk::physics {
// Owns all bodies/shapes/joints. Single-threaded, independent of SDL and the application loop.
class World {
  public:
    static Result<std::unique_ptr<World>> create(const WorldConfig &config = {});
    ~World();
    World(const World &) = delete;
    World &operator=(const World &) = delete;

    Result<BodyHandle> createBody(const BodyDef &definition = {});
    Result<ShapeHandle> createShape(BodyHandle body, const Geometry &geometry,
                                    const ShapeDef &definition = {});
    Result<JointHandle> createDistanceJoint(const DistanceJointDef &definition);
    Result<JointHandle> createRevoluteJoint(const RevoluteJointDef &definition);
    Status destroy(BodyHandle body); // Also invalidates attached shapes and joints.
    Status destroy(ShapeHandle shape);
    Status destroy(JointHandle joint);
    bool valid(BodyHandle body) const;
    bool valid(ShapeHandle shape) const;
    bool valid(JointHandle joint) const;

    Result<BodyState> state(BodyHandle body) const;
    Result<BodyHandle> bodyOf(ShapeHandle shape) const;
    Status setPose(BodyHandle body, Pose pose); // Teleport; resets interpolation for this body.
    Status setVelocity(BodyHandle body, Vec2 linear, float angular = 0);
    Status setEnabled(BodyHandle body, bool enabled);
    Status setAwake(BodyHandle body, bool awake);
    Status setFilter(ShapeHandle shape, CollisionFilter filter);
    Status setGravity(Vec2 gravity);
    Status applyForce(BodyHandle body, Vec2 force, std::optional<Vec2> worldPoint = {});
    Status applyImpulse(BodyHandle body, Vec2 impulse, std::optional<Vec2> worldPoint = {});
    Status applyTorque(BodyHandle body, float torque);

    // Accumulates elapsed time, clamps frame delays, caps catch-up and reports dropped time.
    Result<StepResult> advance(double elapsedSeconds);
    Result<Pose> interpolatedPose(BodyHandle body) const;
    std::span<const Event> events() const; // Valid until next advance or world destruction.
    WorldStats stats() const;

    Result<std::optional<RayHit>> rayCast(Vec2 origin, Vec2 translation,
                                          QueryFilter filter = {}) const;
    Result<std::vector<ShapeHandle>> queryAabb(Rect bounds, QueryFilter filter = {}) const;
    Result<std::vector<ShapeHandle>> queryPoint(Vec2 point, QueryFilter filter = {}) const;
    Result<std::vector<Contact>> contacts(BodyHandle body) const;
    Result<std::vector<ShapeHandle>> sensorOverlaps(ShapeHandle sensor) const;
    Result<std::vector<DebugLine>> debugLines(unsigned circleSegments = 24) const;

  private:
    World();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace yk::physics
