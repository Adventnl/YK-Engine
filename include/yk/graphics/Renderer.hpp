#pragma once
#include "yk/core/Color.hpp"
#include "yk/core/Result.hpp"
#include "yk/graphics/Camera2D.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
namespace yk {
class Application;
class Renderer;
class TextureHandle {
  public:
    TextureHandle() = default;
    bool operator==(const TextureHandle &rhs) const {
        return index_ == rhs.index_ && !owner_.owner_before(rhs.owner_) &&
               !rhs.owner_.owner_before(owner_);
    }

  private:
    friend class Renderer;
    std::weak_ptr<const void> owner_;
    std::size_t index_{};
};
enum class TextureFilter { Nearest, Linear };
// Procedural textures every renderer can provide, so placeholder art needs no files.
enum class BuiltinTexture { White, Circle };
struct Sprite {
    TextureHandle texture;
    Transform2D transform;
    Vec2 size;               // World units, independent of texture dimensions.
    Vec2 anchor{0.5F, 0.5F}; // Fractional pivot used for positioning and rotation.
    Color tint{255, 255, 255, 255};
    // Pixel-space atlas region. Empty draws the complete texture.
    std::optional<Rect> source;
    bool flipHorizontal{};
    int layer{};
    float depth{}; // Ascending depth, then submission order within each layer.
};
// A rectangle of the frame drawn with its own camera. Passes let one frame hold several views (the
// editor's scene and game panels, a HUD drawn in screen space) without a second renderer.
struct RenderPass {
    Camera2D camera;
    std::optional<Rect>
        viewport;               // Pixels of the render output (or logical units in letterbox mode).
    std::optional<Color> clear; // Fills the viewport before drawing.
    // Draws into a texture from createRenderTarget instead of the frame. The viewport is then in
    // texture pixels and defaults to the whole texture.
    std::optional<TextureHandle> target{};
};

class Renderer {
  public:
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    Result<TextureHandle> createTexture(int width, int height, std::span<const Color> pixels,
                                        TextureFilter filter = TextureFilter::Nearest);
    Result<TextureHandle> builtinTexture(BuiltinTexture kind);
    // A texture that passes can render into (linear filtered, alpha blended).
    Result<TextureHandle> createRenderTarget(int width, int height);
    // Backend texture, for tools that display a texture through their own UI layer. Null if
    // invalid.
    SDL_Texture *nativeTexture(TextureHandle texture) const;
    Vec2 textureSize(TextureHandle texture) const; // Pixels; zero for an invalid handle.
    // Absolute paths only; equivalent paths share a cached resource.
    Result<TextureHandle> loadBmp(const std::filesystem::path &path);
    // Decodes PNG as RGBA, preserves alpha and caches by canonical absolute path.
    Result<TextureHandle> loadPng(const std::filesystem::path &path);
    Status release(TextureHandle texture);
    bool valid(TextureHandle texture) const;
    // Logical size in letterbox mode; the current output size in native-resolution mode.
    Vec2 viewport() const;
    Status beginFrame(Color clear, const Camera2D &camera);
    // Draws everything submitted so far, then directs later submissions to `pass`. endPass draws
    // the pass and restores the whole-frame view. Passes cannot nest and must end before present().
    Status beginPass(const RenderPass &pass);
    Status endPass();
    Status submit(const Sprite &sprite);
    Status debugRect(Rect rect, Color color, int layer = 1000);
    Status debugLine(Vec2 first, Vec2 second, Color color, int layer = 1000);
    // Diagnostic readback saves the physical content viewport before present (excludes bars).
    Status present(const std::optional<std::filesystem::path> &capture = std::nullopt);
    // The SDL backend, for tools that draw directly between passes and present (the editor's UI
    // layer). Game code must not use it.
    SDL_Renderer *nativeRenderer() const;

  private:
    friend class Application;
    Renderer();
    void assertThread() const;
    // width == height == 0 selects native resolution: no logical presentation, 1 unit = 1 pixel.
    static Result<std::unique_ptr<Renderer>> create(SDL_Window *window, int width, int height);
    Status flush();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace yk
