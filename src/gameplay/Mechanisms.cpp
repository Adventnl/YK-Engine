#include "yk/core/Log.hpp"
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
const std::vector<std::string> &plateSensingNames() {
    static const std::vector<std::string> names{"Auto", "Weight", "Region"};
    return names;
}

void PressurePlate::describe(TypeBuilder<PressurePlate> &type) {
    type.category("Gameplay")
        .description("Pressed while something stands on it (or in its zone); drives its targets. "
                     "With a kinematic pad it is a real surface that sinks under its load.")
        .onAdd([](Entity &entity, PressurePlate &) {
            // A new plate is a real object: a low, wide, solid pad on a kinematic body that sinks.
            // An entity that is already a zone (it has a trigger collider) stays one, and a plate
            // whose pad is a child entity needs no body of its own: remove them. Loading saved
            // data never runs this hook, so older plates keep working exactly as saved.
            const auto colliders = entity.getAll<Collider>();
            if (std::any_of(colliders.begin(), colliders.end(),
                            [](const Collider *collider) { return collider->isTrigger; }))
                return;
            if (colliders.empty()) {
                Collider &collider = entity.add<Collider>();
                collider.size = {1.4F, 0.5F};
                collider.offset = {0.0F, 0.11F}; // Its top is 0.14 above the origin.
                // A 30 degree slope up onto it that starts below the floor line (a lip above the
                // floor would stop a crate dead).
                collider.chamfer = {0.35F, 0.2F};
                collider.layer = layers::solid;
                collider.friction = 0.8F;
            }
            if (!entity.has<RigidBody>())
                entity.add<RigidBody>().type = RigidBodyType::Kinematic;
        });
    type.field("targets", &PressurePlate::targets)
        .tooltip("Doors, platforms... that react to this plate.");
    type.field("activatorTags", &PressurePlate::activatorTags)
        .tooltip("Only entities with one of these tags press it. Empty: any movable body.");
    type.field("latch", &PressurePlate::latch).tooltip("Stay pressed once triggered.");
    type.field("sensing", &PressurePlate::sensing)
        .options(plateSensingNames())
        .tooltip("Weight: what rests on the pad presses it. Region: what overlaps a trigger "
                 "collider of the plate presses it. Auto: Weight for a pad with a solid collider "
                 "and no trigger, Region otherwise.");
    type.field("pad", &PressurePlate::pad)
        .tooltip("The part that sinks and carries: an entity (this one, or a child) with a "
                 "kinematic RigidBody and a solid Collider. Empty: this entity.");
    type.field("pressedColor", &PressurePlate::pressedColor);
    type.field("pressDepth", &PressurePlate::pressDepth)
        .range(0, 5, 0.01)
        .tooltip("How far the plate sinks when pressed, in world units.");
    type.field("pressSpeed", &PressurePlate::pressSpeed)
        .range(0.01, 20, 0.05)
        .tooltip("Fastest the plate sinks or rises, m/s.");
    type.field("acceleration", &PressurePlate::acceleration)
        .range(0.1, 100, 0.5)
        .tooltip("How quickly it speeds up and slows down, m/s^2. Keep it below gravity so "
                 "whatever stands on the plate never loses contact.");
    type.field("minimumMass", &PressurePlate::minimumMass)
        .range(0, 1000, 0.1)
        .tooltip("Weight sensing: kilograms that must rest on the pad (0: anything).");
    type.field("pressSound", &PressurePlate::pressSound).asset("sound");
    type.field("releaseSound", &PressurePlate::releaseSound).asset("sound");
    type.field("pressed", &PressurePlate::pressed_).readOnly();
}

namespace {
// The pad's position in its parent's frame, and the direction "down" for it in that frame.
Transform2D parentFrame(const Entity &entity) {
    const Entity *parent = entity.parent();
    return parent ? parent->worldTransform() : Transform2D{};
}
Vec2 padDown(const Entity &pad) {
    return rotated({0.0F, 1.0F}, degreesToRadians(pad.transform().rotationDegrees));
}
} // namespace

