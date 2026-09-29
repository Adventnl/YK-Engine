#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/runtime/GameContext.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
const std::vector<std::string> &uiAnchorNames() {
    static const std::vector<std::string> names{"TopLeft",    "Top",    "TopRight",
                                                "Left",       "Center", "Right",
                                                "BottomLeft", "Bottom", "BottomRight"};
    return names;
}

void PlayerInput::describe(TypeBuilder<PlayerInput> &type) {
    type.category("Input").description(
        "Lets a person control this entity through one action set of the project's input map.");
    type.field("actionSet", &PlayerInput::actionSet)
        .inputSet()
        .tooltip("Set of the project's input map, for example Player1.");
}
ButtonState PlayerInput::button(const GameContext &context, std::string_view action) const {
    return enabled ? context.input().state(actionSet, action) : ButtonState{};
}
float PlayerInput::value(const GameContext &context, std::string_view action) const {
    return enabled ? context.input().value(actionSet, action) : 0.0F;
}

void SpriteRenderer::describe(TypeBuilder<SpriteRenderer> &type) {
    type.category("Rendering").description("Draws a texture or a colored placeholder shape.");
    type.field("texture", &SpriteRenderer::texture)
        .asset("texture")
        .tooltip("Leave empty to draw the placeholder shape.");
    type.field("shape", &SpriteRenderer::shape).options({"Rectangle", "Ellipse"});
    type.field("size", &SpriteRenderer::size)
        .range(0.01, 1000)
        .size()
        .tooltip("Width and height in world units.");
    type.field("offset", &SpriteRenderer::offset).offset();
    type.field("color", &SpriteRenderer::color);
    type.field("layer", &SpriteRenderer::layer)
        .range(-1000, 1000)
        .tooltip("Higher layers draw on top.");
    type.field("order", &SpriteRenderer::order)
        .range(-1000, 1000, 0.1)
        .tooltip("Draw order within a layer.");
    type.field("flipX", &SpriteRenderer::flipX);
    type.field("visible", &SpriteRenderer::visible);
    type.field("columns", &SpriteRenderer::columns).range(1, 256).tooltip("Sprite sheet columns.");
    type.field("rows", &SpriteRenderer::rows).range(1, 256).tooltip("Sprite sheet rows.");
    type.field("frame", &SpriteRenderer::frame)
        .range(0, 65535)
        .tooltip("Sprite sheet cell to draw.");
    type.field("drawMode", &SpriteRenderer::drawMode)
        .options({"Simple", "Tiled", "Sliced"})
        .tooltip("Simple stretches the texture; Tiled repeats it; Sliced keeps the corners of a "
                 "nine-slice texture (set its border in the texture's import settings).");
    type.field("tileSize", &SpriteRenderer::tileSize)
        .range(0, 1000)
        .tooltip("Tiled: world size of one repeat. 0 uses the texture's own size.");
    type.field("sliceFill", &SpriteRenderer::sliceFill)
        .options({"Stretch", "Tile"})
        .tooltip("Sliced: how the edges and the middle are filled.");
    type.field("blend", &SpriteRenderer::blend)
        .options({"Alpha", "Additive"})
        .tooltip("Additive adds light to what is behind (glows, sparks).");
    type.field("parallax", &SpriteRenderer::parallax)
        .range(-10, 10, 0.01)
        .tooltip("1 moves with the world, 0 stays fixed on screen, in between scrolls slower than "
                 "the camera. Applied in the game view.");
}
void UiText::describe(TypeBuilder<UiText> &type) {
    type.category("UI").screenSpace().description(
        "Screen-space text. Use {variable} to show game variables.");
    type.field("text", &UiText::text).multiline();
    type.field("anchor", &UiText::anchor).options(uiAnchorNames());
    type.field("offset", &UiText::offset)
        .range(-4000, 4000)
        .tooltip("Pixels from the anchor toward the screen center.");
    type.field("scale", &UiText::scale).range(1, 16, 0.5).tooltip("Font pixel multiplier.");
    type.field("color", &UiText::color);
    type.field("shadow", &UiText::shadow);
    type.field("layer", &UiText::layer).range(-1000, 1000);
}
void UiPanel::describe(TypeBuilder<UiPanel> &type) {
    type.category("UI").screenSpace().description("Screen-space filled rectangle.");
    type.field("anchor", &UiPanel::anchor).options(uiAnchorNames());
    type.field("size", &UiPanel::size).range(0, 8000).size().tooltip("Pixels.");
    type.field("offset", &UiPanel::offset).range(-4000, 4000);
    type.field("color", &UiPanel::color);
    type.field("layer", &UiPanel::layer).range(-1000, 1000);
}

