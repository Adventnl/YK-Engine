#include "yk/gameplay/Gameplay.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace yk {
namespace {
float approach(float value, float target, float amount) {
    return value < target ? std::min(value + amount, target) : std::max(value - amount, target);
}

// Where the character's feet are: the bottom of its first collider.
Vec2 feetOf(const Entity &entity) {
    const Vec2 position = entity.worldPosition();
    if (const auto *collider = entity.get<Collider>())
        return position + Vec2{0.0F, collider->offset.y + collider->size.y * 0.5F};
    return position;
}

struct Ground {
    bool found{};
    Vec2 normal{0.0F, -1.0F}; // Points from the ground toward the character.
    Vec2 velocity{};          // Of the ground under the feet (see groundVelocityAt).
    Vec2 point{};             // Where the character touches it.
    physics::BodyHandle body;
};

// The velocity of the ground at `point`, which is what a rider has to match to stay put on it: a
// platform that turns or tilts moves faster the farther from its center the rider stands. A turning
// platform also pulls the rider toward its center each moment (circular motion); without that the
// rider would drift outward, so half a tick of that pull is added.
Vec2 groundVelocityAt(physics::World &world, physics::BodyHandle body, Vec2 point, float seconds) {
    const auto velocity = world.pointVelocity(body, point);
    if (!velocity)
        return {};
    Vec2 result = velocity.value();
    if (const auto state = world.state(body);
        state && std::fabs(state.value().angularVelocity) > 1e-3F) {
        const float turn = state.value().angularVelocity;
        result += (state.value().worldCenter - point) * (turn * turn * 0.5F * seconds);
    }
    return result;
}

// The most upward-facing touching contact within the walkable slope range.
Ground probeGround(GameContext &context, physics::BodyHandle body, float cosMaxSlope,
                   float seconds) {
    Ground ground;
    auto &world = context.physics();
    const auto contacts = world.contacts(body);
    if (!contacts)
        return ground;
    float best = 0.0F;
    physics::ShapeHandle groundShape;
    for (const physics::Contact &contact : contacts.value()) {
        const auto firstBody = world.bodyOf(contact.first);
        const bool weAreFirst = firstBody && firstBody.value() == body;
        const Vec2 normal = weAreFirst ? -contact.normal : contact.normal;
        float closest = 1e9F;
        Vec2 closestPoint{};
        for (const physics::ContactPoint &point : contact.points)
            if (point.separation < closest) {
                closest = point.separation;
                closestPoint = point.point;
            }
        if (closest > 0.06F) // A speculative contact that is not actually touching.
            continue;
        const float upward = -normal.y;
        if (upward >= cosMaxSlope && upward > best) {
            best = upward;
            ground.normal = normal;
            ground.point = closestPoint;
            groundShape = weAreFirst ? contact.second : contact.first;
        }
    }
    if (best > 0.0F) {
        ground.found = true;
        if (const auto groundBody = world.bodyOf(groundShape)) {
            ground.body = groundBody.value();
            ground.velocity = groundVelocityAt(world, ground.body, ground.point, seconds);
        }
    }
    return ground;
}
} // namespace

void PlatformerController::describe(TypeBuilder<PlatformerController> &type) {
    type.category("Gameplay")
        .description("Side-view character movement: run, jump, slopes, moving platforms.")
        .dependsOn("RigidBody")
        .dependsOn("Collider")
        .dependsOn("PlayerInput")
        .onAdd([](Entity &entity, PlatformerController &) {
            if (auto *body = entity.get<RigidBody>()) {
                body->type = RigidBodyType::Dynamic;
                body->fixedRotation = true;
                body->allowSleep = false;
            }
            if (auto *collider = entity.get<Collider>()) {
                collider->shape = ColliderShape::Capsule;
                collider->size = {0.6F, 0.95F};
                collider->friction = 0.0F;
                collider->layer = layers::player;
            }
        });
    type.field("moveLeftAction", &PlatformerController::moveLeftAction).inputAction();
    type.field("moveRightAction", &PlatformerController::moveRightAction).inputAction();
    type.field("jumpAction", &PlatformerController::jumpAction).inputAction();
    type.field("moveSpeed", &PlatformerController::moveSpeed)
        .range(0, 50, 0.1)
        .tooltip("Top running speed, m/s.");
    type.field("groundAcceleration", &PlatformerController::groundAcceleration).range(0, 500, 1);
    type.field("groundDeceleration", &PlatformerController::groundDeceleration).range(0, 500, 1);
    type.field("airAcceleration", &PlatformerController::airAcceleration).range(0, 500, 1);
    type.field("airDeceleration", &PlatformerController::airDeceleration).range(0, 500, 1);
    type.field("jumpHeight", &PlatformerController::jumpHeight)
        .range(0, 20, 0.05)
        .tooltip("Apex height in meters.");
    type.field("fallGravityMultiplier", &PlatformerController::fallGravityMultiplier)
        .range(0.1, 10, 0.05);
    type.field("jumpCutMultiplier", &PlatformerController::jumpCutMultiplier)
        .range(0, 1, 0.05)
        .tooltip("Speed kept when the jump key is released early (lower = shorter hops).");
    type.field("coyoteTime", &PlatformerController::coyoteTime).range(0, 1, 0.01);
    type.field("jumpBufferTime", &PlatformerController::jumpBufferTime).range(0, 1, 0.01);
    type.field("maxFallSpeed", &PlatformerController::maxFallSpeed).range(1, 200, 0.5);
    type.field("maxSlopeDegrees", &PlatformerController::maxSlopeDegrees).range(0, 89, 1);
    type.field("gripFriction", &PlatformerController::gripFriction).range(0, 10, 0.05);
    type.field("slideFriction", &PlatformerController::slideFriction).range(0, 10, 0.05);
    type.field("groundSnap", &PlatformerController::groundSnap)
        .range(0, 2, 0.05)
        .tooltip(
            "Keeps the feet on the ground over ramp crests, slopes and small steps down: after "
            "walking off the ground (without jumping), a walkable surface at most this far "
            "below pulls the character back. 0 turns it off.");
    type.field("landingSpeed", &PlatformerController::landingSpeed)
        .range(0, 100, 0.1)
        .tooltip("Downward speed at which touching down raises the animation trigger 'landed'.");
    type.field("jumpSound", &PlatformerController::jumpSound).asset("sound");
    type.field("landSound", &PlatformerController::landSound).asset("sound");
    type.field("jumpEffect", &PlatformerController::jumpEffect)
        .asset("prefab")
        .tooltip("Effect prefab spawned at the feet when a jump starts (dust).");
    type.field("landEffect", &PlatformerController::landEffect)
        .asset("prefab")
        .tooltip("Effect prefab spawned at the feet after a landing.");
    type.field("grounded", &PlatformerController::grounded_).readOnly();
}

