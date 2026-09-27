#include "yk/graphics/Renderer.hpp"
#include "yk/core/Log.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cassert>
#include <limits>
#include <map>
#include <thread>
#include <variant>
#include <vector>
namespace yk {
namespace {
struct RendererDeleter {
    void operator()(SDL_Renderer *ptr) const {
        SDL_DestroyRenderer(ptr);
    }
};
struct TextureDeleter {
    void operator()(SDL_Texture *ptr) const {
        SDL_DestroyTexture(ptr);
    }
};
struct SurfaceDeleter {
    void operator()(SDL_Surface *ptr) const {
        SDL_DestroySurface(ptr);
    }
};
using NativeTexture = std::unique_ptr<SDL_Texture, TextureDeleter>;
Error sdlError(const std::string &operation) {
    return {operation + ": " + SDL_GetError()};
}
struct DebugRect {
    Rect rect;
    Color color;
};
struct Command {
    int layer;
    float depth;
    std::size_t sequence;
    std::variant<Sprite, DebugRect> data;
};
bool positive(Vec2 value) {
    return finite(value) && value.x > 0 && value.y > 0;
}
} // namespace
struct Renderer::Impl {
    const std::thread::id thread = std::this_thread::get_id();
    // Declared before resources: backend and identity outlive all textures/queued commands.
    std::unique_ptr<SDL_Renderer, RendererDeleter> native;
    std::shared_ptr<const void> identity = std::make_shared<const int>(0);
    std::vector<NativeTexture> textures;
    std::vector<Command> commands;
    std::map<std::filesystem::path, TextureHandle> fileTextures;
    Camera2D camera;
    Vec2 viewport;
    bool inFrame{};
};
Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
void Renderer::assertThread() const {
    assert(std::this_thread::get_id() == impl_->thread);
}
Renderer::~Renderer() {
    assertThread();
}
Result<std::unique_ptr<Renderer>> Renderer::create(SDL_Window *window, int width, int height) {
    auto renderer = std::unique_ptr<Renderer>(new Renderer());
    auto &state = *renderer->impl_;
    state.native.reset(SDL_CreateRenderer(window, nullptr));
    if (!state.native)
        return sdlError("Create renderer");
    if (!SDL_SetRenderLogicalPresentation(state.native.get(), width, height,
                                          SDL_LOGICAL_PRESENTATION_LETTERBOX))
        return sdlError("Set logical viewport");
    if (!SDL_SetRenderDrawBlendMode(state.native.get(), SDL_BLENDMODE_BLEND))
        return sdlError("Set debug primitive blending");
    state.viewport = {static_cast<float>(width), static_cast<float>(height)};
    state.commands.reserve(128);
    if (!SDL_SetRenderVSync(state.native.get(), 1))
        log(LogLevel::Warning, "renderer",
            "VSync unavailable; application still limits frame rate");
    log(LogLevel::Info, "renderer", SDL_GetRendererName(state.native.get()));
    return renderer;
}
Result<TextureHandle> Renderer::createTexture(int width, int height,
                                              std::span<const Color> pixels) {
    assertThread();
    static_assert(sizeof(Color) == 4);
    if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() / 4)
        return Error{"Texture dimensions invalid"};
    const auto w = static_cast<std::size_t>(width), h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h || pixels.size() != w * h)
        return Error{"Texture pixel count does not match dimensions"};
    NativeTexture texture(SDL_CreateTexture(impl_->native.get(), SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_STATIC, width, height));
    if (!texture)
        return sdlError("Create RGBA texture");
    if (!SDL_UpdateTexture(texture.get(), nullptr, pixels.data(), width * 4) ||
        !SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND) ||
        !SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST))
        return sdlError("Upload/configure RGBA texture");
    TextureHandle handle;
    handle.owner_ = impl_->identity;
    handle.index_ = impl_->textures.size();
    impl_->textures.push_back(std::move(texture));
    return handle;
}
Result<TextureHandle> Renderer::loadBmp(const std::filesystem::path &path) {
    assertThread();
    // The caller resolves its stable asset root. Never depend on an implicit working directory.
    if (!path.is_absolute())
        return Error{"BMP path must be absolute: " + path.string()};
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error)
        return Error{"Resolve BMP '" + path.string() + "': " + error.message()};
    const auto cached = impl_->fileTextures.find(canonical);
    if (cached != impl_->fileTextures.end() && valid(cached->second))
        return cached->second;
    const auto utf8 = canonical.u8string();
    const std::string filename(utf8.begin(), utf8.end());
    std::unique_ptr<SDL_Surface, SurfaceDeleter> surface(SDL_LoadBMP(filename.c_str()));
    if (!surface)
        return sdlError("Load BMP '" + filename + "'");
    NativeTexture texture(SDL_CreateTextureFromSurface(impl_->native.get(), surface.get()));
    if (!texture)
        return sdlError("Create BMP texture '" + filename + "'");
    if (!SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND) ||
        !SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST))
        return sdlError("Configure BMP texture '" + filename + "'");
    TextureHandle handle;
    handle.owner_ = impl_->identity;
    handle.index_ = impl_->textures.size();
    impl_->textures.push_back(std::move(texture));
    impl_->fileTextures.insert_or_assign(canonical, handle);
    return handle;
}
bool Renderer::valid(TextureHandle texture) const {
    assertThread();
    return texture.owner_.lock() == impl_->identity && texture.index_ < impl_->textures.size() &&
           impl_->textures[texture.index_] != nullptr;
}
Status Renderer::release(TextureHandle texture) {
    assertThread();
    if (impl_->inFrame)
        return Error{"Release texture outside frame submission/presentation"};
    if (!valid(texture))
        return Error{"Invalid, released, or foreign texture handle"};
    impl_->textures[texture.index_].reset();
    return success();
}
Vec2 Renderer::viewport() const {
    assertThread();
    return impl_->viewport;
}
Status Renderer::beginFrame(Color clear, const Camera2D &camera) {
    assertThread();
    if (impl_->inFrame)
        return Error{"Frame already begun"};
    if (!finite(camera.position))
        return Error{"Invalid camera position"};
    impl_->camera = camera;
    impl_->commands.clear();
    if (!SDL_SetRenderDrawColor(impl_->native.get(), clear.r, clear.g, clear.b, clear.a) ||
        !SDL_RenderClear(impl_->native.get()))
        return sdlError("Clear frame");
    impl_->inFrame = true;
    return success();
}
Status Renderer::submit(const Sprite &sprite) {
    assertThread();
    if (!impl_->inFrame)
        return Error{"Sprite submission outside frame"};
    if (!valid(sprite.texture))
        return Error{"Sprite has invalid, released, or foreign texture"};
    if (!finite(sprite.transform.position) || !positive(sprite.transform.scale) ||
        !positive(sprite.size) || !finite(sprite.anchor) ||
        !std::isfinite(sprite.transform.rotationDegrees) || !std::isfinite(sprite.depth))
        return Error{"Sprite contains invalid transform, dimensions, or depth"};
    impl_->commands.push_back({sprite.layer, sprite.depth, impl_->commands.size(), sprite});
    return success();
}
Status Renderer::debugRect(Rect rect, Color color, int layer) {
    assertThread();
    if (!impl_->inFrame)
        return Error{"Debug rectangle outside frame"};
    if (!finite(rect.position) || !positive(rect.size))
        return Error{"Invalid debug rectangle"};
    impl_->commands.push_back({layer, 0, impl_->commands.size(), DebugRect{rect, color}});
    return success();
}
Status Renderer::present(const std::optional<std::filesystem::path> &capture) {
    assertThread();
    if (!impl_->inFrame)
        return Error{"Present outside frame"};
    // A failed flush ends this frame too, so callers can recover or shut down safely.
    impl_->inFrame = false;
    std::sort(impl_->commands.begin(), impl_->commands.end(),
              [](const Command &a, const Command &b) {
                  if (a.layer != b.layer)
                      return a.layer < b.layer;
                  if (a.depth != b.depth)
                      return a.depth < b.depth;
                  return a.sequence < b.sequence;
              });
    for (const auto &command : impl_->commands) {
        if (const auto *sprite = std::get_if<Sprite>(&command.data)) {
            const Vec2 size{sprite->size.x * sprite->transform.scale.x * impl_->camera.zoom(),
                            sprite->size.y * sprite->transform.scale.y * impl_->camera.zoom()};
            const auto position =
                impl_->camera.worldToScreen(sprite->transform.position, impl_->viewport);
            const SDL_FPoint pivot{size.x * sprite->anchor.x, size.y * sprite->anchor.y};
            const SDL_FRect destination{position.x - pivot.x, position.y - pivot.y, size.x, size.y};
            if (!finite(size) || !finite({destination.x, destination.y}))
                return Error{"Sprite projection overflow"};
            auto *texture = impl_->textures[sprite->texture.index_].get();
            if (!SDL_SetTextureColorMod(texture, sprite->tint.r, sprite->tint.g, sprite->tint.b) ||
                !SDL_SetTextureAlphaMod(texture, sprite->tint.a) ||
                !SDL_RenderTextureRotated(impl_->native.get(), texture, nullptr, &destination,
                                          sprite->transform.rotationDegrees, &pivot, SDL_FLIP_NONE))
                return sdlError("Draw sprite");
        } else {
            const auto &debug = std::get<DebugRect>(command.data);
            const auto position = impl_->camera.worldToScreen(debug.rect.position, impl_->viewport);
            const auto size = debug.rect.size * impl_->camera.zoom();
            if (!finite(position) || !finite(size))
                return Error{"Debug rectangle projection overflow"};
            const SDL_FRect destination{position.x, position.y, size.x, size.y};
            const auto color = debug.color;
            if (!SDL_SetRenderDrawColor(impl_->native.get(), color.r, color.g, color.b, color.a) ||
                !SDL_RenderRect(impl_->native.get(), &destination))
                return sdlError("Draw debug rectangle");
        }
    }
    if (capture) {
        std::unique_ptr<SDL_Surface, SurfaceDeleter> surface(
            SDL_RenderReadPixels(impl_->native.get(), nullptr));
        if (!surface)
            return sdlError("Read rendered frame");
        const auto utf8 = capture->u8string();
        const std::string filename(utf8.begin(), utf8.end());
        if (!SDL_SaveBMP(surface.get(), filename.c_str()))
            return sdlError("Save frame '" + filename + "'");
    }
    if (!SDL_RenderPresent(impl_->native.get()))
        return sdlError("Present frame");
    return success();
}
} // namespace yk
