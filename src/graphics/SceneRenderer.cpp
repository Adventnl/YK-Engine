#include "yk/graphics/SceneRenderer.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>
#include <cmath>
#include <typeindex>

namespace yk {
namespace {
constexpr Color missingTextureColor{255, 0, 220, 255};
// A tiled sprite needing more quads than this is drawn stretched instead (and reported).
constexpr std::size_t maxTilesPerSprite = 40000;

std::string wrapWords(const std::string &text, std::size_t columns) {
    std::string result;
    std::size_t column = 0;
    columns = std::max<std::size_t>(1, columns);
    for (std::size_t start = 0; start < text.size();) {
        if (text[start] == '\n') {
            result += '\n';
            column = 0;
            ++start;
            continue;
        }
        if (text[start] == ' ') {
            ++start;
            continue;
        }
        const auto end = text.find_first_of(" \n", start);
        const auto word = text.substr(start, end == std::string::npos ? end : end - start);
        if (column && column + word.size() + 1 > columns) {
            result += '\n';
            column = 0;
        }
        if (column) {
            result += ' ';
            ++column;
        }
        for (char ch : word) {
            if (column >= columns) {
                result += '\n';
                column = 0;
            }
            result += ch;
            ++column;
        }
        start += word.size();
    }
    return result;
}

Color colliderColor(const Entity &entity, const Collider &collider) {
    if (collider.isTrigger)
        return {255, 200, 70, 255};
    if (const auto *body = entity.get<RigidBody>()) {
        if (body->type == RigidBodyType::Kinematic)
            return {80, 170, 255, 255};
        if (body->type == RigidBodyType::Dynamic)
            return {90, 220, 110, 255};
    }
    return {130, 150, 180, 255};
}

// Receives the quads of one sprite. Regions are described in the sprite's own frame: origin at its
// center, +x right, +y down, unrotated and unflipped; the sink flips, rotates and places them.
struct QuadSink {
    Renderer &renderer;
    Sprite proto;  // Texture, tint, blend, layer, depth and rotation are shared by all quads.
    Vec2 center;   // World position of the sprite's center.
    float angle{}; // Radians.
    bool flip{};
    bool culling{}; // Skip quads outside `visible` (only exact for unrotated sprites).
    Rect visible;
    std::size_t quads{};

    Status emit(Rect local, std::optional<Rect> source) {
        Vec2 middle = local.position + local.size * 0.5F;
        if (flip)
            middle.x = -middle.x;
        const Vec2 world = center + rotated(middle, angle);
        if (culling && !overlaps({world - local.size * 0.5F, local.size}, visible))
            return success();
        Sprite quad = proto;
        quad.transform.position = world;
        quad.size = local.size;
        quad.source = source;
        ++quads;
        return renderer.submit(quad);
    }