void PressurePlate::onStart(GameContext &context) {
    Entity *padEntity = pad ? context.scene().find(pad) : nullptr;
    if (!padEntity)
        padEntity = &entity();
    const auto *rigid = padEntity->get<RigidBody>();
    bodyDriven_ = rigid && rigid->type == RigidBodyType::Kinematic;
    bool solidPad = false, zone = false;
    for (const Collider *collider : padEntity->getAll<Collider>())
        (collider->isTrigger ? zone : solidPad) = true;
    if (padEntity != &entity())
        for (const Collider *collider : entity().getAll<Collider>())
            zone = zone || collider->isTrigger;
    const bool weight = sensing == PlateSensing::Weight ||
                        (sensing == PlateSensing::Auto && bodyDriven_ && solidPad && !zone);
    mode_ = weight ? Mode::Weight : Mode::Region;
    if (mode_ == Mode::Weight && !bodyDriven_)
        log(LogLevel::Warning, "gameplay",
            "'" + entity().name() +
                "': weight sensing needs a pad with a kinematic RigidBody and a solid Collider");
    restLocal_ = padEntity->transform().position;
    if (const auto *sprite = entity().get<SpriteRenderer>()) {
        baseOffsetY_ = sprite->offset.y;
        baseColor_ = sprite->color;
    }
    pressed_ = false;
    loaded_ = false;
    unloadedFor_ = 1e9F;
    depth_ = 0.0F;
    speed_ = 0.0F;
    applyVisuals(context);
    sendSignal(context.scene(), entity().id(), targets, false);
}

bool PressurePlate::senseWeight(GameContext &context, Entity &padEntity) const {
    const auto body = context.bodyOf(padEntity.id());
    if (!body)
        return false;
    auto &world = context.physics();
    const auto contacts = world.contacts(*body);
    if (!contacts)
        return false;
    // The face that presses: the pad's "up".
    const Vec2 up =
        rotated({0.0F, -1.0F}, degreesToRadians(padEntity.worldTransform().rotationDegrees));
    float mass = 0.0F;
    bool any = false;
    std::vector<physics::BodyHandle> counted; // A body touching with two shapes weighs once.
    for (const physics::Contact &contact : contacts.value()) {
        const auto firstBody = world.bodyOf(contact.first);
        const bool padIsFirst = firstBody && firstBody.value() == *body;
        const physics::ShapeHandle otherShape = padIsFirst ? contact.second : contact.first;
        const Vec2 toOther = padIsFirst ? contact.normal : -contact.normal;
        if (dot(toOther, up) < 0.6F) // Beside or below the pad: leaning on it is not a load.
            continue;
        float closest = 1e9F;
        for (const physics::ContactPoint &point : contact.points)
            closest = std::min(closest, point.separation);
        if (closest > 0.05F) // A speculative contact that is not touching yet.
            continue;
        const auto otherBodyResult = world.bodyOf(otherShape);
        if (!otherBodyResult)
            continue;
        const physics::BodyHandle otherBody = otherBodyResult.value();
        const Entity *other = context.entityOfShape(otherShape);
        const Entity *owner = context.entityOfBody(otherBody);
        if (!other || !owner || other->id() == padEntity.id())
            continue;
        const auto *rigid = owner->get<RigidBody>();
        if (!rigid || rigid->type == RigidBodyType::Static) // Level geometry cannot press.
            continue;
        if (!matchesActivator(*other, activatorTags) && !matchesActivator(*owner, activatorTags))
            continue;
        if (const auto *killable = owner->get<Killable>(); killable && !killable->alive())
            continue;
        if (std::find(counted.begin(), counted.end(), otherBody) != counted.end())
            continue;
        counted.push_back(otherBody);
        if (const auto state = world.state(otherBody))
            mass += state.value().mass;
        any = true;
    }
    return any && mass >= minimumMass;
}

bool PressurePlate::senseRegion(GameContext &context) const {
    if (anyActivator(context, entity().id(), activatorTags))
        return true;
    const Entity *padEntity = pad ? context.scene().find(pad) : nullptr;
    return padEntity && padEntity != &entity() &&
           anyActivator(context, padEntity->id(), activatorTags);
}

