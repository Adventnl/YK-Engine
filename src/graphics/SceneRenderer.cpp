#include "yk/graphics/SceneRenderer.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
namespace {
constexpr Color missingTextureColor{255, 0, 220, 255};

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
} // namespace

Result<std::unique_ptr<SceneRenderer>> SceneRenderer::create(Renderer &renderer,
                                                             const AssetSource *assets) {
    auto font = BitmapFont::create(renderer);
    if (!font)
        return Error{font.error()};
    auto self = std::unique_ptr<SceneRenderer>(new SceneRenderer());
    self->assets_ = assets;
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
    for (const EntityId id : scene.hierarchyOrder()) {
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

std::optional<TextureHandle> SceneRenderer::textureFor(Renderer &renderer,
                                                       const std::string &path) {
    if (path == "builtin:white" || path == "builtin:circle") {
        auto builtin = renderer.builtinTexture(path == "builtin:white" ? BuiltinTexture::White
                                                                       : BuiltinTexture::Circle);
        return builtin ? std::optional<TextureHandle>(builtin.value()) : std::nullopt;
    }
    const auto cached = textures_.find(path);
    if (cached != textures_.end())
        return cached->second;
    std::optional<TextureHandle> loaded;
    std::string failure = "no asset source";
    if (assets_) {
        const std::filesystem::path file = assets_->filePath(path);
        if (file.empty()) {
            failure = "not a file-backed asset";
        } else {
            const std::string extension = file.extension().string();
            auto texture = extension == ".bmp" ? renderer.loadBmp(file) : renderer.loadPng(file);
            if (texture)
                loaded = texture.value();
            else
                failure = texture.error();
        }
    }
    if (!loaded)
        log(LogLevel::Warning, "render",
            "Texture '" + path + "' unavailable (" + failure + "); drawing a placeholder");
    textures_[path] = loaded;
    return loaded;
}

Status SceneRenderer::drawWorld(Renderer &renderer, const Scene &scene) {
    auto white = renderer.builtinTexture(BuiltinTexture::White);
    auto circle = renderer.builtinTexture(BuiltinTexture::Circle);
    if (!white || !circle)
        return Error{"Builtin textures unavailable"};
    for (const EntityId id : scene.hierarchyOrder()) {
        const Entity *entity = scene.find(id);
        if (!entity || !entity->activeInHierarchy())
            continue;
        for (const SpriteRenderer *sprite : entity->getAll<SpriteRenderer>()) {
            if (!sprite->enabled || !sprite->visible || sprite->color.a == 0)
                continue;
            const Transform2D world = entity->worldTransform();
            Sprite out;
            out.tint = sprite->color;
            const Vec2 scale{std::fabs(world.scale.x), std::fabs(world.scale.y)};
            out.size = hadamard(sprite->size, scale);
            if (!(out.size.x > 0.0F) || !(out.size.y > 0.0F))
                continue;
            out.transform.position = transformPoint(world, sprite->offset);
            out.transform.rotationDegrees = world.rotationDegrees;
            out.flipHorizontal = sprite->flipX != (world.scale.x < 0.0F);
            out.layer = sprite->layer;
            out.depth = sprite->order;
            if (sprite->texture.path.empty()) {
                out.texture =
                    sprite->shape == SpriteShape::Ellipse ? circle.value() : white.value();
            } else if (const auto texture = textureFor(renderer, sprite->texture.path)) {
                out.texture = *texture;
                if (sprite->columns > 1 || sprite->rows > 1) {
                    const Vec2 pixels = renderer.textureSize(*texture);
                    const float cellWidth = pixels.x / static_cast<float>(sprite->columns);
                    const float cellHeight = pixels.y / static_cast<float>(sprite->rows);
                    const int frame =
                        std::clamp(sprite->frame, 0, sprite->columns * sprite->rows - 1);
                    out.source = Rect{{static_cast<float>(frame % sprite->columns) * cellWidth,
                                       static_cast<float>(frame / sprite->columns) * cellHeight},
                                      {cellWidth, cellHeight}};
                }
            } else {
                out.texture = white.value();
                out.tint = missingTextureColor;
            }
            if (auto submitted = renderer.submit(out); !submitted)
                return submitted;
        }
    }
    return success();
}

Status SceneRenderer::drawColliders(Renderer &renderer, const Scene &scene, int layer) {
    constexpr int circleSegments = 24;
    for (const EntityId id : scene.hierarchyOrder()) {
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
    for (const EntityId id : scene.hierarchyOrder()) {
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
