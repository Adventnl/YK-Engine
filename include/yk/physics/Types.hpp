#pragma once
#include "yk/core/Math.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <variant>
#include <vector>

namespace yk::physics {
class World;
template <class Tag> class Handle {
  public:
    Handle() = default;
    bool operator==(const Handle &other) const {
        return serial_ == other.serial_ && !owner_.owner_before(other.owner_) &&
               !other.owner_.owner_before(owner_);
    }
    // Creation serial, unique within the owning world and never reused; zero is the null handle.
    // Suitable as a hash key for per-world tables (handles of different worlds may share serials).
    std::uint64_t serial() const {
        return serial_;
    }

  private:
    friend class World;
    std::weak_ptr<const void> owner_;
    std::uint64_t serial_{};
};
using BodyHandle = Handle<struct BodyTag>;
using ShapeHandle = Handle<struct ShapeTag>;
using JointHandle = Handle<struct JointTag>;

enum class BodyType { Static, Kinematic, Dynamic };
struct Pose {
    Vec2 position{};
    float angleRadians{};
};
struct BodyDef {
    BodyType type{BodyType::Dynamic};
    Pose pose;
    Vec2 linearVelocity{};
    float angularVelocity{};
    float linearDamping{};
    float angularDamping{};
    float gravityScale{1.0F};
    bool fixedRotation{};
    bool bullet{};
    bool enableSleep{true};
};
struct BodyState {
    Pose pose;
    Vec2 linearVelocity; // Of the center of mass (the origin for kinematic and static bodies).
    float angularVelocity{};
    float mass{};
    float rotationalInertia{};
    Vec2 worldCenter; // Center of mass in world space: what the body rotates about.
    bool awake{};
    bool enabled{};
};
struct CollisionFilter {
    std::uint64_t categoryBits{1};
    std::uint64_t maskBits{UINT64_MAX};
    int groupIndex{}; // Equal positive groups always collide; negative groups never collide.
};
struct ShapeDef {
    float density{1.0F}; // kg/m^2; sensors also contribute mass unless set to zero.
    float friction{0.6F};
    float restitution{};
    CollisionFilter filter;
    bool sensor{};
    // A one-way platform blocks only what comes from its solid side: a body above it (relative to
    // `oneWayNormal`) lands and stands on it, while a body below or beside it, or moving up through
    // it, passes freely. `oneWayNormal` is body-local (+Y is down, so "up" is (0, -1)). Not allowed
    // on sensors. The decision is made per contact, before the solver responds.
    bool oneWay{};
    Vec2 oneWayNormal{0.0F, -1.0F};
};
struct Circle {
    float radius{0.5F};
    Vec2 center{};
};
struct Box {
    Vec2 halfExtents{0.5F, 0.5F};
    Vec2 center{};
    float angleRadians{};
    // Rounds the four corners: `halfExtents` stays the outer size. A rounded top edge lets a
    // character slide up a low step instead of catching on its corner. Must be smaller than the
    // shorter half extent.
    float cornerRadius{};
};
struct Capsule {
    Vec2 first{0, -0.5F};
    Vec2 second{0, 0.5F};
    float radius{0.25F};
};
struct Segment {
    Vec2 first;
    Vec2 second;
};
struct Polygon {
    std::vector<Vec2> vertices; // 3-8 distinct convex hull vertices, in any order.
    float radius{};             // Rounds the outline by growing it by this much (zero: sharp).
};
using Geometry = std::variant<Circle, Box, Capsule, Segment, Polygon>;

struct WorldConfig {
    Vec2 gravity{0, 9.81F}; // +Y down, meters / seconds^2.
    double fixedSeconds{1.0 / 60.0};
    double maxFrameSeconds{0.25};
    unsigned maxStepsPerAdvance{8};
    int subSteps{4};
    bool enableSleep{true};
    bool continuousCollision{true};
};
struct StepResult {
    unsigned steps{};
    double simulatedSeconds{};
    double droppedSeconds{};
    float interpolationAlpha{};
};
enum class EventType { ContactBegin, ContactEnd, ContactHit, SensorBegin, SensorEnd };
struct Event {
    EventType type;
    ShapeHandle first; // Sensor first for sensor events; normal points first -> second.
    ShapeHandle second;
    Vec2 point{};
    Vec2 normal{};
    float approachSpeed{};
    std::uint64_t tick{};
};
struct QueryFilter {
    std::uint64_t categoryBits{1};
    std::uint64_t maskBits{UINT64_MAX};
};
struct RayHit {
    ShapeHandle shape;
    Vec2 point;
    Vec2 normal;
    float fraction{};
};
struct ContactPoint {
    Vec2 point;
    float separation{};
    float normalImpulse{};
};
struct Contact {
    ShapeHandle first;
    ShapeHandle second;
    Vec2 normal;
    std::vector<ContactPoint> points;
};
struct DistanceJointDef {
    BodyHandle first;
    BodyHandle second;
    Vec2 localAnchorFirst{};
    Vec2 localAnchorSecond{};
    float length{1.0F};
    float springHertz{}; // Zero selects a rigid distance constraint.
    float dampingRatio{1.0F};
    bool collideConnected{};
};
struct RevoluteJointDef {
    BodyHandle first;
    BodyHandle second;
    Vec2 localAnchorFirst{};
    Vec2 localAnchorSecond{};
    float referenceAngle{};
    bool enableLimit{};
    float lowerAngle{-1.0F};
    float upperAngle{1.0F};
    bool enableMotor{};
    float motorSpeed{};
    float maxMotorTorque{};
    // A rotational spring that pulls the joint toward `targetAngle` (relative to the reference
    // angle): seesaws that return to level, levers that snap back, doors that close on their own.
    bool enableSpring{};
    float springHertz{2.0F};
    float springDampingRatio{0.5F};
    float targetAngle{};
    bool collideConnected{};
};
struct DebugLine {
    Vec2 first;
    Vec2 second;
    BodyType bodyType;
    bool sensor{};
    bool awake{};
};
struct WorldStats {
    std::size_t bodies{};
    std::size_t shapes{};
    std::size_t joints{};
    int awakeBodies{};
    std::uint64_t ticks{};
};
} // namespace yk::physics

template <class Tag> struct std::hash<yk::physics::Handle<Tag>> {
    std::size_t operator()(const yk::physics::Handle<Tag> &handle) const noexcept {
        return std::hash<std::uint64_t>{}(handle.serial());
    }
};