void RigidBody::describe(TypeBuilder<RigidBody> &type) {
    type.category("Physics").description("Gives the entity a physics body.");
    type.field("type", &RigidBody::type)
        .options({"Static", "Kinematic", "Dynamic"})
        .tooltip("Static never moves; Kinematic moves by script; Dynamic is fully simulated.");
    type.field("gravityScale", &RigidBody::gravityScale).range(-20, 20, 0.05);
    type.field("linearDamping", &RigidBody::linearDamping).range(0, 100, 0.01);
    type.field("angularDamping", &RigidBody::angularDamping).range(0, 100, 0.01);
    type.field("fixedRotation", &RigidBody::fixedRotation);
    type.field("bullet", &RigidBody::bullet).tooltip("Continuous collision for very fast bodies.");
    type.field("allowSleep", &RigidBody::allowSleep);
}
std::array<Vec2, 3> wedgePoints(Vec2 halfExtents, Vec2 scaleSign) {
    const float sx = scaleSign.x < 0.0F ? -1.0F : 1.0F;
    const float sy = scaleSign.y < 0.0F ? -1.0F : 1.0F;
    // Right angle at the bottom left; the hypotenuse rises to the right (y is down).
    return {Vec2{-halfExtents.x * sx, halfExtents.y * sy}, Vec2{halfExtents.x * sx, halfExtents.y * sy},
            Vec2{halfExtents.x * sx, -halfExtents.y * sy}};
}

void Collider::describe(TypeBuilder<Collider> &type) {
    type.category("Physics").description(
        "Collision or trigger geometry attached to the nearest RigidBody.");
    type.field("shape", &Collider::shape)
        .options({"Box", "Circle", "Capsule", "Wedge"})
        .tooltip("Wedge is a right triangle (a ramp rising to the right); mirror the entity for "
                 "the other direction.");
    type.field("size", &Collider::size)
        .range(0.01, 1000)
        .size()
        .tooltip("Full extent in world units.");
    type.field("offset", &Collider::offset).offset();
    type.field("oneWay", &Collider::oneWay)
        .tooltip("A jump-through platform: blocks only what lands on its top side.");
    type.field("isTrigger", &Collider::isTrigger)
        .tooltip("Detects overlaps without blocking movement.");
    type.field("detectTriggers", &Collider::detectTriggers)
        .tooltip("Let a trigger also see other triggers.");
    type.field("layer", &Collider::layer).layer().tooltip("Project collision layer.");
    type.field("friction", &Collider::friction).range(0, 10, 0.05);
    type.field("restitution", &Collider::restitution).range(0, 1, 0.05);
    type.field("density", &Collider::density).range(0, 100, 0.1);
}