void PressurePlate::applyVisuals(GameContext &) {
    if (auto *animated = entity().get<AnimatedSprite>()) {
        animated->setBool("pressed", pressed_); // Art shows it through its controller.
        animated->setFloat("pressAmount", pressAmount());
        return;
    }
    if (auto *sprite = entity().get<SpriteRenderer>()) {
        sprite->color = pressed_ ? pressedColor : baseColor_;
        if (!bodyDriven_) // A pad carries its own sprite down; a bare plate sinks its sprite.
            sprite->offset.y = baseOffsetY_ + depth_;
    }
}

void PressurePlate::onFixedUpdate(GameContext &context, float seconds) {
    Entity *padEntity = pad ? context.scene().find(pad) : nullptr;
    if (!padEntity)
        padEntity = &entity();
    const bool occupied =
        mode_ == Mode::Weight ? senseWeight(context, *padEntity) : senseRegion(context);
    // A resting contact can drop out for a tick or two (a character stepping, a seam); a short
    // grace keeps the plate from stuttering. A bare zone reports at once, as it always has.
    const float grace = bodyDriven_ ? 0.06F : 0.0F;
    if (occupied) {
        loaded_ = true;
        unloadedFor_ = 0.0F;
    } else {
        unloadedFor_ += seconds;
        if (unloadedFor_ > grace)
            loaded_ = false;
    }

    // The plate sinks under a load and rises without one: a trapezoid speed profile whose
    // acceleration stays below gravity, ending exactly at the stops (no overshoot, no creep).
    const float bottom = std::max(pressDepth, 0.0F);
    const float target = (loaded_ || (latch && pressed_)) ? bottom : 0.0F;
    const float remaining = target - depth_;
    float wanted = 0.0F;
    if (std::fabs(remaining) > 1e-5F)
        wanted = std::copysign(
            std::min(pressSpeed, std::sqrt(2.0F * acceleration * std::fabs(remaining))), remaining);
    const float maxChange = acceleration * seconds;
    speed_ += std::clamp(wanted - speed_, -maxChange, maxChange);
    float next = depth_ + speed_ * seconds;
    if (next >= bottom) {
        next = bottom;
        speed_ = 0.0F;
    } else if (next <= 0.0F) {
        next = 0.0F;
        speed_ = 0.0F;
    }
    if (std::fabs(target - next) < 1e-4F && std::fabs(speed_) < 0.05F) {
        next = target;
        speed_ = 0.0F;
    }
    depth_ = next;
    if (bodyDriven_) {
        const Transform2D parent = parentFrame(*padEntity);
        moveKinematic(context, *padEntity,
                      transformPoint(parent, restLocal_ + padDown(*padEntity) * depth_));
    }

    bool now = pressed_;
    if (bodyDriven_ && bottom >= 0.001F) {
        // A plate with a body is pressed once it is really down, and released once it has come up
        // a little (the two thresholds differ so it cannot flutter at the switch point).
        const float amount = pressAmount();
        if (!pressed_ && amount >= 0.85F)
            now = true;
        else if (pressed_ && amount < 0.7F && !latch)
            now = false;
    } else {
        now = latch ? (pressed_ || loaded_) : loaded_;
    }
    const bool changed = now != pressed_;
    pressed_ = now;
    applyVisuals(context);
    if (changed) {
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
    type.field("openRotation", &Door::openRotation)
        .range(-360, 360, 1)
        .tooltip("Degrees the door turns about its origin (its hinge) when open; positive turns "
                 "clockwise on screen.");
    type.field("speed", &Door::speed).range(0.01, 100, 0.1).tooltip("Slide speed, m/s.");
    type.field("rotationSpeed", &Door::rotationSpeed)
        .range(1, 1440, 1)
        .tooltip("Turn speed, degrees per second.");
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
    closedRotation_ = entity().worldTransform().rotationDegrees;
    amount_ = startsOpen ? 1.0F : 0.0F;
    opening_ = startsOpen;
    started_ = true;
    if (startsOpen) {
        Transform2D open = entity().worldTransform();
        open.position = closedPosition_ + openOffset;
        open.rotationDegrees = closedRotation_ + openRotation;
        entity().setWorldTransform(open);
        context.teleport(entity(), open.position); // Moves the body, pose and angle, too.
    }
}
void Door::onFixedUpdate(GameContext &context, float seconds) {
    if (!started_)
        return;
    const bool wantOpen = signalActive() != startsOpen;
    if (wantOpen != opening_) {
        opening_ = wantOpen;
        play(context, wantOpen ? openSound : closeSound);
    }
    const float slide = yk::length(openOffset);
    const float turn = std::fabs(openRotation);
    if (slide < 1e-4F && turn < 1e-3F)
        return;
    // Sliding and turning finish together: the door takes as long as the slower of the two.
    const float duration =
        std::max(slide > 1e-4F ? slide / speed : 0.0F, turn > 1e-3F ? turn / rotationSpeed : 0.0F);
    const float before = amount_;
    const float step = seconds / duration;
    amount_ = wantOpen ? std::min(1.0F, amount_ + step) : std::max(0.0F, amount_ - step);
    if (turn > 1e-3F)
        moveKinematic(context, entity(), closedPosition_ + openOffset * amount_,
                      closedRotation_ + openRotation * amount_);
    else
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
    type.field("acceleration", &MovingPlatform::acceleration)
        .range(0.1, 1000, 0.5)
        .tooltip("How quickly it speeds up and slows down, m/s^2. Below gravity (9.81), things "
                 "standing on a platform that starts downward stay on it.");
    type.field("spinSpeed", &MovingPlatform::spinSpeed)
        .range(-720, 720, 1)
        .tooltip("Degrees per second the platform turns about its origin while it is moving "
                 "(positive clockwise on screen). With no travel it is a rotating platform.");
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
    startRotation_ = entity().worldTransform().rotationDegrees;
    spin_ = 0.0F;
    t_ = 0.0F;
    speed_ = 0.0F;
    direction_ = 1.0F;
    waiting_ = 0.0F;
}
void MovingPlatform::onFixedUpdate(GameContext &context, float seconds) {
    const float length = yk::length(travel);
    const bool active = !requireSignal || signalActive();
    const bool spins = std::fabs(spinSpeed) > 1e-4F;
    if (active && spins)
        spin_ = std::fmod(spin_ + spinSpeed * seconds, 360.0F);
    const auto place = [&] {
        const Vec2 at = start_ + travel * t_;
        if (spins)
            moveKinematic(context, entity(), at, startRotation_ + spin_);
        else
            moveKinematic(context, entity(), at);
    };
    if (length < 1e-4F) {
        place(); // Nothing to travel (a platform that only turns).
        return;
    }
    if (waiting_ > 0.0F) {
        waiting_ -= seconds;
        speed_ = 0.0F;
        place();
        return;
    }
    // Ease in and out: speed up at `acceleration`, cruise at `speed`, brake to arrive at the end
    // exactly; when the signal goes away, slow to a stop where it is instead of stopping dead
    // (which would leave whatever rides it behind).
    const float remaining = (direction_ > 0.0F ? 1.0F - t_ : t_) * length;
    float wanted = 0.0F;
    if (active)
        wanted = std::max(std::min(speed, std::sqrt(2.0F * acceleration * remaining)), 0.05F);
    const float change = acceleration * seconds;
    speed_ =
        speed_ < wanted ? std::min(speed_ + change, wanted) : std::max(speed_ - change, wanted);
    const float step = speed_ * seconds;
    if (step >= remaining - 1e-5F && (active || speed_ > 0.0F)) {
        t_ = direction_ > 0.0F ? 1.0F : 0.0F; // Arrived.
        speed_ = 0.0F;
        direction_ = -direction_;
        waiting_ = pause;
    } else {
        t_ += direction_ * step / length;
    }
    place();
}
} // namespace yk
