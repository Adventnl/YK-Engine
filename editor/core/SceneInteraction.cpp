#include "core/SceneInteraction.hpp"
#include "yk/components/Components.hpp"
#include <algorithm>
#include <cmath>

namespace yk::editor {
namespace {
constexpr float dragThresholdPixels = 3.0F;
constexpr float handleRadiusPixels = 7.0F;
constexpr float rotateHandleDistancePixels = 30.0F;
constexpr float minimumExtent = 0.05F; // Smallest size a resize drag can produce, local units.

struct EdgeMask {
    float x{}, y{}; // -1: left/top, +1: right/bottom, 0: not affected.
};
EdgeMask maskOf(HandleKind kind) {
    switch (kind) {
    case HandleKind::Left:
        return {-1, 0};
    case HandleKind::Right:
        return {1, 0};
    case HandleKind::Top:
        return {0, -1};
    case HandleKind::Bottom:
        return {0, 1};
    case HandleKind::TopLeft:
        return {-1, -1};
    case HandleKind::TopRight:
        return {1, -1};
    case HandleKind::BottomLeft:
        return {-1, 1};
    case HandleKind::BottomRight:
        return {1, 1};
    default:
        return {};
    }
}
constexpr HandleKind resizeKinds[] = {
    HandleKind::TopLeft,     HandleKind::Top,    HandleKind::TopRight,   HandleKind::Right,
    HandleKind::BottomRight, HandleKind::Bottom, HandleKind::BottomLeft, HandleKind::Left};

float wrapDegrees(float degrees) {
    return std::remainder(degrees, 360.0F);
}
} // namespace

void ViewCamera::zoomAt(Vec2 pixel, Vec2 viewport, float factor) {
    const Vec2 before = toWorld(pixel, viewport);
    zoom = std::clamp(zoom * factor, minZoom, maxZoom);
    center += before - toWorld(pixel, viewport);
}

void ViewCamera::frame(Rect area, Vec2 viewport, float margin) {
    center = yk::center(area);
    const float width = std::max(area.size.x, 1.0F) * margin;
    const float height = std::max(area.size.y, 1.0F) * margin;
    zoom = std::clamp(std::min(viewport.x / width, viewport.y / height), minZoom, maxZoom);
}

void SceneInteraction::bind(EditorDocument &document) {
    cancelDrag();
    document_ = &document;
    hovered_ = {};
    hoveredEntity_ = {};
}

void SceneInteraction::panBy(Vec2 pixels) {
    camera.center -= pixels / camera.zoom;
}

void SceneInteraction::zoomAt(Vec2 pixel, float factor) {
    camera.zoomAt(pixel, viewport, factor);
}

void SceneInteraction::frameSelection() {
    auto area = boundsOf(document_->scene(), document_->selection(), metersPerPixel());
    if (!area)
        return;
    // A lone small object should not fill the whole view.
    const Vec2 size{std::max(area->size.x, 8.0F), std::max(area->size.y, 8.0F)};
    camera.frame({yk::center(*area) - size * 0.5F, size}, viewport, 1.25F);
}

void SceneInteraction::frameAll() {
    const Scene &scene = document_->scene();
    auto area = boundsOf(scene, scene.hierarchyOrder(), metersPerPixel());
    if (!area) {
        camera.center = {};
        return;
    }
    camera.frame(*area, viewport, 1.15F);
}

float SceneInteraction::markerHalf() const {
    return std::max(0.3F, 10.0F * metersPerPixel());
}

std::vector<Handle> SceneInteraction::handles() const {
    std::vector<Handle> result;
    const Scene &scene = document_->scene();
    const Entity *entity = scene.find(document_->primary());
    if (!entity)
        return result;
    const float half = markerHalf();
    std::size_t index = 0;
    for (const Ghost &ghost : displacementGhosts(*entity, half))
        result.push_back({HandleKind::Ghost, index++, toScreen(ghost.box.center)});
    if (tool == Tool::Resize) {
        if (const auto box = gizmoBox(*entity)) {
            const float radians = degreesToRadians(box->rotationDegrees);
            for (const HandleKind kind : resizeKinds) {
                const EdgeMask mask = maskOf(kind);
                const Vec2 world =
                    box->center + rotated({mask.x * box->half.x, mask.y * box->half.y}, radians);
                result.push_back({kind, 0, toScreen(world)});
            }
        }
    } else if (tool == Tool::Rotate) {
        const OrientedBox box = displayBox(*entity, half);
        const float radians = degreesToRadians(box.rotationDegrees);
        const Vec2 top = box.center + rotated({0.0F, -box.half.y}, radians);
        const Vec2 world =
            top + rotated({0.0F, -1.0F}, radians) * (rotateHandleDistancePixels * metersPerPixel());
        result.push_back({HandleKind::Rotate, 0, toScreen(world)});
    }
    return result;
}

Handle SceneInteraction::hitHandle(Vec2 pixel) const {
    Handle best;
    float bestDistance = 1.0e9F;
    float radius = handleRadiusPixels;
    if (tool == Tool::Resize) {
        // Small objects: shrink the handles so their bodies stay grabbable.
        if (const Entity *entity = document_->scene().find(document_->primary()))
            if (const auto box = gizmoBox(*entity))
                radius = std::clamp(std::min(box->half.x, box->half.y) * camera.zoom * 2.0F / 3.0F,
                                    3.0F, handleRadiusPixels);
    }
    for (const Handle &handle : handles()) {
        const float reach = handle.kind == HandleKind::Ghost || handle.kind == HandleKind::Rotate
                                ? handleRadiusPixels + 2.0F
                                : radius;
        const float gap = distance(handle.screen, pixel);
        if (gap <= reach && gap < bestDistance) {
            best = handle;
            bestDistance = gap;
        }
    }
    return best;
}

EntityId SceneInteraction::pickAt(Vec2 pixel) const {
    const auto stack = pickAll(document_->scene(), toWorld(pixel), metersPerPixel());
    return stack.empty() ? EntityId{} : stack.front();
}

float SceneInteraction::snapStep(Modifiers modifiers) const {
    return snap.enabled != modifiers.ctrl ? snap.grid : 0.0F;
}

std::optional<Rect> SceneInteraction::marquee() const {
    if (mode_ != Mode::Marquee)
        return std::nullopt;
    const Vec2 low{std::min(pressWorld_.x, marqueeEnd_.x), std::min(pressWorld_.y, marqueeEnd_.y)};
    const Vec2 high{std::max(pressWorld_.x, marqueeEnd_.x), std::max(pressWorld_.y, marqueeEnd_.y)};
    return Rect{low, high - low};
}

void SceneInteraction::pointerPressed(Vec2 pixel, Modifiers modifiers) {
    if (mode_ != Mode::Idle)
        return;
    pressPixel_ = pixel;
    pressWorld_ = toWorld(pixel);
    pressModifiers_ = modifiers;
    started_ = false;
    pressed_ = {};
    pressedWasSelected_ = false;

    const Handle handle = hitHandle(pixel);
    switch (handle.kind) {
    case HandleKind::Ghost:
        beginGhost(handle);
        if (ghost_.id) {
            mode_ = Mode::Ghost;
            return;
        }
        break;
    case HandleKind::Rotate:
        beginRotate();
        mode_ = Mode::Rotating;
        return;
    case HandleKind::None:
    case HandleKind::Body:
        break;
    default:
        beginResize(handle);
        if (resize_.id) {
            mode_ = Mode::Resizing;
            return;
        }
        break;
    }

    const Scene &scene = document_->scene();
    const auto stack = pickAll(scene, pressWorld_, metersPerPixel());
    EntityId hit;
    if (!stack.empty()) {
        hit = stack.front();
        if (modifiers.alt && stack.size() > 1) {
            // Alt-click walks down the stack of things under the cursor.
            const auto current = std::find(stack.begin(), stack.end(), document_->primary());
            if (current != stack.end())
                hit = stack[static_cast<std::size_t>(std::distance(stack.begin(), current) + 1) %
                            stack.size()];
        }
    }
    if (!hit) {
        mode_ = Mode::Marquee;
        marqueeEnd_ = pressWorld_;
        return;
    }
    pressed_ = hit;
    pressedWasSelected_ = document_->isSelected(hit);
    if (!pressedWasSelected_)
        document_->select(hit, modifiers.ctrl    ? SelectMode::Toggle
                               : modifiers.shift ? SelectMode::Add
                                                 : SelectMode::Replace);
    move_ = {};
    move_.roots = document_->selectionRoots();
    for (std::size_t i = 0; i < move_.roots.size(); ++i) {
        move_.starts.push_back(scene.find(move_.roots[i])->worldPosition());
        if (move_.roots[i] == hit || scene.isAncestor(move_.roots[i], hit))
            move_.anchor = i;
    }
    mode_ = Mode::Moving;
}

void SceneInteraction::pointerMoved(Vec2 pixel, Modifiers modifiers) {
    if (mode_ == Mode::Idle) {
        hovered_ = hitHandle(pixel);
        hoveredEntity_ = hovered_.kind == HandleKind::None ? pickAt(pixel) : EntityId{};
        return;
    }
    const Vec2 world = toWorld(pixel);
    if (mode_ == Mode::Marquee) {
        marqueeEnd_ = world;
        return;
    }
    if (!started_) {
        if (distance(pixel, pressPixel_) < dragThresholdPixels)
            return;
        started_ = true;
        switch (mode_) {
        case Mode::Moving:
            document_->beginChange("Move");
            break;
        case Mode::Resizing:
            document_->beginChange("Resize");
            break;
        case Mode::Rotating:
            document_->beginChange("Rotate");
            break;
        default:
            document_->beginChange("Move Target");
            break;
        }
    }
    switch (mode_) {
    case Mode::Moving:
        updateMove(world, modifiers);
        break;
    case Mode::Resizing:
        updateResize(world, modifiers);
        break;
    case Mode::Rotating:
        updateRotate(world, modifiers);
        break;
    case Mode::Ghost:
        updateGhost(world, modifiers);
        break;
    default:
        break;
    }
}

void SceneInteraction::pointerReleased(Vec2 pixel, Modifiers modifiers) {
    switch (mode_) {
    case Mode::Idle:
        return;
    case Mode::Marquee: {
        const bool additive = modifiers.shift || modifiers.ctrl;
        if (distance(pixel, pressPixel_) < dragThresholdPixels) {
            if (!additive)
                document_->clearSelection(); // A click on nothing.
        } else if (const auto area = marquee()) {
            document_->select(pickInRect(document_->scene(), *area, metersPerPixel()),
                              additive ? SelectMode::Add : SelectMode::Replace);
        }
        break;
    }
    case Mode::Moving:
        if (started_)
            document_->endChange();
        else
            finishSelectionClick(modifiers);
        break;
    default:
        if (started_)
            document_->endChange();
        break;
    }
    clearDrag();
    hovered_ = hitHandle(pixel);
    hoveredEntity_ = hovered_.kind == HandleKind::None ? pickAt(pixel) : EntityId{};
}

void SceneInteraction::finishSelectionClick(Modifiers modifiers) {
    if (!pressed_ || !pressedWasSelected_)
        return;
    if (modifiers.ctrl)
        document_->select(pressed_, SelectMode::Toggle);
    else if (!modifiers.shift)
        document_->select(pressed_, SelectMode::Replace);
}

void SceneInteraction::cancelDrag() {
    if (started_ && document_->inChange())
        document_->cancelChange();
    clearDrag();
}

void SceneInteraction::clearDrag() {
    mode_ = Mode::Idle;
    started_ = false;
    pressed_ = {};
    pressedWasSelected_ = false;
    move_ = {};
    resize_ = {};
    rotate_ = {};
    ghost_ = {};
}

void SceneInteraction::nudge(Vec2 direction, bool large) {
    const float step = (snap.enabled ? snap.grid : 0.1F) * (large ? 10.0F : 1.0F);
    const auto roots = document_->selectionRoots();
    if (roots.empty())
        return;
    document_->change("Nudge", [&](Scene &scene) {
        for (const EntityId id : roots)
            if (Entity *entity = scene.find(id))
                entity->setWorldPosition(entity->worldPosition() + direction * step);
    });
}

void SceneInteraction::updateMove(Vec2 world, Modifiers modifiers) {
    Scene &scene = document_->edit();
    Vec2 delta = world - pressWorld_;
    if (modifiers.shift) { // Constrain to the dominant axis.
        if (std::abs(delta.x) >= std::abs(delta.y))
            delta.y = 0.0F;
        else
            delta.x = 0.0F;
    }
    const float step = snapStep(modifiers);
    if (step > 0.0F && move_.anchor < move_.starts.size()) {
        const Vec2 origin = move_.starts[move_.anchor];
        delta = snapTo(origin + delta, step) - origin; // The grabbed object lands on the grid.
    }
    for (std::size_t i = 0; i < move_.roots.size(); ++i)
        if (Entity *entity = scene.find(move_.roots[i]))
            entity->setWorldPosition(move_.starts[i] + delta);
}

void SceneInteraction::beginResize(Handle handle) {
    resize_ = {};
    const Scene &scene = document_->scene();
    const Entity *entity = scene.find(document_->primary());
    if (!entity)
        return;
    const auto bounds = localBounds(*entity);
    if (!bounds)
        return;
    resize_.id = entity->id();
    resize_.kind = handle.kind;
    resize_.local = *bounds;
    resize_.world = entity->worldTransform();
    resize_.grab = toWorld(handle.screen) - pressWorld_;
    for (const EntityId child : entity->childIds())
        resize_.children.emplace_back(child, scene.find(child)->transform().position);
    const auto &components = entity->components();
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (components[i]->type().screenSpace)
            continue;
        for (const PropertyInfo &property : components[i]->type().properties) {
            if (property.type != PropertyType::Vec2 || (!property.isSize && !property.isOffset))
                continue;
            SavedValue saved{i, property.name, property.get(*components[i])};
            (property.isSize ? resize_.sizes : resize_.offsets).push_back(std::move(saved));
        }
    }
}

