#pragma once
#include "yk/assets/AssetSource.hpp"
#include "yk/components/Components.hpp"
#include "yk/graphics/BitmapFont.hpp"
#include "yk/graphics/Renderer.hpp"
#include "yk/runtime/Blackboard.hpp"
#include "yk/scene/Scene.hpp"
#include <map>
#include <memory>
#include <optional>

namespace yk {
// Turns scene data into renderer submissions. It reads components and never changes the scene, so
// the editor (edit scene) and the player (running scene) share it. Each draw call targets the pass
// the caller has opened.
class SceneRenderer {
  public:
    // `assets` resolves texture paths; it may be null when only placeholder shapes are used.
    static Result<std::unique_ptr<SceneRenderer>> create(Renderer &renderer,
                                                         const AssetSource *assets);

    // Sprites of every entity active in the hierarchy, in world units (1 unit = 1 meter).
    Status drawWorld(Renderer &renderer, const Scene &scene);
    // Collider outlines from component data (trigger, static, kinematic, dynamic colors).
    Status drawColliders(Renderer &renderer, const Scene &scene, int layer = 900);
    // Screen-space UiPanel and UiText; `viewport` is the size in pixels of the pass being drawn.
    // `variables` fills {placeholders} in text; may be null.
    Status drawUi(Renderer &renderer, const Scene &scene, Vec2 viewport,
                  const Blackboard *variables);

    const BitmapFont &font() const {
        return *font_;
    }

    // Camera that shows `view` filling `viewport` pixels vertically.
    static Camera2D cameraFor(const CameraView &view, Vec2 viewport);
    // The first enabled primary Camera among active entities, else the first enabled Camera, else
    // null.
    static const Camera *primaryCamera(const Scene &scene);
    // Top-left pixel of a rectangle of `size` placed by `anchor` and `offset` inside `viewport`.
    static Vec2 anchoredTopLeft(UiAnchor anchor, Vec2 viewport, Vec2 size, Vec2 offset);

  private:
    SceneRenderer() = default;
    // Missing or unreadable textures are reported once and drawn as a magenta square.
    std::optional<TextureHandle> textureFor(Renderer &renderer, const std::string &path);

    const AssetSource *assets_{};
    std::unique_ptr<BitmapFont> font_;
    std::map<std::string, std::optional<TextureHandle>> textures_;
};
} // namespace yk