void PlatformerController::onStart(GameContext &context) {
    // A character falls at up to maxFallSpeed, which is many times its own thickness per tick.
    // Only a bullet body is swept against moving platforms, plates and doors as well as static
    // geometry; without it a landing character sinks into them before it is pushed back out.
    if (const auto body = context.bodyOf(entity().id()))
        context.physics().setBullet(*body, true);
}

void PlatformerController::onFixedUpdate(GameContext &context, float seconds) {
    if (const auto *killable = entity().get<Killable>(); killable && !killable->alive())
        return;
    const auto body = context.bodyOf(entity().id());
    if (!body)
        return;
    auto &world = context.physics();
    const auto state = world.state(*body);
    if (!state)
        return;
    // Input: a character with no PlayerInput (or a disabled one) simply stands still.
    const auto *player = entity().get<PlayerInput>();
    float move = 0.0F;
    ButtonState jump;
    if (player) { // Reads nothing while the game's input is locked (a fade, a level-complete).
        move = player->axis(context, moveLeftAction, moveRightAction);
        jump = player->button(context, jumpAction);
    }
    const float cosMaxSlope = std::cos(degreesToRadians(maxSlopeDegrees));

    Vec2 velocity = state.value().linearVelocity;
    Ground ground = probeGround(context, *body, cosMaxSlope, seconds);
    // Contact manifolds are computed at the start of a physics step, so right after leaving the
    // ground they are one tick stale. Moving away from the surface (along its normal, so running up
    // a slope does not count) faster than 1 m/s means the character has left it. Judge that against
    // the ground velocity the character was riding last tick: a platform that stops or reverses
    // abruptly must not read as the rider having jumped off.
    const float gravityMagnitude =
        std::fabs(context.scene().settings.gravity.y) *
        (entity().get<RigidBody>() ? entity().get<RigidBody>()->gravityScale : 1.0F);
    const float jumpSpeed = std::sqrt(2.0F * gravityMagnitude * jumpHeight);
    if (ground.found) {
        // A bump over a seam is not a jump: require a good fraction of the real jump speed.
        const Vec2 reference = wasGrounded_ ? lastGroundVelocity_ : ground.velocity;
        if (dot(velocity - reference, ground.normal) > std::clamp(0.5F * jumpSpeed, 1.0F, 2.5F))
            ground.found = false;
    }
    // Just left the ground without jumping: over a ramp crest or down a slope the surface falls
    // away faster than gravity pulls, so look for walkable ground a short way below and stay on it.
    float snapDistance = 0.0F;
    if (!ground.found && hadGround_ && !jumping_ && groundSnap > 0.0F) {
        if (const auto *collider = entity().get<Collider>()) {
            const physics::QueryFilter filter{context.layers().categoryBits(collider->layer),
                                              UINT64_MAX};
            const Vec2 feet = feetOf(entity());
            const float reach = groundSnap + 0.05F;
            const auto hit = world.rayCast(feet - Vec2{0.0F, 0.05F}, {0.0F, reach + 0.05F}, filter);
            if (hit && hit.value() && -hit.value()->normal.y >= cosMaxSlope) {
                snapDistance = std::max(0.0F, hit.value()->fraction * (reach + 0.05F) - 0.05F);
                if (snapDistance <= groundSnap) {
                    ground.found = true;
                    ground.normal = hit.value()->normal;
                    ground.point = hit.value()->point;
                    ground.velocity = {};
                    // Stepping down onto something that moves keeps its motion.
                    if (const auto groundBody = world.bodyOf(hit.value()->shape))
                        ground.velocity =
                            groundVelocityAt(world, groundBody.value(), ground.point, seconds);
                }
            }
        }
    }
    hadGround_ = ground.found;
    wasGrounded_ = ground.found;
    lastGroundVelocity_ = ground.found ? ground.velocity : Vec2{};
    grounded_ = ground.found;
    coyote_ = grounded_ ? coyoteTime : std::max(0.0F, coyote_ - seconds);
    jumpBuffer_ = jump.pressed ? jumpBufferTime : std::max(0.0F, jumpBuffer_ - seconds);
    const float target = move * moveSpeed;

    Vec2 next = velocity;
    if (grounded_) {
        // Run along the surface so slopes keep their speed, carried by whatever the ground does.
        const Vec2 tangent{-ground.normal.y, ground.normal.x};
        const float along = dot(velocity - ground.velocity, tangent);
        const float rate = move != 0.0F ? groundAcceleration : groundDeceleration;
        next = ground.velocity + tangent * approach(along, target, rate * seconds);
        if (snapDistance > 0.001F) // Close the gap to the surface within a tick or two.
            next -= ground.normal * std::min(snapDistance / seconds, 8.0F);
        jumping_ = false;
    } else {
        // Right after losing the ground without jumping (a seam, a ledge), keep ground-style
        // control for the coyote window so brief contact loss does not turn into a slide.
        const bool recentlyGrounded = coyote_ > 0.0F && !jumping_;
        const float rate = recentlyGrounded
                               ? (move != 0.0F ? groundAcceleration : groundDeceleration)
                               : (move != 0.0F ? airAcceleration : airDeceleration);
        next.x = approach(velocity.x, target, rate * seconds);
    }

    const auto *rigid = entity().get<RigidBody>();
    const float baseGravity = rigid ? rigid->gravityScale : 1.0F;
    bool jumpedNow = false;
    if (jumpBuffer_ > 0.0F && coyote_ > 0.0F) {
        next.y = (grounded_ ? ground.velocity.y : 0.0F) - jumpSpeed;
        jumpBuffer_ = coyote_ = 0.0F;
        grounded_ = false;
        jumping_ = true;
        jumpedNow = true;
        spawnEffect(context, jumpEffect, feetOf(entity()));
        if (!jumpSound.path.empty())
            context.audio().play(jumpSound.path);
    } else if (jump.released && jumping_ && next.y < 0.0F) {
        next.y *= jumpCutMultiplier;
    }
    next.y = std::min(next.y, maxFallSpeed);
    world.setVelocity(*body, next);

    const float gravityScale = next.y > 0.1F ? baseGravity * fallGravityMultiplier : baseGravity;
    if (gravityScale != appliedGravityScale_) {
        world.setGravityScale(*body, gravityScale);
        appliedGravityScale_ = gravityScale;
    }
    const bool grip = grounded_ && move == 0.0F;
    if (grip != gripping_ || !frictionApplied_) {
        for (const physics::ShapeHandle &shape : context.bodyShapes(entity().id()))
            world.setFriction(shape, grip ? gripFriction : slideFriction);
        gripping_ = grip;
        frictionApplied_ = true;
    }

    if (move != 0.0F)
        facing_ = move > 0.0F ? 1 : -1;
    // With an AnimatedSprite the animation system mirrors the sprite from the `facing` parameter;
    // a plain sprite is mirrored here so a static character still faces where it walks.
    if (!entity().has<AnimatedSprite>())
        if (auto *sprite = entity().get<SpriteRenderer>())
            sprite->flipX = facing_ < 0;
    // Landing: touching down after a real fall (also drives the `landed` animation trigger).
    const bool landedNow = grounded_ && !wasGroundedForAnimation_ && fallSpeed_ >= landingSpeed;
    if (landedNow) {
        if (!landSound.path.empty())
            context.audio().play(landSound.path);
        spawnEffect(context, landEffect, feetOf(entity()));
    }
    if (auto *animated = entity().get<AnimatedSprite>()) {
        // What the body really did last tick (not what was commanded), so pushing against a wall
        // does not look like running.
        const float alongGround = std::fabs(velocity.x - (ground.found ? ground.velocity.x : 0.0F));
        animated->setFloat("speed", alongGround);
        animated->setFloat("speedRatio", moveSpeed > 0.0F ? alongGround / moveSpeed : 0.0);
        animated->setFloat("moveInput", move);
        animated->setFloat("velocityY", velocity.y);
        animated->setBool("grounded", grounded_);
        animated->setFloat("facing", facing_);
        if (jumpedNow)
            animated->trigger("jumped");
        // A landing counts only after a real fall, so stepping off a curb does not squash.
        if (landedNow)
            animated->trigger("landed");
    }
    if (!grounded_)
        fallSpeed_ = std::max(fallSpeed_, velocity.y);
    else
        fallSpeed_ = 0.0F;
    wasGroundedForAnimation_ = grounded_;
}
} // namespace yk
