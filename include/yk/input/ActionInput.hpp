#pragma once
#include "yk/input/InputMap.hpp"
#include <string_view>
#include <vector>

namespace yk {
// Analog inputs (sticks, triggers) below this are ignored; above it the value is rescaled to 0..1.
inline constexpr float axisDeadzone = 0.25F;
// A bound axis counts as a held button once its rescaled value reaches this.
inline constexpr float axisPressThreshold = 0.5F;

// Evaluates an InputMap against raw input, once per fixed tick. Gameplay reads named actions:
//
//     const ActionInput &input = context.input();
//     if (input.state("Player1", "Jump").pressed) ...
//     const float run = input.axis("Player1", "MoveLeft", "MoveRight");  // -1 .. +1
//
// Asking for an unknown set or action is not an error; it reads as "not pressed", so a component
// can refer to an action a project has not defined without failing.
class ActionInput {
  public:
    ActionInput() = default;
    explicit ActionInput(InputMap map);

    const InputMap &map() const {
        return map_;
    }
    // Releases every action (a scene restart, a lost focus).
    void reset();
    // Recomputes every action from `frame`. Edges (pressed/released) last for the one update that
    // sees them; aliases of one action never produce a false release/press when one takes over from
    // another, and a tap that starts and ends inside one poll still registers.
    void update(const InputFrame &frame);

    ButtonState state(std::string_view set, std::string_view action) const;
    // 0..1: 1 for a held key or button, the rescaled deflection for an axis; the strongest binding
    // wins.
    float value(std::string_view set, std::string_view action) const;
    // value(positive) - value(negative), in -1..+1.
    float axis(std::string_view set, std::string_view negative, std::string_view positive) const;
    bool known(std::string_view set, std::string_view action) const;

  private:
    struct Slot {
        ButtonState state;
        float value{};
    };
    const Slot *slot(std::string_view set, std::string_view action) const;

    InputMap map_;
    std::vector<std::vector<Slot>> slots_; // [set][action], parallel to map_.
};
} // namespace yk
