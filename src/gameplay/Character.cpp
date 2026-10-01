#include "yk/gameplay/Character.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/stats/Stats.hpp"
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
            // The motor sets the velocity itself, speeding up and slowing down by its own numbers;
            // damping on top of that would keep a fast character below its running speed.
            if (auto *body = entity.get<RigidBody>())
                body->linearDamping = 0.0F;
        });
    type.field("walkSpeed", &CharacterMotor::walkSpeed).range(0, 50, 0.1);
    type.field("runSpeed", &CharacterMotor::runSpeed).range(0, 50, 0.1);
    type.field("acceleration", &CharacterMotor::acceleration).range(0, 500, 1);
    type.field("deceleration", &CharacterMotor::deceleration).range(0, 500, 1);
    type.field("faceMovement", &CharacterMotor::faceMovement);
    type.field("runStaminaPerSecond", &CharacterMotor::runStaminaPerSecond)
        .range(0, 100, 0.5)
        .tooltip("Stamina spent per second of running; 0 makes running free.");
    type.field("runResumeStamina", &CharacterMotor::runResumeStamina)
        .range(0, 100, 1)
        .tooltip("After running out of stamina, how much it must recover before running again.");
    type.field("staminaStat", &CharacterMotor::staminaStat)
        .ref("stat")
        .tooltip("The stat running spends (a StatSet on the same entity).");
    type.check([](const Entity &, const CharacterMotor &motor, const CheckContext &context,
                  std::vector<std::string> &problems) {
        if (motor.runStaminaPerSecond > 0.0F && context.known &&
            !context.known("stat", motor.staminaStat))
            problems.push_back("running spends the stat '" + motor.staminaStat +
                               "', which is not defined");
    });
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
bool CharacterMotor::suppressed() const {
    const auto *effects = entity().get<StatusEffects>();
    return !suppressions_.empty() || knockbackLeft_ > 0.0F ||
           (effects && effects->hasFlag(characterflags::noMove));
}

void CharacterMotor::onFixedUpdate(GameContext &context, float seconds) {
    const auto body = context.bodyOf(entity().id());
    if (!body)
        return;
    const auto state = context.physics().state(*body);
    if (!state)
        return;
    const auto *effects = entity().get<StatusEffects>();
    Vec2 target{};
    float limit;
    running_ = false;
    if (knockbackLeft_ > 0.0F) {
        knockbackLeft_ = std::max(0.0F, knockbackLeft_ - seconds);
        target = knockbackVelocity_ * (knockbackLeft_ / std::max(knockbackLeft_ + seconds, 1e-4F));
        limit = 1.0e6F; // Knocked back: the shove is the velocity.
    } else {
        if (intentSet_ && suppressions_.empty() &&
            !(effects && effects->hasFlag(characterflags::noMove))) {
            bool run = intentRun_ && intentStrength_ > 0.0F && lengthSquared(intent_) > 1.0e-6F &&
                       !(effects && effects->hasFlag(characterflags::noSprint));
            auto *stats = entity().get<StatSet>();
            if (run && runStaminaPerSecond > 0.0F && stats && stats->has(staminaStat)) {
                if (exhausted_ && stats->value(staminaStat) >= runResumeStamina)
                    exhausted_ = false;
                if (exhausted_) {
                    run = false;
                } else if (!stats->spend(context, staminaStat, runStaminaPerSecond * seconds, "run",
                                         true)) {
                    exhausted_ = true;
                    run = false;
                }
            }
            running_ = run;
            const float factor =
                effects ? static_cast<float>(effects->factor(characterflags::moveSpeed)) : 1.0F;
            target = intent_ * ((run ? runSpeed : walkSpeed) * intentStrength_ * speedMultiplier() *
                                std::max(0.0F, factor));
        }
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
    if (auto *animation = entity().get<AnimatedSprite>())
        animation->setBool("running", running_);
    if (lengthSquared(look) > 1e-6F) { // A turn with no movement still changes the idle clip.
        if (std::fabs(look.x) > std::fabs(look.y))
            facingName_ = look.x < 0 ? "left" : "right";
        else
            facingName_ = look.y < 0 ? "up" : "down";
    }
    intentSet_ = false; // Intents last one tick.
}

void PlayerCharacterController::describe(TypeBuilder<PlayerCharacterController> &type) {
    type.category("Character")
        .description("Lets a person walk and run a CharacterMotor with the named actions of an "
                     "input set. The motor decides what the character may do (stunned, out of "
                     "breath, slowed).")
        .dependsOn("PlayerInput")
        .dependsOn("CharacterMotor");
    type.field("leftAction", &PlayerCharacterController::leftAction).inputAction();
    type.field("rightAction", &PlayerCharacterController::rightAction).inputAction();
    type.field("upAction", &PlayerCharacterController::upAction).inputAction();
    type.field("downAction", &PlayerCharacterController::downAction).inputAction();
    type.field("runAction", &PlayerCharacterController::runAction)
        .inputAction()
        .tooltip("Held to run; empty: the character never runs.");
    type.field("toggleRun", &PlayerCharacterController::toggleRun)
        .tooltip("A press switches running on and off instead of holding it.");
}

void PlayerCharacterController::onFixedUpdate(GameContext &context, float) {
    const auto *input = entity().get<PlayerInput>();
    auto *motor = entity().get<CharacterMotor>();
    if (!input || !motor)
        return;
    const Vec2 direction{input->axis(context, leftAction, rightAction),
                         input->axis(context, upAction, downAction)};
    if (runAction.empty()) {
        runOn_ = false;
    } else {
        const ButtonState button = input->button(context, runAction);
        if (toggleRun) {
            if (button.pressed)
                runOn_ = !runOn_;
            if (lengthSquared(direction) < 1.0e-6F)
                runOn_ = false; // Stopping ends a toggled run.
        } else {
            runOn_ = button.held;
        }
    }
    motor->setIntent(direction, 1.0F, runOn_);
}

void registerCharacterComponents(ComponentRegistry &registry) {
    registry.add<CharacterMotor>("CharacterMotor");
    registry.add<PlayerCharacterController>("PlayerCharacterController");
}
} // namespace yk
