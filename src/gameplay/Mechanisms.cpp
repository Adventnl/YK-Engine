#include "yk/gameplay/Gameplay.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
namespace {
// Movable, living entities from `overlaps` that satisfy `tags`.
bool anyActivator(GameContext &context, EntityId trigger, const std::vector<std::string> &tags) {
    for (const EntityId id : context.overlapping(trigger)) {
        const Entity *other = context.scene().find(id);
        if (!other || !matchesActivator(*other, tags))
            continue;
        if (const auto *killable = other->get<Killable>(); killable && !killable->alive())
            continue;
        return true;
    }
    return false;
}
void turnIntoTrigger(Entity &entity) {
    if (auto *collider = entity.get<Collider>())
        collider->isTrigger = true;
}
void play(GameContext &context, const AssetRef &sound) {
    if (!sound.path.empty())
        context.audio().play(sound.path);
}
} // namespace

// ----- PressurePlate -----
void PressurePlate::describe(TypeBuilder<PressurePlate> &type) {
    type.category("Gameplay")
        .description("Pressed while a character or object stands in its trigger collider; drives "
                     "its targets.")
        .dependsOn("Collider")
        .onAdd([](Entity &entity, PressurePlate &) { turnIntoTrigger(entity); });
    type.field("targets", &PressurePlate::targets)
        .tooltip("Doors, platforms... that react to this plate.");
    type.field("activatorTags", &PressurePlate::activatorTags)
        .tooltip("Only entities with one of these tags press it. Empty: any movable body.");
    type.field("latch", &PressurePlate::latch).tooltip("Stay pressed once triggered.");
    type.field("pressedColor", &PressurePlate::pressedColor);
    type.field("pressDepth", &PressurePlate::pressDepth).range(0, 1, 0.01);
    type.field("pressSound", &PressurePlate::pressSound).asset("sound");
    type.field("releaseSound", &PressurePlate::releaseSound).asset("sound");
    type.field("pressed", &PressurePlate::pressed_).readOnly();
}
void PressurePlate::onStart(GameContext &context) {
    if (const auto *sprite = entity().get<SpriteRenderer>()) {
        baseOffsetY_ = sprite->offset.y;
        baseColor_ = sprite->color;
    }
    pressed_ = false;
    applyVisuals();
    sendSignal(context.scene(), entity().id(), targets, false);
}
void PressurePlate::applyVisuals() {
    if (auto *animated = entity().get<AnimatedSprite>()) {
        animated->setBool("pressed", pressed_); // Art shows it through its controller.
        return;
    }
    if (auto *sprite = entity().get<SpriteRenderer>()) {
        sprite->color = pressed_ ? pressedColor : baseColor_;
        sprite->offset.y = baseOffsetY_ + (pressed_ ? pressDepth : 0.0F);
    }
}
void PressurePlate::onFixedUpdate(GameContext &context, float) {
    const bool occupied = anyActivator(context, entity().id(), activatorTags);
    const bool now = latch ? (pressed_ || occupied) : occupied;
    if (now != pressed_) {
        pressed_ = now;
        applyVisuals();
        play(context, pressed_ ? pressSound : releaseSound);
        context.emit(pressed_ ? "plate_pressed" : "plate_released", entity().id());
    }
    sendSignal(context.scene(), entity().id(), targets, pressed_);
}

// ----- Lever -----
void Lever::describe(TypeBuilder<Lever> &type) {
    type.category("Gameplay")
        .description("Flips between on and off each time a character touches its trigger; drives "
                     "its targets.")
        .dependsOn("Collider")
        .onAdd([](Entity &entity, Lever &) { turnIntoTrigger(entity); });
    type.field("targets", &Lever::targets);
    type.field("activatorTags", &Lever::activatorTags);
    type.field("interactAction", &Lever::interactAction)
        .inputAction()
        .tooltip("Flip when a character in it presses this action (for example Interact). Empty: "
                 "flip on touch.");
    type.field("startsOn", &Lever::startsOn);
    type.field("cooldown", &Lever::cooldown)
        .range(0, 10, 0.05)
        .tooltip("Seconds before it can flip again.");
    type.field("onColor", &Lever::onColor);
    type.field("sound", &Lever::sound).asset("sound");
    type.field("on", &Lever::on_).readOnly();
}
void Lever::onStart(GameContext &context) {
    if (const auto *sprite = entity().get<SpriteRenderer>())
        baseColor_ = sprite->color;
    on_ = startsOn;
    cooldown_ = 0.0F;
    applyVisuals();
    sendSignal(context.scene(), entity().id(), targets, on_);
}
void Lever::applyVisuals() {
    if (auto *animated = entity().get<AnimatedSprite>())
        animated->setBool("on", on_); // Art shows it through its controller.
    else if (auto *sprite = entity().get<SpriteRenderer>())
        sprite->color = on_ ? onColor : baseColor_;
}
void Lever::onFixedUpdate(GameContext &context, float seconds) {
    cooldown_ = std::max(0.0F, cooldown_ - seconds);
    if (!interactAction.empty() && cooldown_ <= 0.0F) {
        for (const EntityId id : context.overlapping(entity().id())) {
            Entity *other = context.scene().find(id);
            if (!other || !matchesActivator(*other, activatorTags))
                continue;
            const auto *killable = other->get<Killable>();
            const auto *player = other->get<PlayerInput>();
            if ((killable && !killable->alive()) || !player ||
                !player->button(context, interactAction).pressed)
                continue;
            if (auto *animated = other->get<AnimatedSprite>())
                animated->trigger("interact");
            flip(context, *other);
            break; // One flip per press, even if two characters press together.
        }
    }
    sendSignal(context.scene(), entity().id(), targets, on_);
}
void Lever::flip(GameContext &context, Entity &by) {
    on_ = !on_;
    cooldown_ = cooldown;
    applyVisuals();
    play(context, sound);
    context.emit("lever_toggled", entity().id(), by.id());
}
void Lever::onTriggerEnter(GameContext &context, Entity &other) {
    if (!interactAction.empty() || cooldown_ > 0.0F || !matchesActivator(other, activatorTags))
        return;
    if (const auto *killable = other.get<Killable>(); killable && !killable->alive())
        return;
    flip(context, other);
}

