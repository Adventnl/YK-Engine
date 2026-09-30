#pragma once
#include "core/EditorDocument.hpp"
#include "core/EditorGeometry.hpp"
#include <optional>
#include <string>
#include <vector>

namespace yk::editor {
// The editor's 2D view: which world point is at the middle of the viewport and how many pixels a
// meter covers. Pixel positions are relative to the viewport's top-left corner.
struct ViewCamera {
    static constexpr float minZoom = 2.0F;
    static constexpr float maxZoom = 600.0F;
    static constexpr float actualSize = 48.0F; // Pixels per meter that read as 100%.
    Vec2 center{};
    float zoom{actualSize}; // Pixels per meter.

    Vec2 toWorld(Vec2 pixel, Vec2 viewport) const {
        return center + (pixel - viewport * 0.5F) / zoom;
    }
    Vec2 toScreen(Vec2 world, Vec2 viewport) const {
        return (world - center) * zoom + viewport * 0.5F;
    }
    // The zoom as the percentage the toolbar shows (100 is `actualSize`).
    float percent() const {
        return zoom / actualSize * 100.0F;
    }
    // Zooms by `factor` keeping the world point under `pixel` where it is.
    void zoomAt(Vec2 pixel, Vec2 viewport, float factor);
    // The zoom (pixels per meter) one stop up (`direction` > 0) or down (< 0) a ladder of round
    // percentages, from wherever the wheel left it. The ends stop at the limits.
    float steppedZoom(int direction) const;
    // Centers `area` and zooms so it fills the viewport with `margin` (>1) to spare.
    void frame(Rect area, Vec2 viewport, float margin = 1.25F);
};

struct SnapSettings {
    bool enabled{true};
    float grid{0.5F};   // World units.
    float angle{15.0F}; // Degrees.
};

struct Modifiers {
    bool shift{};
    bool ctrl{};
    bool alt{};
};

enum class Tool { Move, Resize, Rotate };
enum class HandleKind {
    None,
    Body,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Rotate,
    Ghost,
    Pin
};
struct Handle {
    HandleKind kind{HandleKind::None};
    std::size_t index{}; // Which ghost or pin, for HandleKind::Ghost and HandleKind::Pin.
    Vec2 screen;         // Viewport pixels.
};

// Turns pointer input over the scene view into edits of an EditorDocument: click and marquee
// selection, moving, resizing and rotating with snapping, dragging a door's open position, and
// panning and zooming the view. It draws nothing; the UI asks for handles(), marquee() and
// hovered() and paints them. Every drag is one document change, so a single Undo reverts it, and
// cancelDrag() puts everything back.
class SceneInteraction {
  public:
    // With no document (null) every operation is a no-op, so the view can outlive scenes.
    explicit SceneInteraction(EditorDocument *document = nullptr) : document_(document) {}
    // Points the interaction at another document (a scene was opened) or none; the view stays.
    void bind(EditorDocument *document);
    bool bound() const {
        return document_ != nullptr;
    }

    Tool tool{Tool::Move};
    SnapSettings snap;
    ViewCamera camera;
    Vec2 viewport{1280.0F, 720.0F}; // Pixels of the scene view panel.

    Vec2 toWorld(Vec2 pixel) const {
        return camera.toWorld(pixel, viewport);
    }
    Vec2 toScreen(Vec2 world) const {
        return camera.toScreen(world, viewport);
    }
    float metersPerPixel() const {
        return 1.0F / camera.zoom;
    }
    void panBy(Vec2 pixels);
    // The wheel: zooms by `factor` about the pointer.
    void zoomAt(Vec2 pixel, float factor);
    // Buttons and keys: one stop up or down the zoom ladder, and an exact level, about the middle
    // of the view. `percent` is what the toolbar shows (100 is the reset level).
    void zoomStep(int direction);
    void zoomTo(float percent);
    void frameSelection();
    void frameAll();

    // Left button. Positions are viewport pixels.
    void pointerMoved(Vec2 pixel, Modifiers modifiers);
    void pointerPressed(Vec2 pixel, Modifiers modifiers);
    void pointerReleased(Vec2 pixel, Modifiers modifiers);
    // Abandons the current drag and restores the scene (Escape).
    void cancelDrag();
    // Arrow-key nudge of the selection by `direction` grid steps (10x with `large`).
    void nudge(Vec2 direction, bool large);

    bool dragging() const {
        return mode_ != Mode::Idle && (mode_ == Mode::Marquee || started_);
    }
    Handle hovered() const {
        return hovered_;
    }
    EntityId hoveredEntity() const {
        return hoveredEntity_;
    }
    // The rectangle being dragged out, in world units.
    std::optional<Rect> marquee() const;
    // Gizmo handles for the primary selection under the current tool, in viewport pixels.
    std::vector<Handle> handles() const;
    // The topmost entity under `pixel` (null if none): for eyedropper-style reference picking.
    EntityId pickAt(Vec2 pixel) const;

  private:
    enum class Mode { Idle, Press, Marquee, Moving, Resizing, Rotating, Ghost, Pin };
    struct SavedValue {
        std::size_t component{};
        std::string property;
        PropertyValue value;
    };

    Handle hitHandle(Vec2 pixel) const;
    float snapStep(Modifiers modifiers) const; // Zero when snapping is off.
    float markerHalf() const;
    void beginResize(Handle handle);
    void beginRotate();
    void beginGhost(Handle handle);
    void beginPin(Handle handle);
    void updateMove(Vec2 world, Modifiers modifiers);
    void updateResize(Vec2 world, Modifiers modifiers);
    void updateRotate(Vec2 world, Modifiers modifiers);
    void updateGhost(Vec2 world, Modifiers modifiers);
    void updatePin(Vec2 world, Modifiers modifiers);
    void finishSelectionClick(Modifiers modifiers);
    void clearDrag();

    EditorDocument *document_;
    Mode mode_{Mode::Idle};
    bool started_{}; // The press moved far enough to be a drag; the change is open.
    Vec2 pressPixel_;
    Vec2 pressWorld_;
    Modifiers pressModifiers_;
    EntityId pressed_;
    bool pressedWasSelected_{};
    Handle hovered_;
    EntityId hoveredEntity_;
    Vec2 marqueeEnd_;

    struct MoveState {
        std::vector<EntityId> roots;
        std::vector<Vec2> starts;
        std::size_t anchor{};
    } move_;
    struct ResizeState {
        EntityId id;
        HandleKind kind{HandleKind::None};
        Rect local;
        Transform2D world;
        Vec2 grab;
        std::vector<SavedValue> sizes, offsets;
        std::vector<std::pair<EntityId, Vec2>> children; // Their local positions at the start.
    } resize_;
    struct RotateState {
        std::vector<EntityId> roots;
        std::vector<float> starts;
        Vec2 pivot;
        float startAngle{};
        float primaryStart{};
        std::size_t anchor{};
    } rotate_;
    struct GhostState {
        EntityId id;
        std::size_t component{};
        std::string property;
        Vec2 boxCenter;
        Vec2 grab;
    } ghost_;
    struct PinState {
        EntityId id;
        std::size_t component{};
        std::string property;
        Vec2 grab; // Pointer to pin, so the pin does not jump to the pointer.
    } pin_;
};
} // namespace yk::editor
