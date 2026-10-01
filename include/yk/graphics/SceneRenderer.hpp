#pragma once
#include "yk/assets/AssetSource.hpp"
#include "yk/components/Components.hpp"
#include "yk/components/Effects.hpp"
#include "yk/graphics/BitmapFont.hpp"
#include "yk/graphics/Renderer.hpp"
#include "yk/runtime/Blackboard.hpp"
#include "yk/scene/Scene.hpp"
#include "yk/world/Tilemap.hpp"
#include "yk/world/WorldLevels.hpp"
#include <map>
#include <memory>
#include <optional>

namespace yk {
// How a world pass is looked at: used to skip what is off screen and to place parallax layers.
struct WorldView {
    Camera2D camera;
    // Pixels of the pass being drawn. A zero size draws everything (no culling, no parallax).
    Vec2 viewport{};
    // Apply SpriteRenderer::parallax. The game view does; the editor's scene view does not, so
    // background pieces stay where the designer put them while the camera pans.
    bool parallax{false};
    // The editor's scene view: entities the designer hid (Entity::editorHidden) are not drawn.
    bool editorView{false};
    // Draw entities where the running game's interpolation puts them `alpha` (0..1) of the way from
    // the previous fixed tick to the current one (GameRuntime::interpolationAlpha), instead of
    // where they are at the current tick. Entities with no interpolation state are drawn as they
    // are, so this is safe for scenes that are not running.
    bool interpolate{false};
    float alpha{1.0F};
    // How strongly each world level is drawn (see levelVisibility); null draws every level fully.
    const std::vector<float> *levelAlpha{nullptr};
    // Simulated seconds, for animated tiles.
    double time{0.0};
};

// What the last drawWorld did, for profilers and tests.
struct SceneRenderStats {
    std::size_t sprites{};   // SpriteRenderer components considered.
    std::size_t culled{};    // Skipped because they were entirely outside the view.
    std::size_t quads{};     // Textured quads submitted (every tile of a tiled sprite counts).
    std::size_t particles{}; // Live particles drawn.
    std::size_t tiles{};     // Tile quads submitted.
    std::size_t tileChunksCulled{};
};

// Turns scene data into renderer submissions. It reads components and never changes the scene, so
// the editor (edit scene) and the player (running scene) share it. Each draw call targets the pass
// the caller has opened.
class SceneRenderer {
  public:
    // `assets` resolves texture paths; it may be null when only placeholder shapes are used.
    // `defaults` apply to textures without a .ykmeta sidecar.
    static Result<std::unique_ptr<SceneRenderer>>
    create(Renderer &renderer, const AssetSource *assets, TextureDefaults defaults = {});

    // Sprites of every entity active in the hierarchy, in world units (1 unit = 1 meter).
    Status drawWorld(Renderer &renderer, const Scene &scene, const WorldView &view = {});
    // Collider outlines from component data (trigger, static, kinematic, dynamic colors).
    Status drawColliders(Renderer &renderer, const Scene &scene, int layer = 900);
    // Screen-space UiPanel and UiText; `viewport` is the size in pixels of the pass being drawn.
    // `variables` fills {placeholders} in text; may be null.
    Status drawUi(Renderer &renderer, const Scene &scene, Vec2 viewport,
                  const Blackboard *variables);

    const BitmapFont &font() const {
        return *font_;
    }
    const SceneRenderStats &stats() const {
        return stats_;
    }
    // A texture the renderer has loaded (loading it first), with its size in pixels, for tools that
    // show it (the editor's asset preview). Empty when the file cannot be loaded.
    struct LoadedTexture {
        TextureHandle handle;
        Vec2 pixels;
    };
    std::optional<LoadedTexture> loadedTexture(Renderer &renderer, const std::string &path);
    // Import settings of a texture (project defaults merged with its sidecar).
    ResolvedTexture textureSettings(const std::string &path);
    // Drops what was loaded for `path` (texture and sidecar) so the next draw reads it again; the
    // editor calls this after the file or its import settings changed.
    void reload(Renderer &renderer, const std::string &path);

    // Camera that shows `view` filling `viewport` pixels vertically.
    static Camera2D cameraFor(const CameraView &view, Vec2 viewport);
    // The first enabled primary Camera among active entities, else the first enabled Camera, else
    // null.
    static const Camera *primaryCamera(const Scene &scene);
    // Top-left pixel of a rectangle of `size` placed by `anchor` and `offset` inside `viewport`.
    static Vec2 anchoredTopLeft(UiAnchor anchor, Vec2 viewport, Vec2 size, Vec2 offset);

  private:
    SceneRenderer() = default;
    struct TextureInfo {
        std::optional<TextureHandle> handle; // Empty when the texture could not be loaded.
        Vec2 pixels{};
        ResolvedTexture settings;
    };
    // Missing or unreadable textures are reported once and drawn as a magenta square.
    const TextureInfo &textureFor(Renderer &renderer, const std::string &path);
    // `world` is where the entity is drawn (its current or its interpolated transform).
    Status drawSprite(Renderer &renderer, const Entity &entity, const Transform2D &world,
                      const SpriteRenderer &sprite, const WorldView &view, const Rect &visible,
                      bool culling, float strength = 1.0F);
    Status drawTilemap(Renderer &renderer, const Entity &entity, const Tilemap &map,
                       const WorldView &view, const Rect &visible, bool culling);
    // A tileset asset, read once per path (null when it cannot be loaded; reported once).
    std::shared_ptr<const Tileset> tilesetFor(const std::string &path);
    Status drawParticles(Renderer &renderer, const Transform2D &world,
                         const ParticleEmitter &emitter, const Rect &visible, bool culling);
    Status drawLight(Renderer &renderer, const Transform2D &world, const Light2D &light,
                     const Rect &visible, bool culling);

    const AssetSource *assets_{};
    TextureDefaults defaults_;
    std::unique_ptr<BitmapFont> font_;
    std::map<std::string, TextureInfo> textures_;
    std::map<std::string, std::shared_ptr<const Tileset>> tilesets_;
    SceneRenderStats stats_;
};
} // namespace yk