// ----- Door -----
void Door::describe(TypeBuilder<Door> &type) {
    type.category("Gameplay")
        .description(
            "Slides open while signalled by plates, levers or goals that list it as a target.")
        .dependsOn("RigidBody")
        .dependsOn("Collider")
        .onAdd([](Entity &entity, Door &) {
            if (auto *body = entity.get<RigidBody>())
                body->type = RigidBodyType::Kinematic;
        });
    type.field("openOffset", &Door::openOffset)
        .range(-1000, 1000)
        .displacement()
        .tooltip("How far the door moves when open, in world units (negative Y is up).");
    type.field("speed", &Door::speed).range(0.01, 100, 0.1);
    type.field("startsOpen", &Door::startsOpen).tooltip("Open until signalled, then closes.");
    type.field("logic", &Door::logic)
        .options(signalLogicNames())
        .tooltip("How several sources combine.");
    type.field("invert", &Door::invert);
    type.field("openSound", &Door::openSound).asset("sound");
    type.field("closeSound", &Door::closeSound).asset("sound");
    type.field("openAmount", &Door::amount_).readOnly();
}
void Door::onStart(GameContext &context) {
    closedPosition_ = entity().worldPosition();
    amount_ = startsOpen ? 1.0F : 0.0F;
    opening_ = startsOpen;
    started_ = true;
    if (startsOpen)
        context.teleport(entity(), closedPosition_ + openOffset); // Moves the body too.
}
void Door::onFixedUpdate(GameContext &context, float seconds) {
    if (!started_)
        return;
    const bool wantOpen = signalActive() != startsOpen;
    if (wantOpen != opening_) {
        opening_ = wantOpen;
        play(context, wantOpen ? openSound : closeSound);
    }
    const float length = yk::length(openOffset);
    if (length < 1e-4F)
        return;
    const float before = amount_;
    const float step = speed * seconds / length;
    amount_ = wantOpen ? std::min(1.0F, amount_ + step) : std::max(0.0F, amount_ - step);
    moveKinematic(context, entity(), closedPosition_ + openOffset * amount_);
    if (auto *animated = entity().get<AnimatedSprite>()) {
        animated->setFloat("openAmount", amount_);
        animated->setBool("open", wantOpen);
    }
    if (before != amount_ && (amount_ == 0.0F || amount_ == 1.0F))
        context.emit(amount_ == 1.0F ? "door_opened" : "door_closed", entity().id());
}

// ----- MovingPlatform -----
void MovingPlatform::describe(TypeBuilder<MovingPlatform> &type) {
    type.category("Gameplay")
        .description("Shuttles between its start and start + travel, carrying riders.")
        .dependsOn("RigidBody")
        .dependsOn("Collider")
        .onAdd([](Entity &entity, MovingPlatform &) {
            if (auto *body = entity.get<RigidBody>())
                body->type = RigidBodyType::Kinematic;
        });
    type.field("travel", &MovingPlatform::travel)
        .range(-1000, 1000)
        .displacement()
        .tooltip("Offset of the far end, world units.");
    type.field("speed", &MovingPlatform::speed).range(0.01, 100, 0.1);
    type.field("pause", &MovingPlatform::pause)
        .range(0, 60, 0.1)
        .tooltip("Seconds to wait at each end.");
    type.field("requireSignal", &MovingPlatform::requireSignal)
        .tooltip("Only move while signalled.");
    type.field("logic", &MovingPlatform::logic).options(signalLogicNames());
    type.field("invert", &MovingPlatform::invert);
}
void MovingPlatform::onStart(GameContext &) {
    start_ = entity().worldPosition();
    t_ = 0.0F;
    direction_ = 1.0F;
    waiting_ = 0.0F;
}
void MovingPlatform::onFixedUpdate(GameContext &context, float seconds) {
    const float length = yk::length(travel);
    if (length < 1e-4F || (requireSignal && !signalActive())) {
        moveKinematic(context, entity(), start_ + travel * t_); // Hold still.
        return;
    }
    if (waiting_ > 0.0F) {
        waiting_ -= seconds;
        moveKinematic(context, entity(), start_ + travel * t_);
        return;
    }
    t_ += direction_ * speed * seconds / length;
    if (t_ >= 1.0F || t_ <= 0.0F) {
        t_ = std::clamp(t_, 0.0F, 1.0F);
        direction_ = -direction_;
        waiting_ = pause;
    }
    moveKinematic(context, entity(), start_ + travel * t_);
}
} // namespace yk
