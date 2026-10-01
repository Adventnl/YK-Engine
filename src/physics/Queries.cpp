#include "WorldImpl.hpp"
#include <algorithm>

namespace yk::physics {
namespace {
b2AABB geometryBounds(b2ShapeId id) {
    const auto transform = b2Body_GetTransform(b2Shape_GetBody(id));
    switch (b2Shape_GetType(id)) {
    case b2_circleShape: {
        const auto circle = b2Shape_GetCircle(id);
        return b2ComputeCircleAABB(&circle, transform);
    }
    case b2_capsuleShape: {
        const auto capsule = b2Shape_GetCapsule(id);
        return b2ComputeCapsuleAABB(&capsule, transform);
    }
    case b2_polygonShape: {
        const auto polygon = b2Shape_GetPolygon(id);
        return b2ComputePolygonAABB(&polygon, transform);
    }
    case b2_segmentShape: {
        const auto segment = b2Shape_GetSegment(id);
        return b2ComputeSegmentAABB(&segment, transform);
    }
    default:
        return b2Shape_GetAABB(id);
    }
}
} // namespace
Result<std::optional<RayHit>> World::rayCast(Vec2 origin, Vec2 translation,
                                             QueryFilter filter) const {
    impl_->assertThread();
    if (!bounded(origin) || !bounded(translation) || !bounded(origin + translation))
        return Error{"Invalid ray origin, translation, or endpoint"};
    if (translation.x == 0 && translation.y == 0)
        return std::optional<RayHit>{};
    if (filter.level == allLevels) {
        const auto hit = b2World_CastRayClosest(impl_->world, native(origin), native(translation),
                                                native(filter));
        if (!hit.hit)
            return std::optional<RayHit>{};
        return std::optional<RayHit>{RayHit{impl_->shapeHandle(hit.shapeId), vector(hit.point),
                                            vector(hit.normal), hit.fraction}};
    }
    // A ray on one level ignores the shapes of the others: take the closest of the rest.
    struct Context {
        const Impl *impl;
        int level;
        std::optional<RayHit> best;
    } context{impl_.get(), filter.level, std::nullopt};
    b2World_CastRay(
        impl_->world, native(origin), native(translation), native(filter),
        [](b2ShapeId id, b2Vec2 point, b2Vec2 normal, float fraction, void *raw) {
            auto &query = *static_cast<Context *>(raw);
            if (!Impl::levelsMeet(query.impl->levelOf(id), query.level))
                return -1.0F; // Not on this level: the ray goes on through it.
            query.best =
                RayHit{query.impl->shapeHandle(id), vector(point), vector(normal), fraction};
            return fraction; // Clip the ray here: only something closer matters now.
        },
        &context);
    return std::move(context.best);
}
Result<std::vector<ShapeHandle>> World::queryAabb(Rect bounds, QueryFilter filter) const {
    impl_->assertThread();
    if (!bounded(bounds.position) || !bounded(bounds.size) || bounds.size.x < 0 ||
        bounds.size.y < 0 || !bounded(bounds.position + bounds.size))
        return Error{"Invalid query bounds"};
    struct Context {
        const Impl *impl;
        b2AABB bounds;
        int level;
        std::vector<ShapeHandle> results;
    } context{impl_.get(),
              {native(bounds.position), native(bounds.position + bounds.size)},
              filter.level,
              {}};
    context.results.reserve(impl_->shapes.size());
    b2World_OverlapAABB(
        impl_->world, context.bounds, native(filter),
        [](b2ShapeId id, void *rawContext) {
            auto &query = *static_cast<Context *>(rawContext);
            if (!Impl::levelsMeet(query.impl->levelOf(id), query.level))
                return true;
            // The broad phase uses enlarged proxies. Exclude their padding from public results.
            const auto actual = geometryBounds(id);
            if (actual.lowerBound.x <= query.bounds.upperBound.x &&
                actual.upperBound.x >= query.bounds.lowerBound.x &&
                actual.lowerBound.y <= query.bounds.upperBound.y &&
                actual.upperBound.y >= query.bounds.lowerBound.y)
                query.results.push_back(query.impl->shapeHandle(id));
            return true;
        },
        &context);
    std::sort(context.results.begin(), context.results.end(),
              [](ShapeHandle first, ShapeHandle second) { return first.serial_ < second.serial_; });
    return std::move(context.results);
}
Result<std::vector<ShapeHandle>> World::queryPoint(Vec2 point, QueryFilter filter) const {
    auto candidates = queryAabb({point, {}}, filter);
    if (!candidates)
        return Error{candidates.error()};
    std::erase_if(candidates.value(), [&](ShapeHandle shape) {
        return !b2Shape_TestPoint(impl_->shapes.at(shape.serial_).nativeId, native(point));
    });
    return std::move(candidates.value());
}
Result<std::vector<Contact>> World::contacts(BodyHandle body) const {
    if (!valid(body))
        return Error{"Invalid, destroyed, or foreign body handle"};
    const auto id = impl_->bodies.at(body.serial_).nativeId;
    std::vector<b2ContactData> data(static_cast<std::size_t>(b2Body_GetContactCapacity(id)));
    const int count = b2Body_GetContactData(id, data.data(), static_cast<int>(data.size()));
    std::vector<Contact> result;
    result.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const auto &contact = data[static_cast<std::size_t>(i)];
        Contact value{impl_->shapeHandle(contact.shapeIdA),
                      impl_->shapeHandle(contact.shapeIdB),
                      vector(contact.manifold.normal),
                      {}};
        for (int j = 0; j < contact.manifold.pointCount; ++j) {
            const auto &point = contact.manifold.points[j];
            value.points.push_back({vector(point.point), point.separation, point.normalImpulse});
        }
        result.push_back(std::move(value));
    }
    return result;
}
Result<std::vector<ShapeHandle>> World::sensorOverlaps(ShapeHandle sensor) const {
    if (!valid(sensor))
        return Error{"Invalid, destroyed, or foreign sensor handle"};
    const auto id = impl_->shapes.at(sensor.serial_).nativeId;
    if (!b2Shape_IsSensor(id))
        return Error{"Overlap query requires a sensor shape"};
    if (!b2Body_IsEnabled(b2Shape_GetBody(id)))
        return std::vector<ShapeHandle>{};
    std::vector<b2ShapeId> data(static_cast<std::size_t>(b2Shape_GetSensorCapacity(id)));
    const int count = b2Shape_GetSensorOverlaps(id, data.data(), static_cast<int>(data.size()));
    std::vector<ShapeHandle> result;
    result.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const auto shape = impl_->shapeHandle(data[static_cast<std::size_t>(i)]);
        if (valid(shape) &&
            b2Body_IsEnabled(b2Shape_GetBody(impl_->shapes.at(shape.serial_).nativeId)))
            result.push_back(shape);
    }
    std::sort(result.begin(), result.end(),
              [](ShapeHandle first, ShapeHandle second) { return first.serial_ < second.serial_; });
    return result;
}
Result<std::vector<DebugLine>> World::debugLines(unsigned circleSegments) const {
    impl_->assertThread();
    if (circleSegments < 8 || circleSegments > 128)
        return Error{"Debug circle segments must be between 8 and 128"};
    std::vector<std::uint64_t> order;
    order.reserve(impl_->shapes.size());
    for (const auto &[serial, shape] : impl_->shapes) {
        (void)shape;
        order.push_back(serial);
    }
    std::sort(order.begin(), order.end());
    std::vector<DebugLine> result;
    std::vector<Vec2> points;
    points.reserve(circleSegments + 2);
    for (const auto serial : order) {
        const auto &shape = impl_->shapes.at(serial);
        const auto body = impl_->bodies.at(shape.body.serial_).nativeId;
        if (!b2Body_IsEnabled(body))
            continue;
        const auto transform = b2Body_GetTransform(body);
        points.clear();
        const auto append = [&](b2Vec2 local) {
            points.push_back(vector(b2TransformPoint(transform, local)));
        };
        const auto arc = [&](b2Vec2 center, float radius, float start, float end,
                             unsigned segments) {
            for (unsigned i = 0; i <= segments; ++i) {
                const float angle =
                    start + (end - start) * static_cast<float>(i) / static_cast<float>(segments);
                append({center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)});
            }
        };
        bool closed = true;
        switch (b2Shape_GetType(shape.nativeId)) {
        case b2_circleShape: {
            const auto circle = b2Shape_GetCircle(shape.nativeId);
            arc(circle.center, circle.radius, 0, 2 * pi, circleSegments);
            points.pop_back();
            break;
        }
        case b2_capsuleShape: {
            const auto capsule = b2Shape_GetCapsule(shape.nativeId);
            const float angle = std::atan2(capsule.center2.y - capsule.center1.y,
                                           capsule.center2.x - capsule.center1.x);
            arc(capsule.center1, capsule.radius, angle + pi / 2, angle + 3 * pi / 2,
                circleSegments / 2);
            arc(capsule.center2, capsule.radius, angle - pi / 2, angle + pi / 2,
                circleSegments / 2);
            break;
        }
        case b2_polygonShape: {
            const auto polygon = b2Shape_GetPolygon(shape.nativeId);
            for (int i = 0; i < polygon.count; ++i)
                append(polygon.vertices[i]);
            break;
        }
        case b2_segmentShape: {
            const auto segment = b2Shape_GetSegment(shape.nativeId);
            append(segment.point1);
            append(segment.point2);
            closed = false;
            break;
        }
        default:
            break;
        }
        if (points.size() < 2)
            continue;
        const auto count = closed ? points.size() : points.size() - 1;
        for (std::size_t i = 0; i < count; ++i)
            result.push_back({points[i], points[(i + 1) % points.size()], bodyType(body),
                              b2Shape_IsSensor(shape.nativeId), b2Body_IsAwake(body)});
    }
    return result;
}
} // namespace yk::physics
