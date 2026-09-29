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
    const int index = static_cast<int>(anchor);
    const int column = index % 3, row = index / 3; // Left/center/right, top/middle/bottom.
    const auto place = [](int slot, float extent, float item, float delta) {
        if (slot == 0)
            return delta;
        if (slot == 1)
            return (extent - item) * 0.5F + delta;
        return extent - item - delta;
    };
    return {place(column, viewport.x, size.x, offset.x), place(row, viewport.y, size.y, offset.y)};
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
    const auto found = textures_.find(path);
    if (found == textures_.end())
        return;
    // Builtins are shared; anything file backed is released so the file is decoded again.
    if (found->second.handle && path.rfind("builtin:", 0) != 0)
        renderer.release(*found->second.handle);
    textures_.erase(found);
}

Status SceneRenderer::drawSprite(Renderer &renderer, const Entity &entity,
                                 const SpriteRenderer &sprite, const WorldView &view,
                                 const Rect &visible, bool culling) {
    ++stats_.sprites;
    const Transform2D world = entity.worldTransform();
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

    QuadSink sink{renderer,
                  {},
                  center,
                  degreesToRadians(world.rotationDegrees),
                  sprite.flipX != (world.scale.x < 0.0F),
                  culling && world.rotationDegrees == 0.0F,
                  visible,
                  0};
    sink.proto.tint = sprite.color;
    sink.proto.layer = sprite.layer;
    sink.proto.depth = sprite.order;
    sink.proto.transform.rotationDegrees = world.rotationDegrees;
    sink.proto.flipHorizontal = sink.flip;
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
        done = sink.emit(whole, sheet ? std::optional<Rect>(cell) : std::nullopt);
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
        const auto edge = [&](int index) { return static_cast<float>(border[index]); };
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

Status SceneRenderer::drawParticles(Renderer &renderer, const Entity &entity,
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
    const Transform2D world = emitter.localSpace ? entity.worldTransform() : Transform2D{};
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

Status SceneRenderer::drawLight(Renderer &renderer, const Entity &entity, const Light2D &light,
                                const Rect &visible, bool culling) {
    const float strength = std::min(std::max(light.currentIntensity(), 0.0F), 1.0F);
    if (strength <= 0.0F || !(light.radius > 0.0F))
        return success();
    const Transform2D world = entity.worldTransform();
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

Status SceneRenderer::drawWorld(Renderer &renderer, const Scene &scene, const WorldView &view) {
    stats_ = {};
    const bool culling = view.viewport.x > 0.0F && view.viewport.y > 0.0F;
    const Rect visible = culling ? view.camera.visibleWorld(view.viewport) : Rect{};
    static const std::type_index spriteType(typeid(SpriteRenderer));
    static const std::type_index emitterType(typeid(ParticleEmitter));
    static const std::type_index lightType(typeid(Light2D));
    for (const EntityId id : scene.orderedIds()) {
        const Entity *entity = scene.find(id);
        if (!entity || !entity->activeInHierarchy() ||
            (view.editorView && entity->hiddenInHierarchy()))
            continue;
        for (const auto &component : entity->components()) {
            if (!component->enabled)
                continue;
            const std::type_index type = component->type().type;
            if (type == emitterType) {
                if (auto drawn = drawParticles(renderer, *entity,
                                               static_cast<const ParticleEmitter &>(*component),
                                               visible, culling);
                    !drawn)
                    return drawn;
                continue;
            }
            if (type == lightType) {
                if (auto drawn =
                        drawLight(renderer, *entity, static_cast<const Light2D &>(*component),
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
            if (auto drawn = drawSprite(renderer, *entity, sprite, view, visible, culling); !drawn)
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
                outline = {local({-h.x, -h.y}), local({h.x, -h.y}), local({h.x, h.y}),
                           local({-h.x, h.y})};
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
    return success();
}
} // namespace yk
