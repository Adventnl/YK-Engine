#include "yk/gameplay/Character.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
namespace detail {
void animateDirection(Entity &entity, Vec2 motion, std::string &facing, std::string &last) {
    if (lengthSquared(motion) > 0.001F) {
        if (std::fabs(motion.x) > std::fabs(motion.y))
            facing = motion.x < 0 ? "left" : "right";
        else
            facing = motion.y < 0 ? "up" : "down";
    }
    if (auto *animation = entity.get<AnimatedSprite>()) {
        animation->setFloat("moveX", motion.x);
        animation->setFloat("moveY", motion.y);
        animation->setBool("moving", lengthSquared(motion) > 0.001F);
        const std::string clip = (lengthSquared(motion) > 0.001F ? "walk_" : "idle_") + facing;
        if (animation->controller.path.empty() && clip != last)
            animation->play(clip);
        last = clip;
    }
}
void configureWalker(Entity &entity, const char *defaultLayer) {
    if (auto *body = entity.get<RigidBody>()) {
        body->type = RigidBodyType::Dynamic;
        body->gravityScale = 0.0F;
        body->fixedRotation = true;
        body->linearDamping = 8.0F;
        body->allowSleep = false;
    }
    if (auto *collider = entity.get<Collider>()) {
        if (collider->size == Vec2{1.0F, 1.0F} && collider->offset == Vec2{}) {
            collider->size = {0.55F, 0.45F};
            collider->offset = {0.0F, 0.28F};
        }
        if (collider->layer == "Default")
            collider->layer = defaultLayer;
    }
}
} // namespace detail

void CharacterMotor::describe(TypeBuilder<CharacterMotor> &type) {
    type.category("Character")
        .description("Moves a top-down character on a zero-gravity body. Players, navigation "
                     "agents, scripts and status effects all steer it through the same intent, "
                     "so every character accelerates, turns and animates by the same rules.")
        .updatePhase(UpdatePhase::Motor)
        .dependsOn("RigidBody")
        .dependsOn("Collider")
        .onAdd([](Entity &entity, CharacterMotor &) {
            detail::configureWalker(entity, layers::player);
        });
    type.field("walkSpeed", &CharacterMotor::walkSpeed).range(0, 50, 0.1);
    type.field("runSpeed", &CharacterMotor::runSpeed).range(0, 50, 0.1);
    type.field("acceleration", &CharacterMotor::acceleration).range(0, 500, 1);
    type.field("deceleration", &CharacterMotor::deceleration).range(0, 500, 1);
    type.field("faceMovement", &CharacterMotor::faceMovement);
}

void CharacterMotor::setIntent(Vec2 direction, float strength, bool run) {
    if (!finite(direction)) {
        direction = {};
    }
    if (lengthSquared(direction) > 1.0F)
        direction = normalized(direction);
    intent_ = direction;
    intentStrength_ = std::clamp(strength, 0.0F, 1.0F);
    intentRun_ = run;
    intentSet_ = true;
}
void CharacterMotor::setFacing(Vec2 direction) {
    if (finite(direction) && lengthSquared(direction) > 1e-6F)
        faceIntent_ = normalized(direction);
}
void CharacterMotor::setSpeedModifier(const std::string &source, float multiplier) {
    speedModifiers_[source] = std::max(0.0F, multiplier);
}
void CharacterMotor::removeSpeedModifier(const std::string &source) {
    speedModifiers_.erase(source);
}
float CharacterMotor::speedMultiplier() const {
    float result = 1.0F;
    for (const auto &[source, multiplier] : speedModifiers_) {
        (void)source;
        result *= multiplier;
    }
    return result;
}
void CharacterMotor::knockback(Vec2 velocity, float seconds) {
    if (!finite(velocity) || !(seconds > 0.0F))
        return;
    knockbackVelocity_ = velocity;
    knockbackLeft_ = seconds;
}
void CharacterMotor::suppress(const std::string &reason, bool on) {
    if (on)
        suppressions_[reason] = true;
    else
        suppressions_.erase(reason);
}

void CharacterMotor::onFixedUpdate(GameContext &context, float seconds) {
    const auto body = context.bodyOf(entity().id());
    if (!body)
        return;
    const auto state = context.physics().state(*body);
    if (!state)
        return;
    Vec2 target{};
    float limit;
    if (knockbackLeft_ > 0.0F) {
        knockbackLeft_ = std::max(0.0F, knockbackLeft_ - seconds);
        target = knockbackVelocity_ * (knockbackLeft_ / std::max(knockbackLeft_ + seconds, 1e-4F));
        limit = 1.0e6F; // Knocked back: the shove is the velocity.
    } else {
        if (intentSet_ && suppressions_.empty())
            target = intent_ *
                     ((intentRun_ ? runSpeed : walkSpeed) * intentStrength_ * speedMultiplier());
        const bool speeding = lengthSquared(target) > lengthSquared(state.value().linearVelocity);
        limit = std::max(0.0F, speeding ? acceleration : deceleration) * seconds;
    }
    Vec2 delta = target - state.value().linearVelocity;
    if (length(delta) > limit)
        delta = normalized(delta) * limit;
    velocity_ = state.value().linearVelocity + delta;
    context.physics().setVelocity(*body, velocity_);
    // Which way it faces: the move, unless something asked for another way.
    Vec2 look = faceIntent_;
    if (lengthSquared(look) < 1e-6F && faceMovement && lengthSquared(target) > 0.0025F)
        look = normalized(target);
    if (lengthSquared(look) > 1e-6F)
        facing_ = look;
    faceIntent_ = {};
    detail::animateDirection(
        entity(),
        lengthSquared(look) > 1e-6F && lengthSquared(velocity_) < 0.0025F ? Vec2{} : velocity_,
        facingName_, animation_);
    if (lengthSquared(look) > 1e-6F) { // A turn with no movement still changes the idle clip.
        if (std::fabs(look.x) > std::fabs(look.y))
            facingName_ = look.x < 0 ? "left" : "right";
        else
            facingName_ = look.y < 0 ? "up" : "down";
    }
    intentSet_ = false; // Intents last one tick.
}

void registerCharacterComponents(ComponentRegistry &registry) {
    registry.add<CharacterMotor>("CharacterMotor");
}
} // namespace yk