void SceneInteraction::updateResize(Vec2 world, Modifiers modifiers) {
    Scene &scene = document_->edit();
    Entity *entity = scene.find(resize_.id);
    if (!entity)
        return;
    const Transform2D &transform = resize_.world;
    const float step = snapStep(modifiers);
    const bool aligned = axisAligned(transform.rotationDegrees);
    Vec2 pointer = world + resize_.grab;
    if (aligned)
        pointer = snapTo(pointer, step); // The dragged edge lands on the world grid.
    Vec2 local = inverseTransformPoint(transform, pointer);
    if (!aligned)
        local = snapTo(local, step);

    // A mirrored entity shows its local left edge on the right; map the handle to local edges.
    const EdgeMask visual = maskOf(resize_.kind);
    const float sideX = transform.scale.x < 0.0F ? -visual.x : visual.x;
    const float sideY = transform.scale.y < 0.0F ? -visual.y : visual.y;

    const Rect &box = resize_.local;
    struct Axis {
        float low, high, side, pointer;
    };
    const auto factorOf = [&](const Axis &axis, float &anchor) {
        const float size = axis.high - axis.low;
        const float middle = (axis.low + axis.high) * 0.5F;
        anchor = middle;
        if (axis.side == 0.0F)
            return 1.0F;
        float newSize;
        if (modifiers.alt) {
            const float reach = axis.side > 0 ? axis.pointer - middle : middle - axis.pointer;
            newSize = std::max(reach * 2.0F, minimumExtent);
        } else if (axis.side > 0) {
            newSize = std::max(axis.pointer, axis.low + minimumExtent) - axis.low;
            anchor = axis.low;
        } else {
            newSize = axis.high - std::min(axis.pointer, axis.high - minimumExtent);
            anchor = axis.high;
        }
        return newSize / size;
    };
    float anchorX = 0.0F, anchorY = 0.0F;
    float factorX =
        factorOf({box.position.x, box.position.x + box.size.x, sideX, local.x}, anchorX);
    float factorY =
        factorOf({box.position.y, box.position.y + box.size.y, sideY, local.y}, anchorY);
    if (modifiers.shift && sideX != 0.0F && sideY != 0.0F)
        factorX = factorY = std::max(factorX, factorY); // Keep the proportions.

    // Sizes and offsets scale about the entity's origin, and the origin itself moves with the
    // scaled geometry (it keeps its proportional place in the box, like a sprite's pivot). Children
    // stay where they are in the world.
    const Vec2 shift{anchorX * (1.0F - factorX), anchorY * (1.0F - factorY)};
    for (const SavedValue &saved : resize_.sizes) {
        Component &component = *entity->components()[saved.component];
        const PropertyInfo *property = component.type().find(saved.property);
        if (!property)
            continue;
        Vec2 size = std::get<Vec2>(saved.value);
        const auto *collider = dynamic_cast<const Collider *>(&component);
        if (collider && collider->shape == ColliderShape::Circle) {
            const float factor = factorX != 1.0F ? factorX : factorY; // A circle stays round.
            size = {size.x * factor, size.x * factor};
        } else {
            size = {size.x * factorX, size.y * factorY};
        }
        property->assign(component, size);
    }
    for (const SavedValue &saved : resize_.offsets) {
        Component &component = *entity->components()[saved.component];
        const PropertyInfo *property = component.type().find(saved.property);
        if (!property)
            continue;
        const Vec2 offset = std::get<Vec2>(saved.value);
        property->assign(component, Vec2{offset.x * factorX, offset.y * factorY});
    }
    entity->setWorldPosition(
        transform.position +
        rotated(hadamard(transform.scale, shift), degreesToRadians(transform.rotationDegrees)));
    for (const auto &[id, position] : resize_.children)
        if (Entity *child = scene.find(id))
            child->transform().position = position - shift;
}

