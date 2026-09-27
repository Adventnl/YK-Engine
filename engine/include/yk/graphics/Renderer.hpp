#pragma once
#include "yk/core/Result.hpp"
#include "yk/graphics/Camera2D.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
struct SDL_Window;
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
struct Color {
    std::uint8_t r{}, g{}, b{}, a{255};
};
struct Sprite {
    TextureHandle texture;
    Transform2D transform;
    Vec2 size;               // World units, independent of texture dimensions.
    Vec2 anchor{0.5F, 0.5F}; // Fractional pivot used for positioning and rotation.
    Color tint{255, 255, 255, 255};
    int layer{};
    float depth{}; // Ascending depth, then submission order within each layer.
};
class Renderer {
  public:
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    Result<TextureHandle> createTexture(int width, int height, std::span<const Color> pixels);
    // Absolute paths only; equivalent paths share a cached resource.
    Result<TextureHandle> loadBmp(const std::filesystem::path &path);
    Status release(TextureHandle texture);
    bool valid(TextureHandle texture) const;
    Vec2 viewport() const;
    Status beginFrame(Color clear, const Camera2D &camera);
    Status submit(const Sprite &sprite);
    Status debugRect(Rect rect, Color color, int layer = 1000);
    Status debugLine(Vec2 first, Vec2 second, Color color, int layer = 1000);
    // Diagnostic readback saves the physical content viewport before present (excludes bars).
    Status present(const std::optional<std::filesystem::path> &capture = std::nullopt);

  private:
    friend class Application;
    Renderer();
    void assertThread() const;
    static Result<std::unique_ptr<Renderer>> create(SDL_Window *window, int width, int height);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace yk
