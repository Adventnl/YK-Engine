#pragma once
#include "yk/physics/World.hpp"
#include <box2d/box2d.h>
#include <cassert>
#include <thread>
#include <unordered_map>

namespace yk::physics {
inline b2Vec2 native(Vec2 value) {
    return {value.x, value.y};
}
inline Vec2 vector(b2Vec2 value) {
    return {value.x, value.y};
}
inline Pose poseOf(b2BodyId id) {
    return {vector(b2Body_GetPosition(id)), b2Rot_GetAngle(b2Body_GetRotation(id))};
}
// Reject extremes before arithmetic reaches the solver. Normal geometry is meter-scale.
inline bool bounded(float value) {
    return std::isfinite(value) && std::abs(value) <= 1000000.0F;
}
inline bool bounded(Vec2 value) {
    return bounded(value.x) && bounded(value.y);
}
inline bool nonnegative(float value) {
    return bounded(value) && value >= 0;
}
inline bool validFilter(CollisionFilter filter) {
    return filter.groupIndex >= -32768 && filter.groupIndex <= 32767;
}
inline b2Filter native(CollisionFilter filter) {
    return {filter.categoryBits, filter.maskBits, filter.groupIndex};
}
inline b2QueryFilter native(QueryFilter filter) {
    return {filter.categoryBits, filter.maskBits};
}
inline BodyType bodyType(b2BodyId id) {
    switch (b2Body_GetType(id)) {
    case b2_staticBody:
        return BodyType::Static;
    case b2_kinematicBody:
        return BodyType::Kinematic;
    case b2_dynamicBody:
        return BodyType::Dynamic;
    default:
        return BodyType::Static;
    }
}
struct World::Impl {
    struct Body {
        b2BodyId nativeId;
        Pose previous;
        std::vector<ShapeHandle> shapes;
        std::vector<JointHandle> joints;
    };
    struct Shape {
        b2ShapeId nativeId;
        BodyHandle body;
        std::uint64_t creationTick;
    };
    struct Joint {
        b2JointId nativeId;
        BodyHandle first;
        BodyHandle second;
    };
    const std::thread::id thread = std::this_thread::get_id();
    std::shared_ptr<const void> identity = std::make_shared<const int>(0);
    b2WorldId world{};
    WorldConfig config;
    std::uint64_t serial{};
    std::uint64_t ticks{};
    double accumulator{};
    float alpha{};
    std::unordered_map<std::uint64_t, Body> bodies;
    std::unordered_map<std::uint64_t, Shape> shapes;
    std::unordered_map<std::uint64_t, Joint> joints;
    std::unordered_map<std::uint64_t, ShapeHandle> nativeShapes;
    // One-way shapes: native shape id -> body-local direction of the solid side (unit length).
    std::unordered_map<std::uint64_t, Vec2> oneWayShapes;
    std::vector<std::uint64_t> retiredShapes;
    std::vector<Event> events;

    ~Impl() {
        assertThread();
        if (B2_IS_NON_NULL(world))
            b2DestroyWorld(world);
    }
    void assertThread() const {
        assert(thread == std::this_thread::get_id());
    }
    template <class T> bool owns(Handle<T> handle) const {
        assertThread();
        return handle.serial_ != 0 && handle.owner_.lock() == identity;
    }
    template <class T> Handle<T> handle() {
        Handle<T> result;
        result.owner_ = identity;
        result.serial_ = ++serial;
        return result;
    }
    ShapeHandle shapeHandle(b2ShapeId id) const {
        const auto it = nativeShapes.find(b2StoreShapeId(id));
        return it == nativeShapes.end() ? ShapeHandle{} : it->second;
    }
    void retireShape(const Shape &shape) {
        const auto id = b2StoreShapeId(shape.nativeId);
        // Unstepped shapes have no native contacts or sensor history to preserve.
        if (shape.creationTick == ticks)
            nativeShapes.erase(id);
        else
            retiredShapes.push_back(id);
    }
    void collectEvents();
    // Pre-solve decision for contacts involving one-way shapes (see ShapeDef::oneWay). True keeps
    // the contact, false disables it for this step. Read-only: the solver is running.
    bool preSolve(b2ShapeId shapeA, b2ShapeId shapeB, const b2Manifold &manifold) const;
};
} // namespace yk::physics