void SceneInteraction::beginRotate() {
    rotate_ = {};
    const Scene &scene = document_->scene();
    const Entity *primary = scene.find(document_->primary());
    if (!primary)
        return;
    rotate_.roots = document_->selectionRoots();
    for (const EntityId id : rotate_.roots)
        rotate_.starts.push_back(scene.find(id)->worldTransform().rotationDegrees);
    rotate_.pivot = primary->worldPosition();
    rotate_.primaryStart = primary->worldTransform().rotationDegrees;
    const Vec2 arm = pressWorld_ - rotate_.pivot;
    rotate_.startAngle = radiansToDegrees(std::atan2(arm.y, arm.x));
}

void SceneInteraction::updateRotate(Vec2 world, Modifiers modifiers) {
    Scene &scene = document_->edit();
    const Vec2 arm = world - rotate_.pivot;
    if (lengthSquared(arm) < 1.0e-8F)
        return;
    float delta = wrapDegrees(radiansToDegrees(std::atan2(arm.y, arm.x)) - rotate_.startAngle);
    if (snap.enabled != modifiers.ctrl && snap.angle > 0.0F)
        delta = snapTo(rotate_.primaryStart + delta, snap.angle) - rotate_.primaryStart;
    for (std::size_t i = 0; i < rotate_.roots.size(); ++i) {
        Entity *entity = scene.find(rotate_.roots[i]);
        if (!entity)
            continue;
        Transform2D transform = entity->worldTransform();
        transform.rotationDegrees = wrapDegrees(rotate_.starts[i] + delta);
        entity->setWorldTransform(transform);
    }
}

void SceneInteraction::beginGhost(Handle handle) {
    ghost_ = {};
    const Entity *entity = document_->scene().find(document_->primary());
    if (!entity)
        return;
    const auto ghosts = displacementGhosts(*entity, markerHalf());
    if (handle.index >= ghosts.size())
        return;
    const Ghost &ghost = ghosts[handle.index];
    ghost_.id = entity->id();
    ghost_.component = ghost.componentIndex;
    ghost_.property = ghost.property;
    ghost_.boxCenter = displayBox(*entity, markerHalf()).center;
    ghost_.grab = ghost.box.center - pressWorld_;
}

void SceneInteraction::updateGhost(Vec2 world, Modifiers modifiers) {
    Scene &scene = document_->edit();
    Entity *entity = scene.find(ghost_.id);
    if (!entity || ghost_.component >= entity->components().size())
        return;
    Component &component = *entity->components()[ghost_.component];
    const PropertyInfo *property = component.type().find(ghost_.property);
    if (!property)
        return;
    const Vec2 displacement = snapTo(world + ghost_.grab - ghost_.boxCenter, snapStep(modifiers));
    property->assign(component, displacement);
}
} // namespace yk::editor
