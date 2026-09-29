#include "core/EditorGeometry.hpp"
#include "yk/components/Components.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

namespace yk::editor {
namespace {
OrientedBox worldBoxOf(const Rect &local, const Transform2D &world) {
    const Vec2 middle = transformPoint(world, center(local));
    return {middle,
            {std::abs(local.size.x * 0.5F * world.scale.x),
             std::abs(local.size.y * 0.5F * world.scale.y)},
            world.rotationDegrees};
}

bool inside(const OrientedBox &box, Vec2 point, bool ellipse) {
    if (!ellipse)
        return box.contains(point);
    const Vec2 local = rotated(point - box.center, -degreesToRadians(box.rotationDegrees));
    if (box.half.x <= 0.0F || box.half.y <= 0.0F)
        return false;
    const float u = local.x / box.half.x, v = local.y / box.half.y;
    return u * u + v * v <= 1.0F;
}

struct Candidate {
    EntityId id;
    bool visual{};
    int layer{};
    float order{};
    float area{};
    std::size_t sequence{};
};
// Topmost visible sprite first, then the smallest collider or marker; later entities win ties.
bool better(const Candidate &a, const Candidate &b) {
    if (a.visual != b.visual)
        return a.visual;
    if (a.visual) {
        if (a.layer != b.layer)
            return a.layer > b.layer;
        if (a.order != b.order)
            return a.order > b.order;
    } else if (a.area != b.area) {
        return a.area < b.area;
    }
    return a.sequence > b.sequence;
}

void forEachReference(
    const Entity &entity,
    const std::function<void(const Component &, const PropertyInfo &, EntityId)> &visit) {
    for (const auto &component : entity.components()) {
        for (const PropertyInfo &property : component->type().properties) {
            if (property.readOnly)
                continue;
            if (property.type == PropertyType::EntityReference) {
                if (const EntityId target = std::get<EntityId>(property.get(*component)))
                    visit(*component, property, target);
            } else if (property.type == PropertyType::EntityReferenceList) {
                const PropertyValue value = property.get(*component);
                for (const EntityId target : std::get<std::vector<EntityId>>(value))
                    if (target)
                        visit(*component, property, target);
            }
        }
    }
}
} // namespace

std::array<Vec2, 4> OrientedBox::corners() const {
    const float radians = degreesToRadians(rotationDegrees);
    const auto corner = [&](float x, float y) { return center + rotated({x, y}, radians); };
    return {corner(-half.x, -half.y), corner(half.x, -half.y), corner(half.x, half.y),
            corner(-half.x, half.y)};
}

bool OrientedBox::contains(Vec2 point) const {
    const Vec2 local = rotated(point - center, -degreesToRadians(rotationDegrees));
    return std::abs(local.x) <= half.x && std::abs(local.y) <= half.y;
}

Rect OrientedBox::bounds() const {
    const auto points = corners();
    Vec2 low = points[0], high = points[0];
    for (const Vec2 point : points) {
        low = {std::min(low.x, point.x), std::min(low.y, point.y)};
        high = {std::max(high.x, point.x), std::max(high.y, point.y)};
    }
    return {low, high - low};
}

std::vector<Extent> extentsOf(const Entity &entity) {
    std::vector<Extent> extents;
    for (const auto &component : entity.components()) {
        const ComponentType &type = component->type();
        if (type.screenSpace)
            continue;
        const PropertyInfo *sizeProperty = nullptr;
        const PropertyInfo *offsetProperty = nullptr;
        for (const PropertyInfo &property : type.properties) {
            if (property.type != PropertyType::Vec2)
                continue;
            if (property.isSize && !sizeProperty)
                sizeProperty = &property;
            if (property.isOffset && !offsetProperty)
                offsetProperty = &property;
        }
        if (!sizeProperty)
            continue;
        Vec2 size = std::get<Vec2>(sizeProperty->get(*component));
        if (!(size.x > 0.0F && size.y > 0.0F) || !finite(size))
            continue;
        const Vec2 offset =
            offsetProperty ? std::get<Vec2>(offsetProperty->get(*component)) : Vec2{};
        Extent extent;
        extent.source = type.name;
        if (const auto *collider = dynamic_cast<const Collider *>(component.get())) {
            if (collider->shape == ColliderShape::Circle) {
                size.y = size.x; // Circles use the width as their diameter.
                extent.ellipse = true;
            }
        }
        if (const auto *sprite = dynamic_cast<const SpriteRenderer *>(component.get())) {
            extent.visual = sprite->visible;
            extent.ellipse = sprite->shape == SpriteShape::Ellipse;
            extent.layer = sprite->layer;
            extent.order = sprite->order;
        }
        extent.local = {offset - size * 0.5F, size};
        extents.push_back(std::move(extent));
    }
    return extents;
}

std::optional<Rect> localBounds(const Entity &entity) {
    std::optional<Rect> bounds;
    for (const Extent &extent : extentsOf(entity)) {
        if (!bounds) {
            bounds = extent.local;
            continue;
        }
        const Vec2 low{std::min(bounds->position.x, extent.local.position.x),
                       std::min(bounds->position.y, extent.local.position.y)};
        const Vec2 high{std::max(bounds->position.x + bounds->size.x,
                                 extent.local.position.x + extent.local.size.x),
                        std::max(bounds->position.y + bounds->size.y,
                                 extent.local.position.y + extent.local.size.y)};
        bounds = Rect{low, high - low};
    }
    return bounds;
}

std::optional<OrientedBox> gizmoBox(const Entity &entity) {
    const auto bounds = localBounds(entity);
    if (!bounds)
        return std::nullopt;
    return worldBoxOf(*bounds, entity.worldTransform());
}

OrientedBox displayBox(const Entity &entity, float markerHalf) {
    if (auto box = gizmoBox(entity))
        return *box;
    const Transform2D world = entity.worldTransform();
    return {world.position, {markerHalf, markerHalf}, world.rotationDegrees};
}

std::vector<EntityId> pickAll(const Scene &scene, Vec2 world, float metersPerPixel) {
    const float markerHalf = std::max(0.3F, 10.0F * metersPerPixel);
    std::vector<Candidate> found;
    std::size_t sequence = 0;
    for (const EntityId id : scene.hierarchyOrder()) {
        const Entity &entity = *scene.find(id);
        const Transform2D transform = entity.worldTransform();
        std::optional<Candidate> best;
        const auto consider = [&](const Candidate &candidate) {
            if (!best || better(candidate, *best))
                best = candidate;
        };
        const auto extents = extentsOf(entity);
        if (extents.empty()) {
            const OrientedBox marker{transform.position, {markerHalf, markerHalf}, 0.0F};
            if (marker.contains(world))
                consider({id, false, 0, 0.0F, marker.half.x * marker.half.y * 4.0F, sequence});
        }
        for (const Extent &extent : extents) {
            const OrientedBox box = worldBoxOf(extent.local, transform);
            if (inside(box, world, extent.ellipse))
                consider({id, extent.visual, extent.layer, extent.order,
                          box.half.x * box.half.y * 4.0F, sequence});
        }
        if (best)
            found.push_back(*best);
        ++sequence;
    }
    std::sort(found.begin(), found.end(), better);
    std::vector<EntityId> ids;
    ids.reserve(found.size());
    for (const Candidate &candidate : found)
        ids.push_back(candidate.id);
    return ids;
}

std::vector<EntityId> pickInRect(const Scene &scene, Rect area, float metersPerPixel) {
    const float markerHalf = std::max(0.3F, 10.0F * metersPerPixel);
    std::vector<EntityId> ids;
    for (const EntityId id : scene.hierarchyOrder())
        if (overlaps(displayBox(*scene.find(id), markerHalf).bounds(), area))
            ids.push_back(id);
    return ids;
}

std::optional<Rect> boundsOf(const Scene &scene, const std::vector<EntityId> &ids,
                             float metersPerPixel) {
    const float markerHalf = std::max(0.3F, 10.0F * metersPerPixel);
    std::optional<Rect> total;
    for (const EntityId id : ids) {
        const Entity *entity = scene.find(id);
        if (!entity)
            continue;
        const Rect box = displayBox(*entity, markerHalf).bounds();
        if (!total) {
            total = box;
            continue;
        }
        const Vec2 low{std::min(total->position.x, box.position.x),
                       std::min(total->position.y, box.position.y)};
        const Vec2 high{std::max(total->position.x + total->size.x, box.position.x + box.size.x),
                        std::max(total->position.y + total->size.y, box.position.y + box.size.y)};
        total = Rect{low, high - low};
    }
    return total;
}

std::vector<Link> linksFrom(const Scene &scene, EntityId from) {
    std::vector<Link> links;
    const Entity *entity = scene.find(from);
    if (!entity)
        return links;
    forEachReference(
        *entity, [&](const Component &component, const PropertyInfo &property, EntityId target) {
            if (scene.find(target))
                links.push_back({from, target, component.type().name, property.name});
        });
    return links;
}

std::vector<Link> linksTo(const Scene &scene, EntityId target) {
    std::vector<Link> links;
    for (const Link &link : allLinks(scene))
        if (link.to == target)
            links.push_back(link);
    return links;
}

std::vector<Link> allLinks(const Scene &scene) {
    std::vector<Link> links;
    for (const EntityId id : scene.hierarchyOrder())
        for (Link &link : linksFrom(scene, id))
            links.push_back(std::move(link));
    return links;
}

std::vector<Ghost> displacementGhosts(const Entity &entity, float markerHalf) {
    std::vector<Ghost> ghosts;
    const auto &components = entity.components();
    for (std::size_t index = 0; index < components.size(); ++index) {
        for (const PropertyInfo &property : components[index]->type().properties) {
            if (!property.isDisplacement || property.type != PropertyType::Vec2)
                continue;
            const Vec2 displacement = std::get<Vec2>(property.get(*components[index]));
            OrientedBox box = displayBox(entity, markerHalf);
            box.center += displacement;
            ghosts.push_back({index, property.name, displacement, box});
        }
    }
    return ghosts;
}

float snapTo(float value, float step) {
    return step > 0.0F ? std::round(value / step) * step : value;
}
Vec2 snapTo(Vec2 value, float step) {
    return {snapTo(value.x, step), snapTo(value.y, step)};
}
bool axisAligned(float rotationDegrees) {
    return std::abs(std::remainder(rotationDegrees, 90.0F)) < 0.01F;
}
} // namespace yk::editor