void Camera::describe(TypeBuilder<Camera> &type) {
    type.category("Rendering")
        .description("Defines what the game shows. The first primary camera is used.");
    type.field("primary", &Camera::primary);
    type.field("mode", &Camera::mode)
        .options({"Fixed", "Follow", "FitTargets"})
        .tooltip("Fixed stays put; Follow tracks the targets' midpoint; FitTargets also zooms to "
                 "keep them in view.");
    type.field("orthographicHeight", &Camera::orthographicHeight)
        .range(1, 500, 0.5)
        .tooltip("World units visible vertically.");
    type.field("targets", &Camera::targets);
    type.field("padding", &Camera::padding).range(0, 100, 0.25);
    type.field("smoothTime", &Camera::smoothTime).range(0, 5, 0.01);
    type.field("minHeight", &Camera::minHeight).range(1, 500, 0.5);
    type.field("maxHeight", &Camera::maxHeight).range(1, 500, 0.5);
    type.field("clampToBounds", &Camera::clampToBounds)
        .tooltip("Keep the view inside the bounds rectangle.");
    type.field("boundsMin", &Camera::boundsMin).range(-10000, 10000);
    type.field("boundsMax", &Camera::boundsMax).range(-10000, 10000);
}
CameraView Camera::view() const {
    if (initialized_)
        return {position_, height_};
    return {entity().worldPosition(), orthographicHeight};
}
void Camera::onLateUpdate(GameContext &context, float seconds) {
    const Vec2 viewport = context.viewportSize();
    const float aspect = viewport.y > 0 ? viewport.x / viewport.y : 16.0F / 9.0F;
    Vec2 goal = entity().worldPosition();
    float height = orthographicHeight;
    if (mode != CameraMode::Fixed) {
        bool any = false;
        Vec2 low{}, high{};
        for (const EntityRef reference : targets) {
            const Entity *target = context.scene().find(reference);
            if (!target || !target->activeInHierarchy())
                continue;
            const Vec2 point = target->worldPosition();
            low = any ? Vec2{std::min(low.x, point.x), std::min(low.y, point.y)} : point;
            high = any ? Vec2{std::max(high.x, point.x), std::max(high.y, point.y)} : point;
            any = true;
        }
        if (any) {
            goal = (low + high) * 0.5F;
            if (mode == CameraMode::FitTargets) {
                const float neededHeight = (high.y - low.y) + 2 * padding;
                const float neededWidth = (high.x - low.x) + 2 * padding;
                height = std::clamp(std::max(neededHeight, neededWidth / aspect),
                                    std::min(minHeight, maxHeight), std::max(minHeight, maxHeight));
            }
        }
    }
    if (!initialized_) {
        position_ = goal;
        height_ = height;
        initialized_ = true;
    } else {
        const float blend = smoothTime > 0.0F ? 1.0F - std::exp(-seconds / smoothTime) : 1.0F;
        position_ = lerp(position_, goal, blend);
        height_ = lerp(height_, height, blend);
    }
    if (clampToBounds) {
        const Vec2 half{height_ * aspect * 0.5F, height_ * 0.5F};
        const auto clampAxis = [](float value, float low, float high, float halfExtent) {
            return high - low <= 2 * halfExtent
                       ? (low + high) * 0.5F
                       : std::clamp(value, low + halfExtent, high - halfExtent);
        };
        position_.x = clampAxis(position_.x, boundsMin.x, boundsMax.x, half.x);
        position_.y = clampAxis(position_.y, boundsMin.y, boundsMax.y, half.y);
    }
}

void AudioSource::describe(TypeBuilder<AudioSource> &type) {
    type.category("Audio").description("Plays a sound on start or on request.");
    type.field("sound", &AudioSource::sound).asset("sound");
    type.field("volume", &AudioSource::volume).range(0, 2, 0.05);
    type.field("loop", &AudioSource::loop);
    type.field("playOnStart", &AudioSource::playOnStart);
}
void AudioSource::play(GameContext &context) const {
    if (!sound.path.empty())
        context.audio().play(sound.path, volume, loop);
}
void AudioSource::onStart(GameContext &context) {
    if (playOnStart)
        play(context);
}