    // Repeats `source` (pixels) across `local`, cropping the last column and row, one tile being
    // `tileWorld` big. Only tiles that can be seen are emitted when the sprite is unrotated. When
    // more than maxTilesPerSprite would be emitted nothing is drawn and `tooMany` is set, so the
    // caller can fall back to one stretched quad.
    Status tiles(Rect local, Rect source, Vec2 tileWorld, bool &tooMany) {
        tooMany = false;
        if (!(tileWorld.x > 0.0F) || !(tileWorld.y > 0.0F) || !(local.size.x > 0.0F) ||
            !(local.size.y > 0.0F))
            return success();
        constexpr float epsilon = 1e-4F;
        const int columns =
            std::max(1, static_cast<int>(std::ceil(local.size.x / tileWorld.x - epsilon)));
        const int rows =
            std::max(1, static_cast<int>(std::ceil(local.size.y / tileWorld.y - epsilon)));
        int first[2] = {0, 0}, last[2] = {columns - 1, rows - 1};
        if (culling) {
            // The visible rectangle in the sprite's frame (mirrored when flipped).
            Rect view{visible.position - center, visible.size};
            if (flip)
                view.position.x = -(view.position.x + view.size.x);
            const auto range = [&](int axis, float origin, float visibleStart, float visibleSize,
                                   float tile, int count) {
                first[axis] = std::clamp(
                    static_cast<int>(std::floor((visibleStart - origin) / tile)), 0, count - 1);
                last[axis] = std::clamp(
                    static_cast<int>(std::ceil((visibleStart + visibleSize - origin) / tile)) - 1,
                    0, count - 1);
            };
            range(0, local.position.x, view.position.x, view.size.x, tileWorld.x, columns);
            range(1, local.position.y, view.position.y, view.size.y, tileWorld.y, rows);
        }
        const double emitted = static_cast<double>(last[0] - first[0] + 1) *
                               static_cast<double>(last[1] - first[1] + 1);
        if (emitted > static_cast<double>(maxTilesPerSprite)) {
            tooMany = true;
            return success();
        }
        for (int row = first[1]; row <= last[1]; ++row) {
            for (int column = first[0]; column <= last[0]; ++column) {
                const float x = local.position.x + static_cast<float>(column) * tileWorld.x;
                const float y = local.position.y + static_cast<float>(row) * tileWorld.y;
                const float w = std::min(tileWorld.x, local.position.x + local.size.x - x);
                const float h = std::min(tileWorld.y, local.position.y + local.size.y - y);
                if (!(w > 0.0F) || !(h > 0.0F))
                    continue;
                const Rect part{
                    {source.position.x, source.position.y},
                    {source.size.x * (w / tileWorld.x), source.size.y * (h / tileWorld.y)}};
                if (auto done = emit({{x, y}, {w, h}}, part); !done)
                    return done;
            }
        }
        return success();
    }
};
} // namespace

Result<std::unique_ptr<SceneRenderer>>
SceneRenderer::create(Renderer &renderer, const AssetSource *assets, TextureDefaults defaults) {
    auto font = BitmapFont::create(renderer);
    if (!font)
        return Error{font.error()};
    auto self = std::unique_ptr<SceneRenderer>(new SceneRenderer());
    self->assets_ = assets;
    self->defaults_ = defaults;
    self->font_ = std::move(font.value());
    return self;
}

Camera2D SceneRenderer::cameraFor(const CameraView &view, Vec2 viewport) {
    Camera2D camera;
    camera.position = finite(view.position) ? view.position : Vec2{};
    if (view.visibleHeight > 0.0F && viewport.y > 0.0F)
        camera.setZoom(viewport.y / view.visibleHeight);
    return camera;
}

Result<TextureHandle> SceneRenderer::worldRaster(Renderer &renderer, Vec2 pixels) {
    if (worldRaster_ && renderer.valid(*worldRaster_) &&
        renderer.textureSize(*worldRaster_) == pixels)
        return *worldRaster_;
    if (worldRaster_ && renderer.valid(*worldRaster_))
        if (auto released = renderer.retire(*worldRaster_); !released)
            return Error{released.error()};
    worldRaster_.reset();
    auto created = renderer.createRenderTarget(static_cast<int>(pixels.x),
                                               static_cast<int>(pixels.y), TextureFilter::Nearest);
    if (created)
        worldRaster_ = created.value();
    return created;
}

const Camera *SceneRenderer::primaryCamera(const Scene &scene) {
    const Camera *fallback = nullptr;
    for (const EntityId id : scene.orderedIds()) {
        const Entity *entity = scene.find(id);
        if (!entity || !entity->activeInHierarchy())
            continue;
        for (const Camera *camera : entity->getAll<Camera>()) {
            if (!camera->enabled)
                continue;
            if (camera->primary)
                return camera;
            if (!fallback)
                fallback = camera;
        }
    }
    return fallback;
}

Vec2 SceneRenderer::anchoredTopLeft(UiAnchor anchor, Vec2 viewport, Vec2 size, Vec2 offset) {
    return uiTopLeft(anchor, viewport, size, offset);
}

const SceneRenderer::TextureInfo &SceneRenderer::textureFor(Renderer &renderer,
                                                            const std::string &path) {
    const auto cached = textures_.find(path);
    if (cached != textures_.end())
        return cached->second;
    TextureInfo info;
    info.settings = resolve(defaults_, {});
    if (path == "builtin:white" || path == "builtin:circle") {
        auto builtin = renderer.builtinTexture(path == "builtin:white" ? BuiltinTexture::White
                                                                       : BuiltinTexture::Circle);
        if (builtin) {
            info.handle = builtin.value();
            info.pixels = renderer.textureSize(builtin.value());
        }
        return textures_.emplace(path, std::move(info)).first->second;
    }
    std::string failure = "no asset source";
    if (assets_) {
        // The sidecar is optional; a broken one is reported and ignored.
        TextureMeta meta;
        if (auto sidecar = assets_->readText(TextureMeta::sidecarPath(path)); sidecar) {
            auto parsed = Json::parse(sidecar.value());
            auto decoded = parsed ? TextureMeta::fromJson(parsed.value())
                                  : Result<TextureMeta>(Error{parsed.error()});
            if (decoded)
                meta = decoded.value();
            else
                log(LogLevel::Warning, "render",
                    "Import settings of '" + path + "' ignored: " + decoded.error());
        }
        info.settings = resolve(defaults_, meta);
        const std::filesystem::path file = assets_->filePath(path);
        if (file.empty()) {
            failure = "not a file-backed asset";
        } else {
            const std::string extension = file.extension().string();
            auto texture = extension == ".bmp" ? renderer.loadBmp(file, info.settings.filter)
                                               : renderer.loadPng(file, info.settings.filter);
            if (texture) {
                info.handle = texture.value();
                info.pixels = renderer.textureSize(texture.value());
            } else {
                failure = texture.error();
            }
        }
    }
    if (!info.handle)
        log(LogLevel::Warning, "render",
            "Texture '" + path + "' unavailable (" + failure + "); drawing a placeholder");
    return textures_.emplace(path, std::move(info)).first->second;
}

std::optional<SceneRenderer::LoadedTexture> SceneRenderer::loadedTexture(Renderer &renderer,
                                                                         const std::string &path) {
    const TextureInfo &info = textureFor(renderer, path);
    if (!info.handle)
        return std::nullopt;
    return LoadedTexture{*info.handle, info.pixels};
}

ResolvedTexture SceneRenderer::textureSettings(const std::string &path) {
    const auto cached = textures_.find(path);
    if (cached != textures_.end())
        return cached->second.settings;
    TextureMeta meta;
    if (assets_)
        if (auto sidecar = assets_->readText(TextureMeta::sidecarPath(path)); sidecar)
            if (auto parsed = Json::parse(sidecar.value()); parsed)
                if (auto decoded = TextureMeta::fromJson(parsed.value()); decoded)
                    meta = decoded.value();
    return resolve(defaults_, meta);
}

void SceneRenderer::reload(Renderer &renderer, const std::string &path) {
    tilesets_.erase(path);
    const auto found = textures_.find(path);
    if (found == textures_.end())
        return;
    // Builtins are shared; anything file backed is released so the file is decoded again.
    if (found->second.handle && path.rfind("builtin:", 0) != 0)
        renderer.release(*found->second.handle);
    textures_.erase(found);
}

Status SceneRenderer::drawSprite(Renderer &renderer, const Entity &entity, const Transform2D &world,
                                 const SpriteRenderer &sprite, const WorldView &view,
                                 const Rect &visible, bool culling, float strength) {
    (void)entity;
    ++stats_.sprites;
    const Vec2 scale{std::fabs(world.scale.x), std::fabs(world.scale.y)};
    const Vec2 size = hadamard(sprite.size, scale);
    if (!(size.x > 0.0F) || !(size.y > 0.0F))
        return success();
    Vec2 center = transformPoint(world, sprite.offset);
    if (view.parallax && view.viewport.x > 0.0F)
        center += hadamard(view.camera.position, Vec2{1.0F, 1.0F} - sprite.parallax);
    // A bounding circle rejects whole sprites quickly (a tiled background may be huge, so the
    // per-tile test below matters more than this one).
    if (culling) {
        const float radius = 0.5F * length(size);
        const Vec2 nearest = clamped(center, visible.position, visible.position + visible.size);
        if (distance(center, nearest) > radius) {
            ++stats_.culled;
            return success();
        }
    }

    if (sprite.shadowSize.x > 0.0F && sprite.shadowSize.y > 0.0F && sprite.shadowColor.a > 0) {
        auto glow = renderer.builtinTexture(BuiltinTexture::Glow);
        if (!glow)
            return Error{glow.error()};
        Sprite shadow;
        shadow.texture = glow.value();
        shadow.transform.position = transformPoint(world, sprite.shadowOffset);
        shadow.size = hadamard(sprite.shadowSize, scale);
        shadow.tint = sprite.shadowColor;
        shadow.tint.a = static_cast<std::uint8_t>(static_cast<float>(shadow.tint.a) * strength);
        shadow.layer = sprite.layer - 1;
        shadow.depth = sprite.ySort ? world.position.y + sprite.sortOffset : sprite.order;
        if (auto drawn = renderer.submit(shadow); !drawn)
            return drawn;
        ++stats_.quads;
    }
    QuadSink sink{renderer,
                  {},
                  center,
                  degreesToRadians(world.rotationDegrees),
                  sprite.flipX != (world.scale.x < 0.0F),
                  culling && world.rotationDegrees == 0.0F,
                  visible,
                  0};
    sink.proto.tint = sprite.color;
    if (strength < 1.0F) // A faded world level.
        sink.proto.tint.a = static_cast<std::uint8_t>(static_cast<float>(sprite.color.a) *
                                                      std::clamp(strength, 0.0F, 1.0F));
    sink.proto.layer = sprite.layer;
    sink.proto.depth =
        sprite.ySort ? world.position.y + sprite.sortOffset + sprite.order : sprite.order;
    sink.proto.transform.rotationDegrees = world.rotationDegrees;
    sink.proto.flipHorizontal = sink.flip;
    sink.proto.snapEdges = sprite.drawMode != SpriteDrawMode::Simple;
    sink.proto.blend =
        sprite.blend == SpriteBlend::Additive ? BlendMode::Additive : BlendMode::Alpha;
    const Rect whole{-size * 0.5F, size};

    // Placeholder shapes: no texture at all.
    if (sprite.texture.path.empty()) {
        auto shape = renderer.builtinTexture(
            sprite.shape == SpriteShape::Ellipse ? BuiltinTexture::Circle : BuiltinTexture::White);
        if (!shape)
            return Error{shape.error()};
        sink.proto.texture = shape.value();
        auto done = sink.emit(whole, std::nullopt);
        stats_.quads += sink.quads;
        return done;
    }

    const TextureInfo &info = textureFor(renderer, sprite.texture.path);
    if (!info.handle) { // Missing art: a magenta rectangle, so the problem is visible.
        auto white = renderer.builtinTexture(BuiltinTexture::White);
        if (!white)
            return Error{white.error()};
        sink.proto.texture = white.value();
        sink.proto.tint = missingTextureColor;
        auto done = sink.emit(whole, std::nullopt);
        stats_.quads += sink.quads;
        return done;
    }
    sink.proto.texture = *info.handle;

    // The cell of the sprite sheet to draw, in texture pixels.
    const int columns = sprite.columns > 1 ? sprite.columns : info.settings.columns;
    const int rows = sprite.rows > 1 ? sprite.rows : info.settings.rows;
    Rect cell{{0.0F, 0.0F}, info.pixels};
    const bool sheet = columns > 1 || rows > 1;
    if (sheet) {
        const float cellWidth = info.pixels.x / static_cast<float>(columns);
        const float cellHeight = info.pixels.y / static_cast<float>(rows);
        const int frame = std::clamp(sprite.frame, 0, columns * rows - 1);
        cell = {{static_cast<float>(frame % columns) * cellWidth,
                 static_cast<float>(frame / columns) * cellHeight},
                {cellWidth, cellHeight}};
    }
    const float pixelsPerUnit = info.settings.pixelsPerUnit;
    Status done = success();
    switch (sprite.drawMode) {
    case SpriteDrawMode::Simple:
        if (std::abs(sprite.stepSwing) > 0.0001F) {
            // Keep the supplied head/torso intact and step each leg from its own texture region.
            constexpr float torso = 0.73F;
            done = sink.emit({whole.position, {size.x, size.y * torso}},
                             Rect{cell.position, {cell.size.x, cell.size.y * torso}});
            for (int side = 0; side < 2 && done; ++side) {
                const float swing = sprite.stepSwing * scale.y * (side == 0 ? 1.0F : -1.0F);
                done = sink.emit(
                    {whole.position +
                         Vec2{size.x * 0.5F * static_cast<float>(side), size.y * torso},
                     {size.x * 0.5F, std::max(0.001F, size.y * (1.0F - torso) + swing)}},
                    Rect{cell.position + Vec2{cell.size.x * 0.5F * static_cast<float>(side),
                                              cell.size.y * torso},
                         {cell.size.x * 0.5F, cell.size.y * (1.0F - torso)}});
            }
        } else {
            done = sink.emit(whole, sheet ? std::optional<Rect>(cell) : std::nullopt);
        }
        break;
    case SpriteDrawMode::Tiled: {
        Vec2 tile = sprite.tileSize;
        if (!(tile.x > 0.0F) || !(tile.y > 0.0F))
            tile = cell.size / pixelsPerUnit;
        bool tooMany = false;
        done = sink.tiles(whole, cell, tile, tooMany);
        if (done && tooMany) {
            log(LogLevel::Warning, "render",
                "'" + entity.name() +
                    "': too many tiles to draw (raise the tile size); "
                    "drawing it stretched");
            done = sink.emit(whole, cell);
        }
        break;
    }
    case SpriteDrawMode::Sliced: {
        if (!info.settings.hasBorder()) {
            log(LogLevel::Warning, "render",
                "'" + entity.name() + "': '" + sprite.texture.path +
                    "' has no nine-slice border (import settings); drawing it stretched");
            done = sink.emit(whole, cell);
            break;
        }
        const auto &border = info.settings.border; // Left, top, right, bottom, pixels.
        const auto edge = [&](std::size_t index) { return static_cast<float>(border[index]); };
        float left = edge(0) / pixelsPerUnit, top = edge(1) / pixelsPerUnit;
        float right = edge(2) / pixelsPerUnit, bottom = edge(3) / pixelsPerUnit;
        // A sprite smaller than its borders shrinks them together.
        if (left + right > size.x) {
            const float shrink = size.x / (left + right);
            left *= shrink;
            right *= shrink;
        }
        if (top + bottom > size.y) {
            const float shrink = size.y / (top + bottom);
            top *= shrink;
            bottom *= shrink;
        }
        const float xs[4] = {whole.position.x, whole.position.x + left,
                             whole.position.x + size.x - right, whole.position.x + size.x};
        const float ys[4] = {whole.position.y, whole.position.y + top,
                             whole.position.y + size.y - bottom, whole.position.y + size.y};
        const float sx[4] = {cell.position.x, cell.position.x + edge(0),
                             cell.position.x + cell.size.x - edge(2),
                             cell.position.x + cell.size.x};
        const float sy[4] = {cell.position.y, cell.position.y + edge(1),
                             cell.position.y + cell.size.y - edge(3),
                             cell.position.y + cell.size.y};
        for (int row = 0; row < 3 && done; ++row) {
            for (int column = 0; column < 3 && done; ++column) {
                const Rect local{{xs[column], ys[row]},
                                 {xs[column + 1] - xs[column], ys[row + 1] - ys[row]}};
                const Rect source{{sx[column], sy[row]},
                                  {sx[column + 1] - sx[column], sy[row + 1] - sy[row]}};
                if (!(local.size.x > 0.0F) || !(local.size.y > 0.0F) || !(source.size.x > 0.0F) ||
                    !(source.size.y > 0.0F))
                    continue;
                const bool corner = (row != 1) && (column != 1);
                bool tooMany = false;
                if (corner || sprite.sliceFill == SpriteFill::Stretch) {
                    done = sink.emit(local, source);
                } else {
                    done = sink.tiles(local, source, source.size / pixelsPerUnit, tooMany);
                    if (done && tooMany)
                        done = sink.emit(local, source);
                }
            }
        }
        break;
    }
    }
    stats_.quads += sink.quads;
    return done;
}

Status SceneRenderer::drawParticles(Renderer &renderer, const Transform2D &placed,
                                    const ParticleEmitter &emitter, const Rect &visible,
                                    bool culling) {
    if (emitter.particles().empty())
        return success();
    Sprite proto;
    if (emitter.texture.path.empty()) {
        auto builtin = renderer.builtinTexture(
            emitter.particleShape == ParticleShape::Soft     ? BuiltinTexture::Glow
            : emitter.particleShape == ParticleShape::Circle ? BuiltinTexture::Circle
                                                             : BuiltinTexture::White);
        if (!builtin)
            return Error{builtin.error()};
        proto.texture = builtin.value();
    } else {
        const TextureInfo &info = textureFor(renderer, emitter.texture.path);
        if (!info.handle) {
            auto white = renderer.builtinTexture(BuiltinTexture::White);
            if (!white)
                return Error{white.error()};
            proto.texture = white.value();
        } else {
            proto.texture = *info.handle;
        }
    }
    proto.layer = emitter.layer;
    proto.depth = emitter.order;
    proto.blend = emitter.blend == SpriteBlend::Additive ? BlendMode::Additive : BlendMode::Alpha;
    const Transform2D world = emitter.localSpace ? placed : Transform2D{};
    for (const ParticleEmitter::Particle &particle : emitter.particles()) {
        const float t = std::clamp(particle.age / particle.lifetime, 0.0F, 1.0F);
        const float size = particle.size * lerp(1.0F, emitter.endScale, t);
        if (!(size > 0.0F))
            continue;
        const Vec2 position =
            emitter.localSpace ? transformPoint(world, particle.position) : particle.position;
        if (culling &&
            !overlaps({position - Vec2{size, size} * 0.71F, Vec2{size, size} * 1.42F}, visible))
            continue;
        Sprite quad = proto;
        quad.transform.position = position;
        quad.transform.rotationDegrees = particle.rotation + world.rotationDegrees;
        quad.size = {size, size};
        quad.tint = lerp(emitter.startColor, emitter.endColor, t);
        if (quad.tint.a == 0)
            continue;
        if (auto submitted = renderer.submit(quad); !submitted)
            return submitted;
        ++stats_.quads;
        ++stats_.particles;
    }
    return success();
}

Status SceneRenderer::drawLight(Renderer &renderer, const Transform2D &world, const Light2D &light,
                                const Rect &visible, bool culling) {
    const float strength = std::min(std::max(light.currentIntensity(), 0.0F), 1.0F);
    if (strength <= 0.0F || !(light.radius > 0.0F))
        return success();
    const Vec2 center = transformPoint(world, light.offset);
    const float radius =
        light.radius * std::max(std::fabs(world.scale.x), std::fabs(world.scale.y));
    if (culling && !overlaps({center - Vec2{radius, radius}, Vec2{radius, radius} * 2.0F}, visible))
        return success();
    auto glow = renderer.builtinTexture(BuiltinTexture::Glow);
    if (!glow)
        return Error{glow.error()};
    Sprite quad;
    quad.texture = glow.value();
    quad.transform.position = center;
    quad.size = {radius * 2.0F, radius * 2.0F};
    quad.tint = {static_cast<std::uint8_t>(static_cast<float>(light.color.r) * strength),
                 static_cast<std::uint8_t>(static_cast<float>(light.color.g) * strength),
                 static_cast<std::uint8_t>(static_cast<float>(light.color.b) * strength),
                 light.color.a};
    quad.blend = BlendMode::Additive;
    quad.layer = light.layer;
    quad.depth = light.order;
    ++stats_.quads;
    return renderer.submit(quad);
}

std::shared_ptr<const Tileset> SceneRenderer::tilesetFor(const std::string &path) {
    const auto cached = tilesets_.find(path);
    if (cached != tilesets_.end())
        return cached->second;
    std::shared_ptr<const Tileset> loaded;
    std::string failure = "no asset source";
    if (assets_) {
        auto text = assets_->readText(path);
        auto document = text ? Json::parse(text.value()) : Result<Json>(Error{text.error()});
        auto set = document ? Tileset::fromJson(document.value())
                            : Result<Tileset>(Error{document.error()});
        if (set)
            loaded = std::make_shared<const Tileset>(std::move(set.value()));
        else
            failure = set.error();
    }
    if (!loaded)
        log(LogLevel::Warning, "render", "Tileset '" + path + "' unavailable (" + failure + ")");
    tilesets_.emplace(path, loaded);
    return loaded;
}

namespace {
// The eight ways a tile can be turned or mirrored, as the rotation (degrees, clockwise on screen)
// and horizontal flip the renderer draws with (it flips, then rotates). Tiled's rule: transpose
// first, then flip horizontally, then vertically.
struct Orientation {
    float degrees{};
    bool flip{};
};
Orientation orientationOf(std::int32_t flags) {
    struct Matrix {
        int a, b, c, d; // [[a, b], [c, d]]
    };
    const auto multiply = [](Matrix l, Matrix r) {
        return Matrix{l.a * r.a + l.b * r.c, l.a * r.b + l.b * r.d, l.c * r.a + l.d * r.c,
                      l.c * r.b + l.d * r.d};
    };
    Matrix m{1, 0, 0, 1};
    if ((flags & tile::transpose) != 0)
        m = multiply(Matrix{0, 1, 1, 0}, m);
    if ((flags & tile::flipX) != 0)
        m = multiply(Matrix{-1, 0, 0, 1}, m);
    if ((flags & tile::flipY) != 0)
        m = multiply(Matrix{1, 0, 0, -1}, m);
    const Matrix rotations[4] = {{1, 0, 0, 1}, {0, -1, 1, 0}, {-1, 0, 0, -1}, {0, 1, -1, 0}};
    for (int flip = 0; flip < 2; ++flip)
        for (int turn = 0; turn < 4; ++turn) {
            const Matrix candidate =
                multiply(rotations[turn], flip != 0 ? Matrix{-1, 0, 0, 1} : Matrix{1, 0, 0, 1});
            if (candidate.a == m.a && candidate.b == m.b && candidate.c == m.c &&
                candidate.d == m.d)
                return {static_cast<float>(turn) * 90.0F, flip != 0};
        }
    return {};
}
// A stable color for a tile with no art, so a map can be laid out and played with a bare tileset.
Color placeholderColor(int index, bool solid) {
    const auto channel = [&](int mix) {
        return static_cast<std::uint8_t>(90 + ((index * mix) % 120));
    };
    return solid ? Color{channel(37), channel(23), channel(11), 255}
                 : Color{static_cast<std::uint8_t>(channel(11) / 2),
                         static_cast<std::uint8_t>(channel(23) / 2 + 40),
                         static_cast<std::uint8_t>(channel(37) / 2 + 30), 255};
}
} // namespace

Status SceneRenderer::drawTilemap(Renderer &renderer, const Entity &entity, const Tilemap &map,
                                  const WorldView &view, const Rect &visible, bool culling) {
    (void)entity;
    if (map.layers.empty())
        return success();
    const std::shared_ptr<const Tileset> set =
        map.tileset.path.empty() ? nullptr : tilesetFor(map.tileset.path);
    TextureHandle sheet;
    bool haveSheet = false;
    if (set && !set->texture.empty()) {
        const TextureInfo &info = textureFor(renderer, set->texture);
        if (info.handle) {
            sheet = *info.handle;
            haveSheet = true;
        }
    }
    auto white = renderer.builtinTexture(BuiltinTexture::White);
    if (!white)
        return Error{white.error()};
    const Vec2 cell = map.cellSizeInWorld();
    if (!(cell.x > 0.0F) || !(cell.y > 0.0F))
        return success();
    const Vec2 origin = map.entity().worldPosition();
    const WorldLevelSet &levels = map.entity().scene().settings.levels;
    // The visible rectangle in cells (inclusive), widened by one so a partly visible tile draws.
    int firstX = -1000000, firstY = -1000000, lastX = 1000000, lastY = 1000000;
    if (culling) {
        firstX = static_cast<int>(std::floor((visible.position.x - origin.x) / cell.x)) - 1;
        firstY = static_cast<int>(std::floor((visible.position.y - origin.y) / cell.y)) - 1;
        lastX = static_cast<int>(
                    std::floor((visible.position.x + visible.size.x - origin.x) / cell.x)) +
                1;
        lastY = static_cast<int>(
                    std::floor((visible.position.y + visible.size.y - origin.y) / cell.y)) +
                1;
    }
    for (const TileLayer &layer : map.layers) {
        if (!layer.visible || layer.opacity <= 0.0F)
            continue;
        float levelStrength = 1.0F;
        if (view.levelAlpha && !levels.empty()) {
            const int level = levels.indexOf(layer.level);
            if (level >= 0 && static_cast<std::size_t>(level) < view.levelAlpha->size())
                levelStrength = (*view.levelAlpha)[static_cast<std::size_t>(level)];
        }
        const float strength = layer.opacity * levelStrength;
        if (strength <= 0.0F)
            continue;
        const auto alpha = static_cast<std::uint8_t>(std::clamp(strength, 0.0F, 1.0F) * 255.0F);
        for (const auto &[key, chunk] : layer.chunks()) {
            const int cx0 = chunkOrigin(key.x), cy0 = chunkOrigin(key.y);
            if (cx0 + tileChunkSize - 1 < firstX || cx0 > lastX ||
                cy0 + tileChunkSize - 1 < firstY || cy0 > lastY) {
                ++stats_.tileChunksCulled;
                continue;
            }
            for (int ly = 0; ly < tileChunkSize; ++ly) {
                const int y = cy0 + ly;
                if (y < firstY || y > lastY)
                    continue;
                for (int lx = 0; lx < tileChunkSize; ++lx) {
                    const int x = cx0 + lx;
                    const std::int32_t value = chunk.get(lx, ly);
                    if (x < firstX || x > lastX || tile::empty(value))
                        continue;
                    const int index = tile::index(value);
                    const int frame = set ? set->frameAt(index, view.time) : index;
                    Sprite quad;
                    quad.transform.position = {origin.x + (static_cast<float>(x) + 0.5F) * cell.x,
                                               origin.y + (static_cast<float>(y) + 0.5F) * cell.y};
                    quad.size = cell;
                    quad.layer = map.drawLayer + layer.sortLayer;
                    quad.depth = layer.ySort
                                     ? origin.y + static_cast<float>(y + 1) * cell.y + layer.order
                                     : layer.order;
                    const Orientation orientation = orientationOf(tile::flags(value));
                    quad.transform.rotationDegrees = orientation.degrees;
                    quad.flipHorizontal = orientation.flip;
                    if (haveSheet) {
                        quad.texture = sheet;
                        quad.source = set->sourceRect(frame);
                        quad.tint = {255, 255, 255, alpha};
                    } else {
                        quad.texture = white.value();
                        Color color = placeholderColor(frame, set && set->properties(index).solid);
                        color.a = alpha;
                        quad.tint = color;
                    }
                    if (auto submitted = renderer.submit(quad); !submitted)
                        return submitted;
                    ++stats_.tiles;
                    ++stats_.quads;
                }
            }
        }
    }
    return success();
}

Status SceneRenderer::drawWorld(Renderer &renderer, const Scene &scene, const WorldView &view) {
    stats_ = {};
    const bool culling = view.viewport.x > 0.0F && view.viewport.y > 0.0F;
    const Rect visible = culling ? view.camera.visibleWorld(view.viewport) : Rect{};
    static const std::type_index spriteType(typeid(SpriteRenderer));
    static const std::type_index emitterType(typeid(ParticleEmitter));
    static const std::type_index lightType(typeid(Light2D));
    static const std::type_index tilemapType(typeid(Tilemap));
    for (const EntityId id : scene.orderedIds()) {
        const Entity *entity = scene.find(id);
        if (!entity || !entity->activeInHierarchy() ||
            (view.editorView && entity->hiddenInHierarchy()))
            continue;
        // A world level the game view is not showing hides what is on it (a floor above the one
        // the player is on); a faded one tints it.
        float levelStrength = 1.0F;
        if (view.levelAlpha && !scene.settings.levels.empty()) {
            const int level = levelOf(*entity);
            if (level >= 0 && static_cast<std::size_t>(level) < view.levelAlpha->size())
                levelStrength = (*view.levelAlpha)[static_cast<std::size_t>(level)];
        }
        // Where the entity is drawn, worked out once and only if it has something to draw.
        std::optional<Transform2D> placed;
        const auto placement = [&]() -> const Transform2D & {
            if (!placed)
                placed = view.interpolate ? entity->renderTransform(view.alpha)
                                          : entity->worldTransform();
            return *placed;
        };
        for (const auto &component : entity->components()) {
            if (!component->enabled)
                continue;
            const std::type_index type = component->type().type;
            if (type == tilemapType) {
                if (auto drawn =
                        drawTilemap(renderer, *entity, static_cast<const Tilemap &>(*component),
                                    view, visible, culling);
                    !drawn)
                    return drawn;
                continue;
            }
            if (levelStrength <= 0.0F)
                continue;
            if (type == emitterType) {
                if (auto drawn = drawParticles(renderer, placement(),
                                               static_cast<const ParticleEmitter &>(*component),
                                               visible, culling);
                    !drawn)
                    return drawn;
                continue;
            }
            if (type == lightType) {
                if (auto drawn =
                        drawLight(renderer, placement(), static_cast<const Light2D &>(*component),
                                  visible, culling);
                    !drawn)
                    return drawn;
                continue;
            }
            if (type != spriteType)
                continue;
            const auto &sprite = static_cast<const SpriteRenderer &>(*component);
            if (!sprite.enabled || !sprite.visible || sprite.color.a == 0)
                continue;
            if (auto drawn = drawSprite(renderer, *entity, placement(), sprite, view, visible,
                                        culling, levelStrength);
                !drawn)
                return drawn;
        }
    }
    return success();
}

Status SceneRenderer::drawColliders(Renderer &renderer, const Scene &scene, int layer) {
    constexpr int circleSegments = 24;
    for (const EntityId id : scene.orderedIds()) {
        const Entity *entity = scene.find(id);
        if (!entity || !entity->activeInHierarchy())
            continue;
        for (const Collider *collider : entity->getAll<Collider>()) {
            if (!collider->enabled)
                continue;
            const Transform2D world = entity->worldTransform();
            const Vec2 scale{std::fabs(world.scale.x), std::fabs(world.scale.y)};
            const Vec2 extent = hadamard(collider->size, scale);
            const Vec2 center = transformPoint(world, collider->offset);
            const float angle = degreesToRadians(world.rotationDegrees);
            const Color color = colliderColor(*entity, *collider);
            std::vector<Vec2> outline;
            const auto local = [&](Vec2 point) { return center + rotated(point, angle); };
            const auto arc = [&](Vec2 middle, float radius, float from, float to, int steps) {
                for (int i = 0; i <= steps; ++i) {
                    const float t =
                        from + (to - from) * static_cast<float>(i) / static_cast<float>(steps);
                    outline.push_back(local(middle + Vec2{std::cos(t), std::sin(t)} * radius));
                }
            };
            if (collider->shape == ColliderShape::Box) {
                const Vec2 h = extent * 0.5F;
                const float radius = std::clamp(collider->cornerRadius * std::min(scale.x, scale.y),
                                                0.0F, 0.49F * std::min(extent.x, extent.y));
                const Vec2 cut{std::min(collider->chamfer.x * scale.x, 0.49F * extent.x),
                               std::min(collider->chamfer.y * scale.y, 0.49F * extent.y)};
                if (cut.x > 0.005F && cut.y > 0.005F) {
                    outline = {local({-h.x + cut.x, -h.y}), local({h.x - cut.x, -h.y}),
                               local({h.x, -h.y + cut.y}),  local({h.x, h.y - cut.y}),
                               local({h.x - cut.x, h.y}),   local({-h.x + cut.x, h.y}),
                               local({-h.x, h.y - cut.y}),  local({-h.x, -h.y + cut.y})};
                } else if (radius > 0.0F) {
                    // Each corner is a quarter circle around a point `radius` inside it.
                    arc({h.x - radius, -h.y + radius}, radius, -pi / 2, 0, 6);
                    arc({h.x - radius, h.y - radius}, radius, 0, pi / 2, 6);
                    arc({-h.x + radius, h.y - radius}, radius, pi / 2, pi, 6);
                    arc({-h.x + radius, -h.y + radius}, radius, pi, 3 * pi / 2, 6);
                } else {
                    outline = {local({-h.x, -h.y}), local({h.x, -h.y}), local({h.x, h.y}),
                               local({-h.x, h.y})};
                }
            } else if (collider->shape == ColliderShape::Wedge) {
                for (const Vec2 corner : wedgePoints(extent * 0.5F, world.scale))
                    outline.push_back(local(corner));
            } else if (collider->shape == ColliderShape::Circle) {
                arc({0, 0}, 0.5F * collider->size.x * std::max(scale.x, scale.y), 0, 2 * pi,
                    circleSegments);
                outline.pop_back();
            } else {
                const float radius = extent.x * 0.5F;
                const float half = std::max((extent.y - extent.x) * 0.5F, 0.0F);
                arc({0, -half}, radius, pi, 2 * pi, circleSegments / 2);
                arc({0, half}, radius, 0, pi, circleSegments / 2);
            }
            for (std::size_t i = 0; i < outline.size(); ++i)
                if (auto drawn = renderer.debugLine(outline[i], outline[(i + 1) % outline.size()],
                                                    color, layer);
                    !drawn)
                    return drawn;
        }
    }
    return success();
}

Status SceneRenderer::drawUi(Renderer &renderer, const Scene &scene, Vec2 viewport,
                             const Blackboard *variables) {
    auto white = renderer.builtinTexture(BuiltinTexture::White);
    if (!white)
        return Error{white.error()};
    for (const EntityId id : scene.orderedIds()) {
        const Entity *entity = scene.find(id);
        if (!entity || !entity->activeInHierarchy())
            continue;
        for (const UiPanel *panel : entity->getAll<UiPanel>()) {
            if (!panel->enabled || panel->color.a == 0 || !(panel->size.x > 0) ||
                !(panel->size.y > 0))
                continue;
            Sprite sprite;
            sprite.texture = white.value();
            sprite.size = panel->size;
            sprite.anchor = {0.0F, 0.0F};
            sprite.transform.position =
                anchoredTopLeft(panel->anchor, viewport, panel->size, panel->offset);
            sprite.tint = panel->color;
            if (const auto *button = entity->get<UiButton>();
                button && button->enabled && button->hovered())
                sprite.tint = button->pressed() ? button->pressedColor : button->hoverColor;
            sprite.layer = panel->layer;
            if (auto submitted = renderer.submit(sprite); !submitted)
                return submitted;
        }
        for (const UiImage *image : entity->getAll<UiImage>()) {
            if (!image->enabled || image->color.a == 0 ||
                (!image->fillScreen && (!(image->size.x > 0) || !(image->size.y > 0))))
                continue;
            Sprite sprite;
            sprite.size = image->fillScreen ? viewport : image->size;
            sprite.anchor = {0.0F, 0.0F};
            sprite.transform.position =
                image->fillScreen
                    ? Vec2{0.0F, 0.0F}
                    : anchoredTopLeft(image->anchor, viewport, image->size, image->offset);
            sprite.tint = image->color;
            sprite.layer = image->layer;
            if (image->texture.path.empty()) {
                sprite.texture = white.value();
            } else {
                const TextureInfo &info = textureFor(renderer, image->texture.path);
                if (!info.handle) {
                    sprite.texture = white.value();
                    sprite.tint = missingTextureColor;
                } else {
                    sprite.texture = *info.handle;
                    const int columns = image->columns > 1 ? image->columns : info.settings.columns;
                    const int rows = image->rows > 1 ? image->rows : info.settings.rows;
                    if (columns > 1 || rows > 1) {
                        const float cellWidth = info.pixels.x / static_cast<float>(columns);
                        const float cellHeight = info.pixels.y / static_cast<float>(rows);
                        const int frame = std::clamp(image->frame, 0, columns * rows - 1);
                        sprite.source = Rect{{static_cast<float>(frame % columns) * cellWidth,
                                              static_cast<float>(frame / columns) * cellHeight},
                                             {cellWidth, cellHeight}};
                    }
                }
            }
            if (auto submitted = renderer.submit(sprite); !submitted)
                return submitted;
        }
        for (const UiText *label : entity->getAll<UiText>()) {
            if (!label->enabled || label->color.a == 0)
                continue;
            const std::string text = variables ? variables->format(label->text) : label->text;
            if (text.empty())
                continue;
            const float scale = std::max(label->scale, 1.0F);
            const Vec2 size = BitmapFont::measure(text, scale);
            const Vec2 origin = anchoredTopLeft(label->anchor, viewport, size, label->offset);
            // Layers of a text entity sit above panels sharing the layer; the shadow just below the
            // text.
            if (label->shadow) {
                const Color shadow{0, 0, 0, static_cast<std::uint8_t>(label->color.a * 0.75F)};
                if (auto drawn = font_->draw(renderer, text, origin + Vec2{scale, scale}, scale,
                                             shadow, label->layer, -0.5F);
                    !drawn)
                    return drawn;
            }
            if (auto drawn =
                    font_->draw(renderer, text, origin, scale, label->color, label->layer, 0.0F);
                !drawn)
                return drawn;
        }
    }
    // Inventory HUD projects reserve bands outside the world viewport.
    if (variables && variables->number("inventory_hud") != 0) {
        const float unit = std::min(viewport.x / 1280.0F, viewport.y / 720.0F);
        const float bottom = viewport.y - 100.0F * unit;
        auto rectangle = [&](Vec2 at, Vec2 size, Color color, int layer = 6000) {
            if (size.x <= 0 || size.y <= 0)
                return success();
            Sprite sprite;
            sprite.texture = white.value();
            sprite.anchor = {0, 0};
            sprite.transform.position = at;
            sprite.size = size;
            sprite.tint = color;
            sprite.layer = layer;
            return renderer.submit(sprite);
        };
        auto text = [&](const std::string &line, Vec2 at, float scale, Color color,
                        int layer = 6002) {
            return font_->draw(renderer, line, at, scale * unit, color, layer);
        };
        const Color ink{225, 218, 190, 255}, gold{229, 181, 97, 255};
        if (auto status = rectangle({}, {viewport.x, 40 * unit}, {22, 25, 31, 255}); !status)
            return status;
        if (auto status = rectangle({0, bottom}, {viewport.x, 100 * unit}, {22, 25, 31, 255});
            !status)
            return status;
        if (auto status = rectangle({0, bottom}, {viewport.x, unit}, {84, 78, 63, 255}); !status)
            return status;
        if (auto status = text(variables->text("inventory_title"), {20 * unit, 13 * unit}, 2, gold);
            !status)
            return status;
        const float suspicion = static_cast<float>(variables->number("patrol_suspicion"));
        const std::string routine =
            variables->text(suspicion > 0.02F ? "inventory_alert" : "inventory_status");
        if (auto status = text(routine, {viewport.x - 360 * unit, 14 * unit}, 1.7F,
                               suspicion > 0.02F ? Color{245, 125, 101, 255} : ink);
            !status)
            return status;
        if (suspicion > 0)
            if (auto status = rectangle({viewport.x - 360 * unit, 34 * unit},
                                        {320 * unit * suspicion, 3 * unit}, {245, 125, 101, 255});
                !status)
                return status;
        const std::string notice = variables->text("interaction_notice");
        if (auto status = text(notice.empty() ? variables->text("objective") : notice,
                               {20 * unit, bottom + 12 * unit}, 1.6F, gold);
            !status)
            return status;
        if (auto status = text(variables->text("interaction_prompt"),
                               {20 * unit, bottom + 37 * unit}, 1.8F, ink);
            !status)
            return status;
        if (auto status = text(variables->text("inventory_controls"),
                               {20 * unit, bottom + 76 * unit}, 1.2F, {167, 167, 149, 255});
            !status)
            return status;
        const float first = viewport.x - 364 * unit;
        for (int i = 0; i < 6; ++i) {
            const Vec2 at{first + static_cast<float>(i) * 58 * unit, bottom + 29 * unit};
            const std::string prefix = "inventory_slot_" + std::to_string(i);
            const auto path = variables->text(prefix + "_icon");
            const Color border = path.empty() ? Color{54, 69, 85, 255} : Color{147, 117, 60, 255};
            if (auto status = rectangle(at, {48 * unit, 48 * unit}, border); !status)
                return status;
            if (auto status =
                    rectangle(at + Vec2{unit, unit}, {46 * unit, 46 * unit}, {18, 29, 42, 255});
                !status)
                return status;
            if (!path.empty()) {
                const auto &info = textureFor(renderer, path);
                if (info.handle) {
                    Sprite icon;
                    icon.texture = *info.handle;
                    icon.anchor = {0, 0};
                    icon.transform.position = at + Vec2{6 * unit, 6 * unit};
                    icon.size = {36 * unit, 36 * unit};
                    icon.layer = 6001;
                    if (auto status = renderer.submit(icon); !status)
                        return status;
                }
                if (auto status = text(variables->text(prefix + "_name"),
                                       at + Vec2{-3 * unit, 52 * unit}, .8F, ink);
                    !status)
                    return status;
            }
        }
        if (variables->text("level_state") == "complete") {
            const Vec2 at{(viewport.x - 650 * unit) / 2, (viewport.y - 150 * unit) / 2};
            if (auto status = rectangle(at, {650 * unit, 150 * unit}, {22, 25, 31, 250}, 8500);
                !status)
                return status;
            if (auto status = text(variables->text("inventory_completion_title"),
                                   at + Vec2{32 * unit, 32 * unit}, 2.8F, gold, 8502);
                !status)
                return status;
            if (auto status = text(variables->text("inventory_completion_text"),
                                   at + Vec2{32 * unit, 80 * unit}, 1.8F, ink, 8502);
                !status)
                return status;
            const std::string finishControls = variables->number("level_has_next") != 0
                                                   ? variables->text("inventory_continue")
                                                   : variables->text("inventory_restart");
            if (auto status =
                    text(finishControls, at + Vec2{32 * unit, 116 * unit}, 1.5F, ink, 8502);
                !status)
                return status;
        }
    }
    if (variables && variables->number("search_ui_active") != 0) {
        const float unit = std::min(viewport.x / 1280.0F, viewport.y / 720.0F);
        const bool busy = variables->number("search_ui_busy") != 0;
        const Vec2 at{busy ? viewport.x - 350 * unit : (viewport.x - 610 * unit) / 2,
                      busy ? 60 * unit : viewport.y - 286 * unit};
        auto rect = [&](Vec2 offset, Vec2 size, Color color, int layer = 8100) {
            if (size.x <= 0 || size.y <= 0)
                return success();
            Sprite sprite;
            sprite.texture = white.value();
            sprite.anchor = {0, 0};
            sprite.transform.position = at + offset * unit;
            sprite.size = size * unit;
            sprite.tint = color;
            sprite.layer = layer;
            return renderer.submit(sprite);
        };
        auto label = [&](const std::string &line, Vec2 offset, float scale, Color color) {
            return font_->draw(renderer, line, at + offset * unit, scale * unit, color, 8102);
        };
        if (auto status = rect({}, busy ? Vec2{330, 100} : Vec2{610, 170}, {22, 25, 31, 250});
            !status)
            return status;
        if (auto status = rect({}, busy ? Vec2{330, 2} : Vec2{610, 2}, {229, 181, 97, 255});
            !status)
            return status;
        if (auto status =
                label(variables->text("search_ui_title"), busy ? Vec2{16, 12} : Vec2{24, 18},
                      busy ? 1.5F : 2.3F, {229, 181, 97, 255});
            !status)
            return status;
        const std::string message = variables->text("search_ui_message");
        // Authored action copy wraps at spaces rather than splitting words.
        std::string wrapped;
        std::size_t column = 0;
        for (std::size_t start = 0; start < message.size();) {
            auto end = message.find(' ', start);
            if (end == std::string::npos)
                end = message.size();
            const auto word = message.substr(start, end - start);
            if (column && column + word.size() + 1 > (busy ? 38U : 48U)) {
                wrapped += '\n';
                column = 0;
            }
            if (column) {
                wrapped += ' ';
                ++column;
            }
            wrapped += word;
            column += word.size();
            start = end + 1;
        }
        if (auto status = label(wrapped, busy ? Vec2{16, 36} : Vec2{24, 58}, busy ? 1.2F : 1.8F,
                                {225, 218, 190, 255});
            !status)
            return status;
        if (busy) {
            if (auto status = rect({16, 62}, {298, 6}, {61, 60, 53, 255}); !status)
                return status;
            if (auto status =
                    rect({16, 62},
                         {298 * static_cast<float>(variables->number("search_ui_progress")), 6},
                         {229, 181, 97, 255}, 8101);
                !status)
                return status;
        } else {
            const auto path = variables->text("search_ui_icon");
            if (!path.empty()) {
                const auto &info = textureFor(renderer, path);
                if (info.handle) {
                    Sprite icon;
                    icon.texture = *info.handle;
                    icon.anchor = {0, 0};
                    icon.layer = 8101;
                    icon.transform.position = at + Vec2{530 * unit, 100 * unit};
                    icon.size = {48 * unit, 48 * unit};
                    if (auto status = renderer.submit(icon); !status)
                        return status;
                }
            }
        }
        if (auto status = label(busy ? "ESC - CANCEL" : "E - CONTINUE / ESC - CLOSE",
                                busy ? Vec2{16, 82} : Vec2{24, 142}, busy ? 1.1F : 1.3F,
                                {167, 167, 149, 255});
            !status)
            return status;
    }
    // Wiring UI reads presentation state published by the gameplay component.
    if (variables && variables->number("wire_ui_active") != 0) {
        const float scale = std::min(viewport.x / 900.0F, viewport.y / 600.0F);
        const Vec2 origin{(viewport.x - 820.0F * scale) * 0.5F,
                          (viewport.y - 480.0F * scale) * 0.5F};
        auto rect = [&](Vec2 at, Vec2 size, Color color, int layer) {
            Sprite quad;
            quad.texture = white.value();
            quad.anchor = {0, 0};
            quad.transform.position = at;
            quad.size = size;
            quad.tint = color;
            quad.layer = layer;
            return renderer.submit(quad);
        };
        auto label = [&](const std::string &text, float x, float y, Color color,
                         float size = 2.0F) {
            return font_->draw(renderer, text, origin + Vec2{x, y} * scale, size * scale, color,
                               9003);
        };
        if (auto drawn = rect({}, viewport, {5, 10, 19, 190}, 9000); !drawn)
            return drawn;
        if (auto drawn = rect(origin, Vec2{820, 480} * scale, {21, 34, 48, 255}, 9001); !drawn)
            return drawn;
        if (auto drawn = label("CONNECT THE WIRES", 32, 26, {255, 221, 142, 255}, 3); !drawn)
            return drawn;
        if (auto drawn =
                label(variables->text("wire_ui_message"), 32, 78, {233, 239, 246, 255}, 1.5F);
            !drawn)
            return drawn;
        if (variables->number("wire_ui_ready") != 0) {
            const Color colors[] = {{250, 121, 117, 255},
                                    {107, 193, 250, 255},
                                    {248, 210, 106, 255},
                                    {127, 224, 171, 255}};
            const int selectedSource = static_cast<int>(variables->number("wire_ui_source"));
            const int selectedTarget = static_cast<int>(variables->number("wire_ui_target"));
            const int connected = static_cast<int>(variables->number("wire_ui_connected"));
            for (int i = 0; i < 4; ++i) {
                const float y = 130.0F + static_cast<float>(i) * 60.0F;
                const int right = (i + 2) % 4;
                if (auto drawn =
                        rect(origin + Vec2{40, y} * scale, Vec2{165, 40} * scale,
                             selectedSource == i ? Color{63, 82, 101, 255} : Color{30, 46, 63, 255},
                             9002);
                    !drawn)
                    return drawn;
                if (auto drawn =
                        rect(origin + Vec2{615, y} * scale, Vec2{165, 40} * scale,
                             selectedTarget == i ? Color{63, 82, 101, 255} : Color{30, 46, 63, 255},
                             9002);
                    !drawn)
                    return drawn;
                const std::string left = (selectedSource == i ? "> " : "  ") +
                                         std::string(1, static_cast<char>('A' + i)) +
                                         ((connected & (1 << i)) ? "  OK" : "");
                const std::string target = (selectedTarget == i ? "> " : "  ") +
                                           std::string(1, static_cast<char>('A' + right));
                if (auto drawn = label(left, 50, y + 12, colors[i]); !drawn)
                    return drawn;
                if (auto drawn = label(target, 625, y + 12, colors[right]); !drawn)
                    return drawn;
                if (connected & (1 << i)) {
                    const Vec2 a = origin + Vec2{205, y + 20} * scale;
                    const Vec2 b =
                        origin +
                        Vec2{615, 150.0F + static_cast<float>((i + 2) % 4) * 60.0F} * scale;
                    for (int width = -1; width <= 1; ++width)
                        if (auto drawn = renderer.debugLine(a + Vec2{0, static_cast<float>(width)},
                                                            b + Vec2{0, static_cast<float>(width)},
                                                            colors[i], 9002);
                            !drawn)
                            return drawn;
                }
            }
            if (auto drawn = label("W/S: WIRE   A/D: TERMINAL   E: CONNECT   ESC: CLOSE", 32, 410,
                                   {216, 228, 242, 255}, 1.5F);
                !drawn)
                return drawn;
        } else if (auto drawn = label("E / ESC: CLOSE AND FIND THE MISSING SUPPLIES", 32, 410,
                                      {216, 228, 242, 255}, 1.5F);
                   !drawn)
            return drawn;
    }
    // Conversations are screen-space presentations, independent of an NPC's world sprite.
    // Only the first active dialogue is shown; gameplay owns its pages and input lifecycle.
    for (const EntityId id : scene.orderedIds()) {
        const Entity *entity = scene.find(id);
        const auto *dialogue =
            entity && entity->activeInHierarchy() ? entity->get<Dialogue>() : nullptr;
        if (!dialogue || !dialogue->enabled || !dialogue->active())
            continue;
        const DialoguePage &page = dialogue->currentPage();
        const float unit = std::min(viewport.x / 1280.0F, viewport.y / 720.0F);
        const bool hasPortrait = !page.portrait.path.empty();
        const bool portraitRight = page.side == "right";
        const float width = std::min(1040 * unit, viewport.x - 48 * unit);
        const float portraitSpace = hasPortrait ? 256 * unit : 0;
        const float scale = 1.8F * unit;
        const float lineHeight = BitmapFont::lineAdvance * scale;
        const std::size_t columns =
            std::max<std::size_t>(1, static_cast<std::size_t>((width - portraitSpace - 64 * unit) /
                                                              (BitmapFont::advance * scale)));
        const auto fullText = wrapWords(page.text, columns);
        const std::size_t lines =
            1 + static_cast<std::size_t>(std::count(fullText.begin(), fullText.end(), '\n'));
        std::size_t choiceLines = 0;
        for (const auto &choice : dialogue->choices()) {
            const auto wrappedChoice = wrapWords("> " + choice, columns);
            choiceLines += 1 + static_cast<std::size_t>(
                                   std::count(wrappedChoice.begin(), wrappedChoice.end(), '\n'));
        }
        const float height =
            std::max(300 * unit, 126 * unit + static_cast<float>(lines + choiceLines) * lineHeight);
        const float progress = std::clamp(dialogue->presentationTime() / .18F, 0.0F, 1.0F);
        const float arrival = 1.0F - std::pow(1.0F - progress, 3.0F);
        const Vec2 boxPos{(viewport.x - width) * .5F,
                          viewport.y - 124 * unit - height + 12 * unit * (1 - arrival)};
        auto rectangle = [&](Vec2 at, Vec2 size, Color color, int layer) {
            Sprite quad;
            quad.texture = white.value();
            quad.anchor = {0, 0};
            quad.transform.position = at;
            quad.size = size;
            quad.tint = color;
            quad.layer = layer;
            return renderer.submit(quad);
        };
        if (auto status =
                rectangle({}, viewport, {5, 9, 16, static_cast<std::uint8_t>(105 * arrival)}, 8000);
            !status)
            return status;
        if (auto status =
                rectangle(boxPos + Vec2{0, 6 * unit}, {width, height}, {0, 0, 0, 90}, 8001);
            !status)
            return status;
        if (auto status = rectangle(boxPos, {width, height}, {22, 25, 31, 255}, 8002); !status)
            return status;
        if (auto status = rectangle(boxPos, {width, 2 * unit}, {229, 181, 97, 255}, 8003); !status)
            return status;
        float textX = boxPos.x + 32 * unit;
        if (hasPortrait) {
            const Vec2 portraitAt =
                boxPos + Vec2{portraitRight ? width - 248 * unit : 16 * unit, 16 * unit};
            if (auto status = rectangle(portraitAt, {232 * unit, height - 32 * unit},
                                        {36, 42, 52, 255}, 8003);
                !status)
                return status;
            const TextureInfo &portrait = textureFor(renderer, page.portrait.path);
            if (portrait.handle && portrait.pixels.x > 0 && portrait.pixels.y > 0) {
                const float fit = std::min(224 * unit / portrait.pixels.x,
                                           (height - 40 * unit) / portrait.pixels.y);
                Sprite artwork;
                artwork.texture = *portrait.handle;
                artwork.anchor = {0, 0};
                artwork.layer = 8004;
                artwork.size = portrait.pixels * fit;
                artwork.transform.position = portraitAt + Vec2{(232 * unit - artwork.size.x) * .5F,
                                                               height - 32 * unit - artwork.size.y};
                if (auto status = renderer.submit(artwork); !status)
                    return status;
            }
            if (!portraitRight)
                textX += portraitSpace;
        }
        const Color ink{225, 218, 190, 255}, gold{229, 181, 97, 255};
        if (auto status = font_->draw(renderer, page.speaker, {textX, boxPos.y + 28 * unit},
                                      2.5F * unit, gold, 8005);
            !status)
            return status;
        if (auto status = font_->draw(renderer, wrapWords(dialogue->visibleText(), columns),
                                      {textX, boxPos.y + 76 * unit}, scale, ink, 8005);
            !status)
            return status;
        if (dialogue->choosing()) {
            float y = boxPos.y + 88 * unit + static_cast<float>(lines) * lineHeight;
            for (std::size_t i = 0; i < dialogue->choices().size(); ++i) {
                const bool selected = static_cast<int>(i) == dialogue->selectedChoice();
                const auto line =
                    wrapWords((selected ? "> " : "  ") + dialogue->choices()[i], columns);
                if (auto status =
                        font_->draw(renderer, line, {textX, y}, scale, selected ? gold : ink, 8005);
                    !status)
                    return status;
                y +=
                    static_cast<float>(1 + std::count(line.begin(), line.end(), '\n')) * lineHeight;
            }
        }
        const std::string controls = dialogue->choosing() ? "ARROWS SELECT   E CONFIRM   ESC CLOSE"
                                                          : "E CONTINUE   ESC CLOSE";
        if (auto status = font_->draw(renderer, controls, {textX, boxPos.y + height - 30 * unit},
                                      1.4F * unit, gold, 8005);
            !status)
            return status;
        break;
    }
    return success();
}
} // namespace yk
