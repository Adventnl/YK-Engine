#include "yk/graphics/Renderer.hpp"
#include "yk/core/Log.hpp"
#include <SDL3/SDL.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <map>
#include <set>
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
// Reads back everything drawn to the current output and writes it as a BMP.
Status readbackAndSave(SDL_Renderer *native, const std::filesystem::path &path) {
    std::unique_ptr<SDL_Surface, SurfaceDeleter> surface(SDL_RenderReadPixels(native, nullptr));
    if (!surface)
        return sdlError("Read rendered frame");
    const auto utf8 = path.u8string();
    const std::string filename(utf8.begin(), utf8.end());
    if (!SDL_SaveBMP(surface.get(), filename.c_str()))
        return sdlError("Save frame '" + filename + "'");
    return success();
}
struct DebugRect {
    Rect rect;
    Color color;
};
struct DebugLine {
    Vec2 first, second;
    Color color;
};
struct Command {
    int layer;
    float depth;
    std::size_t sequence;
    std::variant<Sprite, DebugRect, DebugLine> data;
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
    Camera2D camera;      // Camera of the pass being recorded.
    Camera2D frameCamera; // Camera given to beginFrame; restored after each pass.
    Vec2 viewport;        // Whole output (logical units in letterbox mode).
    Vec2 passSize;        // Size of the viewport the current pass draws into.
    std::array<std::optional<TextureHandle>, 3> builtins;
    std::set<std::size_t> renderTargets; // Texture indices created by createRenderTarget.
    FrameStats building;                 // Counted while the current frame is being drawn.
    FrameStats finished;                 // The last completed frame.
    bool targetActive{};
    bool nativeResolution{};
    bool passOpen{};
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
    state.nativeResolution = width == 0 && height == 0;
    if (!state.nativeResolution &&
        !SDL_SetRenderLogicalPresentation(state.native.get(), width, height,
                                          SDL_LOGICAL_PRESENTATION_LETTERBOX))
        return sdlError("Set logical viewport");
    if (!SDL_SetRenderDrawBlendMode(state.native.get(), SDL_BLENDMODE_BLEND))
        return sdlError("Set debug primitive blending");
    state.viewport = {static_cast<float>(width), static_cast<float>(height)};
    if (state.nativeResolution) {
        int outputWidth = 0, outputHeight = 0;
        if (!SDL_GetCurrentRenderOutputSize(state.native.get(), &outputWidth, &outputHeight))
            return sdlError("Query render output size");
        state.viewport = {static_cast<float>(outputWidth), static_cast<float>(outputHeight)};
    }
    state.passSize = state.viewport;
    state.commands.reserve(128);
    if (!SDL_SetRenderVSync(state.native.get(), 1))
        log(LogLevel::Warning, "renderer",
            "VSync unavailable; application still limits frame rate");
    log(LogLevel::Info, "renderer", SDL_GetRendererName(state.native.get()));
    return renderer;
}
Result<TextureHandle> Renderer::createTexture(int width, int height, std::span<const Color> pixels,
                                              TextureFilter filter) {
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
        !SDL_SetTextureScaleMode(texture.get(), filter == TextureFilter::Linear
                                                    ? SDL_SCALEMODE_LINEAR
                                                    : SDL_SCALEMODE_NEAREST))
        return sdlError("Upload/configure RGBA texture");
    TextureHandle handle;
    handle.owner_ = impl_->identity;
    handle.index_ = impl_->textures.size();
    impl_->textures.push_back(std::move(texture));
    return handle;
}
Result<TextureHandle> Renderer::builtinTexture(BuiltinTexture kind) {
    assertThread();
    auto &slot = impl_->builtins[static_cast<std::size_t>(kind)];
    if (slot && valid(*slot))
        return *slot;
    Result<TextureHandle> created = Error{"Unknown builtin texture"};
    if (kind == BuiltinTexture::White) {
        const std::array<Color, 1> white{{{255, 255, 255, 255}}};
        created = createTexture(1, 1, white);
    } else if (kind == BuiltinTexture::Circle) {
        // An anti-aliased disc; scaled non-uniformly it becomes an ellipse.
        constexpr int size = 128;
        std::vector<Color> pixels(static_cast<std::size_t>(size * size));
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const float distance = std::hypot(static_cast<float>(x) + 0.5F - size / 2.0F,
                                                  static_cast<float>(y) + 0.5F - size / 2.0F);
                const float coverage = std::clamp(size / 2.0F - distance + 0.5F, 0.0F, 1.0F);
                pixels[static_cast<std::size_t>(y * size + x)] = {
                    255, 255, 255, static_cast<std::uint8_t>(std::lround(coverage * 255.0F))};
            }
        created = createTexture(size, size, pixels, TextureFilter::Linear);
    }
    if (kind == BuiltinTexture::Glow) {
        // White with a smooth radial falloff (alpha 1 at the center, 0 at the edge): the shape of
        // every soft particle and glow. Squared so it fades gently rather than showing a disc.
        constexpr int size = 128;
        std::vector<Color> pixels(static_cast<std::size_t>(size * size));
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const float distance = std::hypot(static_cast<float>(x) + 0.5F - size / 2.0F,
                                                  static_cast<float>(y) + 0.5F - size / 2.0F) /
                                       (size / 2.0F);
                const float falloff = std::clamp(1.0F - distance, 0.0F, 1.0F);
                pixels[static_cast<std::size_t>(y * size + x)] = {
                    255, 255, 255,
                    static_cast<std::uint8_t>(std::lround(falloff * falloff * 255.0F))};
            }
        created = createTexture(size, size, pixels, TextureFilter::Linear);
    }
    if (created)
        slot = created.value();
    return created;
}
Vec2 Renderer::textureSize(TextureHandle texture) const {
    assertThread();
    if (!valid(texture))
        return {};
    float width = 0, height = 0;
    SDL_GetTextureSize(impl_->textures[texture.index_].get(), &width, &height);
    return {width, height};
}
Result<TextureHandle> Renderer::createRenderTarget(int width, int height) {
    assertThread();
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
        return Error{"Render target dimensions invalid"};
    NativeTexture texture(SDL_CreateTexture(impl_->native.get(), SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_TARGET, width, height));
    if (!texture)
        return sdlError("Create render target");
    if (!SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND) ||
        !SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR))
        return sdlError("Configure render target");
    TextureHandle handle;
    handle.owner_ = impl_->identity;
    handle.index_ = impl_->textures.size();
    impl_->textures.push_back(std::move(texture));
    impl_->renderTargets.insert(handle.index_);
    return handle;
}
SDL_Texture *Renderer::nativeTexture(TextureHandle texture) const {
    assertThread();
    return valid(texture) ? impl_->textures[texture.index_].get() : nullptr;
}
Renderer::FrameStats Renderer::lastFrameStats() const {
    assertThread();
    return impl_->finished;
}
SDL_Renderer *Renderer::nativeRenderer() const {
    assertThread();
    return impl_->native.get();
}
Result<TextureHandle> Renderer::loadBmp(const std::filesystem::path &path, TextureFilter filter) {
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
        !SDL_SetTextureScaleMode(texture.get(), filter == TextureFilter::Linear
                                                    ? SDL_SCALEMODE_LINEAR
                                                    : SDL_SCALEMODE_NEAREST))
        return sdlError("Configure BMP texture '" + filename + "'");
    TextureHandle handle;
    handle.owner_ = impl_->identity;
    handle.index_ = impl_->textures.size();
    impl_->textures.push_back(std::move(texture));
    impl_->fileTextures.insert_or_assign(canonical, handle);
    return handle;
}
Result<TextureHandle> Renderer::loadPng(const std::filesystem::path &path, TextureFilter filter) {
    assertThread();
    if (!path.is_absolute())
        return Error{"PNG path must be absolute: " + path.string()};
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error)
        return Error{"Resolve PNG '" + path.string() + "': " + error.message()};
    const auto cached = impl_->fileTextures.find(canonical);
    if (cached != impl_->fileTextures.end() && valid(cached->second))
        return cached->second;
    const auto utf8 = canonical.u8string();
    const std::string filename(utf8.begin(), utf8.end());
    int width = 0, height = 0, channels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha), stbi_image_free);
    if (!pixels)
        return Error{"Decode PNG '" + filename + "': " + stbi_failure_reason()};
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    auto texture = createTexture(width, height,
                                 {reinterpret_cast<const Color *>(pixels.get()), count}, filter);
    if (!texture)
        return Error{"Upload PNG '" + filename + "': " + texture.error()};
    impl_->fileTextures.insert_or_assign(canonical, texture.value());
    return texture;
}
Result<TextureHandle> Renderer::loadPngMemory(std::span<const unsigned char> bytes,
                                              TextureFilter filter) {
    int width = 0, height = 0, channels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                              &channels, STBI_rgb_alpha),
        stbi_image_free);
    if (!pixels)
        return Error{std::string("Decode embedded PNG: ") + stbi_failure_reason()};
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    return createTexture(width, height, {reinterpret_cast<const Color *>(pixels.get()), count},
                         filter);
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
    impl_->renderTargets.erase(texture.index_);
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
    if (impl_->nativeResolution) {
        int width = 0, height = 0;
        if (!SDL_GetCurrentRenderOutputSize(impl_->native.get(), &width, &height))
            return sdlError("Query render output size");
        impl_->viewport = {static_cast<float>(width), static_cast<float>(height)};
    }
    impl_->camera = impl_->frameCamera = camera;
    impl_->passSize = impl_->viewport;
    impl_->passOpen = false;
    impl_->building = {};
    impl_->commands.clear();
    SDL_SetRenderViewport(impl_->native.get(), nullptr);
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
Status Renderer::debugLine(Vec2 first, Vec2 second, Color color, int layer) {
    assertThread();
    if (!impl_->inFrame)
        return Error{"Debug line outside frame"};
    if (!finite(first) || !finite(second))
        return Error{"Invalid debug line"};
    impl_->commands.push_back({layer, 0, impl_->commands.size(), DebugLine{first, second, color}});
    return success();
}
Status Renderer::flush() {
    // Commands leave the queue up front so a failed draw never replays stale submissions.
    std::vector<Command> drawing;
    drawing.swap(impl_->commands);
    std::sort(drawing.begin(), drawing.end(), [](const Command &a, const Command &b) {
        if (a.layer != b.layer)
            return a.layer < b.layer;
        if (a.depth != b.depth)
            return a.depth < b.depth;
        return a.sequence < b.sequence;
    });
    for (const auto &command : drawing) {
        if (const auto *sprite = std::get_if<Sprite>(&command.data)) {
            const Vec2 size{sprite->size.x * sprite->transform.scale.x * impl_->camera.zoom(),
                            sprite->size.y * sprite->transform.scale.y * impl_->camera.zoom()};
            const auto position =
                impl_->camera.worldToScreen(sprite->transform.position, impl_->passSize);
            const SDL_FPoint pivot{size.x * sprite->anchor.x, size.y * sprite->anchor.y};
            const SDL_FRect destination{position.x - pivot.x, position.y - pivot.y, size.x, size.y};
            if (!finite(size) || !finite({destination.x, destination.y}))
                return Error{"Sprite projection overflow"};
            auto *texture = impl_->textures[sprite->texture.index_].get();
            SDL_FRect sourceStorage{};
            const SDL_FRect *source = nullptr;
            if (sprite->source) {
                const auto &region = *sprite->source;
                if (!finite(region.position) || !positive(region.size))
                    return Error{"Sprite contains an invalid source region"};
                sourceStorage = {region.position.x, region.position.y, region.size.x,
                                 region.size.y};
                source = &sourceStorage;
            }
            ++impl_->building.sprites;
            if (!SDL_SetTextureBlendMode(texture, sprite->blend == BlendMode::Additive
                                                      ? SDL_BLENDMODE_ADD
                                                      : SDL_BLENDMODE_BLEND) ||
                !SDL_SetTextureColorMod(texture, sprite->tint.r, sprite->tint.g, sprite->tint.b) ||
                !SDL_SetTextureAlphaMod(texture, sprite->tint.a) ||
                !SDL_RenderTextureRotated(impl_->native.get(), texture, source, &destination,
                                          sprite->transform.rotationDegrees, &pivot,
                                          sprite->flipHorizontal ? SDL_FLIP_HORIZONTAL
                                                                 : SDL_FLIP_NONE))
                return sdlError("Draw sprite");
        } else if (const auto *line = std::get_if<DebugLine>(&command.data)) {
            ++impl_->building.lines;
            const auto first = impl_->camera.worldToScreen(line->first, impl_->passSize);
            const auto second = impl_->camera.worldToScreen(line->second, impl_->passSize);
            if (!finite(first) || !finite(second))
                return Error{"Debug line projection overflow"};
            const auto color = line->color;
            if (!SDL_SetRenderDrawColor(impl_->native.get(), color.r, color.g, color.b, color.a) ||
                !SDL_RenderLine(impl_->native.get(), first.x, first.y, second.x, second.y))
                return sdlError("Draw debug line");
        } else {
            ++impl_->building.lines;
            const auto &debug = std::get<DebugRect>(command.data);
            const auto position = impl_->camera.worldToScreen(debug.rect.position, impl_->passSize);
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
    return success();
}
Status Renderer::beginPass(const RenderPass &pass) {
    assertThread();
    if (!impl_->inFrame)
        return Error{"Render pass outside frame"};
    if (impl_->passOpen)
        return Error{"Render passes cannot nest"};
    if (!finite(pass.camera.position))
        return Error{"Invalid pass camera position"};
    Vec2 surface = impl_->viewport; // Size of what this pass draws onto.
    SDL_Texture *targetTexture = nullptr;
    if (pass.target) {
        if (!valid(*pass.target) || !impl_->renderTargets.contains(pass.target->index_))
            return Error{"Pass target is not a live render target"};
        targetTexture = impl_->textures[pass.target->index_].get();
        surface = textureSize(*pass.target);
    }
    SDL_Rect area{};
    if (pass.viewport) {
        const Rect &rect = *pass.viewport;
        if (!finite(rect.position) || !finite(rect.size))
            return Error{"Invalid pass viewport"};
        const int left = std::max(0, static_cast<int>(std::floor(rect.position.x)));
        const int top = std::max(0, static_cast<int>(std::floor(rect.position.y)));
        const int right = std::min(static_cast<int>(surface.x),
                                   static_cast<int>(std::ceil(rect.position.x + rect.size.x)));
        const int bottom = std::min(static_cast<int>(surface.y),
                                    static_cast<int>(std::ceil(rect.position.y + rect.size.y)));
        if (right <= left || bottom <= top)
            return Error{"Pass viewport is empty or outside the frame"};
        area = {left, top, right - left, bottom - top};
    }
    if (auto drawn = flush(); !drawn) // Earlier submissions belong to the previous view.
        return drawn;
    ++impl_->building.passes;
    if (targetTexture) {
        if (!SDL_SetRenderTarget(impl_->native.get(), targetTexture))
            return sdlError("Set render target");
        impl_->targetActive = true;
    }
    if (!SDL_SetRenderViewport(impl_->native.get(), pass.viewport ? &area : nullptr))
        return sdlError("Set pass viewport");
    impl_->passSize =
        pass.viewport ? Vec2{static_cast<float>(area.w), static_cast<float>(area.h)} : surface;
    impl_->camera = pass.camera;
    if (pass.clear) {
        const Color color = *pass.clear;
        if (!SDL_SetRenderDrawColor(impl_->native.get(), color.r, color.g, color.b, color.a) ||
            !SDL_RenderFillRect(impl_->native.get(), nullptr))
            return sdlError("Clear pass viewport");
    }
    impl_->passOpen = true;
    return success();
}
Status Renderer::endPass() {
    assertThread();
    if (!impl_->passOpen)
        return Error{"No render pass to end"};
    impl_->passOpen = false;
    auto drawn = flush();
    SDL_SetRenderViewport(impl_->native.get(), nullptr);
    if (impl_->targetActive) {
        SDL_SetRenderTarget(impl_->native.get(), nullptr);
        impl_->targetActive = false;
    }
    impl_->camera = impl_->frameCamera;
    impl_->passSize = impl_->viewport;
    return drawn;
}
Status Renderer::capture(const std::filesystem::path &path) {
    assertThread();
    if (!impl_->inFrame)
        return Error{"Capture outside frame"};
    if (impl_->passOpen)
        return Error{"End the render pass before capturing"};
    if (auto drawn = flush(); !drawn)
        return drawn;
    return readbackAndSave(impl_->native.get(), path);
}
Status Renderer::present(const std::optional<std::filesystem::path> &capture) {
    assertThread();
    if (!impl_->inFrame)
        return Error{"Present outside frame"};
    // A failed flush ends this frame too, so callers can recover or shut down safely.
    impl_->inFrame = false;
    impl_->passOpen = false;
    auto drawn = flush();
    SDL_SetRenderViewport(impl_->native.get(), nullptr);
    if (impl_->targetActive) {
        SDL_SetRenderTarget(impl_->native.get(), nullptr);
        impl_->targetActive = false;
    }
    impl_->camera = impl_->frameCamera;
    impl_->passSize = impl_->viewport;
    impl_->finished = impl_->building;
    if (!drawn)
        return drawn;
    if (capture)
        if (auto saved = readbackAndSave(impl_->native.get(), *capture); !saved)
            return saved;
    if (!SDL_RenderPresent(impl_->native.get()))
        return sdlError("Present frame");
    return success();
}
} // namespace yk
