#pragma once
#include "yk/gameplay/Gameplay.hpp"
#include <map>

namespace yk {
// Moves a top-down character on a zero-gravity body, whoever decides where it goes. A player's
// controller, a navigation agent, a script or a status effect all give it the same thing: an
// intent for this tick (a direction and how hard to push), plus modifiers (slowed, carrying
// something heavy), knockback and suppression (stunned, knocked out, in a conversation). The motor
// owns acceleration, speed limits, facing and the animation parameters, so a guard and a player
// move, turn and animate identically and with the same rules.
//
// Intents last one tick: a controller that stops calling setIntent stops the character.
class CharacterMotor final : public Component {
  public:
    float walkSpeed{2.6F};     // m/s
    float runSpeed{4.6F};      // m/s when the intent asks to run
    float acceleration{30.0F}; // m/s^2 speeding up
    float deceleration{40.0F}; // m/s^2 slowing down
    bool faceMovement{true};   // Facing follows the way it moves (off: use setFacing)
    static void describe(TypeBuilder<CharacterMotor> &type);

    // ---- Intent (set every tick by whatever controls the character) ----------------------------
    // `direction` is normalized when longer than one; `strength` (0..1) scales the speed (a path
    // follower eases off near its goal).
    void setIntent(Vec2 direction, float strength = 1.0F, bool run = false);
    void setFacing(Vec2 direction);

    // ---- Modifiers: named so that each can be taken away again ---------------------------------
    void setSpeedModifier(const std::string &source, float multiplier);
    void removeSpeedModifier(const std::string &source);
    float speedMultiplier() const;
    // The character is carried along at `velocity` for `seconds`, ignoring its own intent.
    void knockback(Vec2 velocity, float seconds);
    // While anything suppresses the motor it does not respond to intents (it still stops).
    void suppress(const std::string &reason, bool on);
    bool suppressed() const {
        return !suppressions_.empty() || knockbackLeft_ > 0.0F;
    }

    // ---- State ---------------------------------------------------------------------------------
    Vec2 facing() const {
        return facing_;
    }
    Vec2 velocity() const {
        return velocity_;
    }
    float speed() const {
        return length(velocity_);
    }
    // The way the character last pointed, as the four-way name animation clips use.
    const std::string &facingName() const {
        return facingName_;
    }

    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    Vec2 intent_{};
    float intentStrength_{};
    bool intentRun_{};
    bool intentSet_{};
    Vec2 faceIntent_{};
    Vec2 facing_{0.0F, 1.0F};
    Vec2 velocity_{};
    Vec2 knockbackVelocity_{};
    float knockbackLeft_{};
    std::map<std::string, float> speedModifiers_;
    std::map<std::string, bool> suppressions_;
    std::string animation_;
    std::string facingName_{"down"};
};

void registerCharacterComponents(ComponentRegistry &registry);

namespace detail {
// Shared by every component that walks a character around: configures the body of a top-down
// walker (no gravity, no spin, damped), and publishes the movement parameters to its animation.
void configureWalker(Entity &entity, const char *defaultLayer);
void animateDirection(Entity &entity, Vec2 motion, std::string &facing, std::string &last);
} // namespace detail
} // namespace yk