void AnimatedSprite::describe(TypeBuilder<AnimatedSprite> &type) {
    type.category("Rendering")
        .dependsOn("SpriteRenderer")
        .description("Animates the sprite from an animation asset, optionally through an "
                     "animation controller (state machine).");
    type.field("animation", &AnimatedSprite::animation)
        .asset("animation")
        .tooltip("The .ykanim asset: sheet texture, grid and clips.");
    type.field("controller", &AnimatedSprite::controller)
        .asset("animator")
        .tooltip("Optional .ykctl state machine. Without one, `clip` plays.");
    type.field("clip", &AnimatedSprite::clip).tooltip("Clip to play when there is no controller.");
    type.field("speed", &AnimatedSprite::speed).range(0, 10, 0.05);
    type.field("playOnStart", &AnimatedSprite::playOnStart);
    type.field("flipParameter", &AnimatedSprite::flipParameter)
        .tooltip("Mirror the sprite when this controller parameter is negative (facing).");
    type.field("artFacesLeft", &AnimatedSprite::artFacesLeft)
        .tooltip("The art is drawn facing left, so the mirroring is inverted.");
}
void AnimatedSprite::play(const std::string &name) {
    if (!player_.ready() || !player_.set()->find(name))
        return;
    player_.playClip(name);
    playing_ = true;
}
void AnimatedSprite::onStart(GameContext &context) {
    if (animation.path.empty())
        return;
    const auto set = context.animationSet(animation.path);
    if (!set)
        return;
    std::shared_ptr<const AnimationController> machine;
    if (!controller.path.empty())
        machine = context.animationController(controller.path);
    if (auto status = player_.start(set, machine); !status) {
        log(LogLevel::Warning, "animation", "'" + entity().name() + "': " + status.error());
        return;
    }
    if (!clip.empty() && !machine)
        player_.playClip(clip);
    playing_ = playOnStart || machine != nullptr;
    if (auto *sprite = entity().get<SpriteRenderer>()) {
        if (!set->texture.empty())
            sprite->texture.path = set->texture;
        sprite->columns = set->columns;
        sprite->rows = set->rows;
        sprite->frame = player_.frame();
    }
}
void AnimatedSprite::onUpdate(GameContext &context, float seconds) {
    if (!player_.ready())
        return;
    if (playing_)
        player_.update(seconds * speed);
    if (auto *sprite = entity().get<SpriteRenderer>()) {
        sprite->frame = player_.frame();
        if (!flipParameter.empty()) {
            const double direction = player_.value(flipParameter);
            if (direction != 0.0)
                sprite->flipX = (direction < 0.0) != artFacesLeft;
        }
    }
    for (const ClipEvent &event : player_.takeEvents()) {
        context.emit(event.name, entity().id());
        if (!event.sound.empty())
            context.audio().play(event.sound);
    }
}

void registerEngineComponents(ComponentRegistry &registry) {
    registry.add<SpriteRenderer>("SpriteRenderer");
    registry.add<UiText>("UiText");
    registry.add<UiPanel>("UiPanel");
    registry.add<PlayerInput>("PlayerInput");
    registry.add<RigidBody>("RigidBody");
    registry.add<Collider>("Collider").allowMultiple();
    registry.add<Camera>("Camera");
    registry.add<AudioSource>("AudioSource");
    registry.add<AnimatedSprite>("AnimatedSprite");
    const auto place = [](Scene &scene, Vec2 at, const char *name) -> Entity & {
        Entity &entity = scene.createEntity(name);
        entity.setWorldPosition(at);
        return entity;
    };
    registry.addTemplate({"Empty", "Basic", [place](Scene &scene, Vec2 at) {
                              return place(scene, at, "Entity").id();
                          }});
    registry.addTemplate({"Sprite", "Basic", [place](Scene &scene, Vec2 at) {
                              Entity &entity = place(scene, at, "Sprite");
                              entity.add<SpriteRenderer>();
                              return entity.id();
                          }});
    registry.addTemplate({"Camera", "Basic", [place](Scene &scene, Vec2 at) {
                              Entity &entity = place(scene, at, "Camera");
                              entity.add<Camera>();
                              return entity.id();
                          }});
    registry.addTemplate({"UI Text", "UI", [place](Scene &scene, Vec2 at) {
                              Entity &entity = place(scene, at, "UI Text");
                              entity.add<UiText>();
                              return entity.id();
                          }});
    registry.addTemplate({"UI Panel", "UI", [place](Scene &scene, Vec2 at) {
                              Entity &entity = place(scene, at, "UI Panel");
                              entity.add<UiPanel>();
                              return entity.id();
                          }});
}
} // namespace yk
