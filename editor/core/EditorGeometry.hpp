#pragma once
#include "yk/core/Math.hpp"
#include "yk/scene/Scene.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>

// Where things are, for selecting, framing and drawing gizmos. Everything derives from component
// data through the reflection flags (a size, its offset, a displacement), so a new component with a
// `size` field is selectable and resizable with no editor code.
namespace yk::editor {
// A rectangle rotated about its center, in world units.
struct OrientedBox {
    Vec2 center;
    Vec2 half; // Non-negative half extents.
    float rotationDegrees{};

    // Corners in the box's own frame, clockwise on screen from the top-left: TL, TR, BR, BL.
    std::array<Vec2, 4> corners() const;
    bool contains(Vec2 point) const;
    Rect bounds() const; // Axis-aligned bounds of the rotated box.
};

// One extent an entity contributes: a sprite, a collider, anything with a size property.
struct Extent {
    Rect local;     // In the entity's unscaled local space.
    bool visual{};  // A visible sprite (as opposed to a collider outline).
    bool ellipse{}; // Round shapes hit-test as ellipses.
    int layer{};    // Sprite draw layer, for picking the topmost.
    float order{};
    std::string source; // Component type name, for tooltips.
};
std::vector<Extent> extentsOf(const Entity &entity);

// Local (unscaled) bounds that contain every extent; nullopt for entities without any.
std::optional<Rect> localBounds(const Entity &entity);
// The world-space box the resize gizmo wraps: those bounds under the entity's world transform.
std::optional<OrientedBox> gizmoBox(const Entity &entity);
// gizmoBox, or a small marker square for entities that have no extent (spawn logic, groups).
OrientedBox displayBox(const Entity &entity, float markerHalf);

// Entities under `world`, best candidate first: visible sprites by draw order (topmost first), then
// colliders and markers, smallest first. `metersPerPixel` sizes the markers of extent-less
// entities.
std::vector<EntityId> pickAll(const Scene &scene, Vec2 world, float metersPerPixel);
// Entities whose display box touches `area`.
std::vector<EntityId> pickInRect(const Scene &scene, Rect area, float metersPerPixel);
// World bounds of the given entities' display boxes.
std::optional<Rect> boundsOf(const Scene &scene, const std::vector<EntityId> &ids,
                             float metersPerPixel);

// An entity reference held by a component property (a plate's target door).
struct Link {
    EntityId from;
    EntityId to;
    std::string component;
    std::string property;
};
std::vector<Link> linksFrom(const Scene &scene, EntityId from);
std::vector<Link> linksTo(const Scene &scene, EntityId target);
std::vector<Link> allLinks(const Scene &scene);

// Where an entity ends up once a displacement property is applied (a door's open position).
struct Ghost {
    std::size_t componentIndex{};
    std::string property;
    Vec2 displacement;
    OrientedBox box; // The entity's display box shifted by the displacement.
};
std::vector<Ghost> displacementGhosts(const Entity &entity, float markerHalf);

// Rounds to the nearest multiple of `step` (a non-positive step leaves the value alone).
float snapTo(float value, float step);
Vec2 snapTo(Vec2 value, float step);
bool axisAligned(float rotationDegrees); // A multiple of 90 degrees.
} // namespace yk::editor
