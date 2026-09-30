#include "yk/input/ActionInput.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
namespace {
// Rescales a deflection beyond the deadzone to 0..1.
float rescaled(float deflection) {
    const float magnitude = std::max(deflection, 0.0F);
    if (magnitude <= axisDeadzone)
        return 0.0F;
    return std::min((magnitude - axisDeadzone) / (1.0F - axisDeadzone), 1.0F);
}
} // namespace

ActionInput::ActionInput(InputMap map) : map_(std::move(map)) {
    reset();
}

void ActionInput::reset() {
    slots_.clear();
    slots_.reserve(map_.sets.size());
    for (const ActionSet &set : map_.sets)
        slots_.emplace_back(set.actions.size());
}

void ActionInput::update(const InputFrame &frame) {
    for (std::size_t s = 0; s < map_.sets.size(); ++s) {
        const ActionSet &set = map_.sets[s];
        const Gamepad *pad = set.gamepad >= 0 && static_cast<std::size_t>(set.gamepad) < maxGamepads
                                 ? &frame.gamepads[static_cast<std::size_t>(set.gamepad)]
                                 : nullptr;
        if (pad && !pad->connected())
            pad = nullptr;
        for (std::size_t a = 0; a < set.actions.size(); ++a) {
            bool digitalHeld = false, edgePressed = false, edgeReleased = false;
            float analog = 0.0F;
            for (const InputBinding &binding : set.actions[a].bindings) {
                switch (binding.kind) {
                case InputBinding::Kind::Key: {
                    const ButtonState key = frame.keyboard.state(binding.key);
                    digitalHeld = digitalHeld || key.held;
                    edgePressed = edgePressed || key.pressed;
                    edgeReleased = edgeReleased || key.released;
                    break;
                }
                case InputBinding::Kind::GamepadButton: {
                    if (!pad)
                        break;
                    const ButtonState button = pad->button(binding.button);
                    digitalHeld = digitalHeld || button.held;
                    edgePressed = edgePressed || button.pressed;
                    edgeReleased = edgeReleased || button.released;
                    break;
                }
                case InputBinding::Kind::GamepadAxis: {
                    if (!pad)
                        break;
                    const float raw = pad->axis(binding.axis);
                    analog = std::max(analog, rescaled(binding.axisPositive ? raw : -raw));
                    break;
                }
                }
            }
            Slot &slot = slots_[s][a];
            const bool wasDown = slot.state.held;
            const bool down = digitalHeld || analog >= axisPressThreshold;
            slot.value = digitalHeld ? 1.0F : analog;
            // Edges come from the change in the combined state, so one alias taking over from
            // another (A released as Left is pressed) is neither a release nor a press. A tap that
            // began and ended inside one poll (both raw edges, never held) is kept.
            slot.state = {down, !wasDown && (down || edgePressed), wasDown && !down};
            if (!wasDown && !down && edgePressed && edgeReleased)
                slot.state.released = true;
        }
    }
}

const ActionInput::Slot *ActionInput::slot(std::string_view set, std::string_view action) const {
    for (std::size_t s = 0; s < map_.sets.size() && s < slots_.size(); ++s) {
        if (map_.sets[s].name != set)
            continue;
        const auto &actions = map_.sets[s].actions;
        for (std::size_t a = 0; a < actions.size() && a < slots_[s].size(); ++a)
            if (actions[a].name == action)
                return &slots_[s][a];
        return nullptr;
    }
    return nullptr;
}

ButtonState ActionInput::state(std::string_view set, std::string_view action) const {
    const Slot *found = slot(set, action);
    return found ? found->state : ButtonState{};
}
float ActionInput::value(std::string_view set, std::string_view action) const {
    const Slot *found = slot(set, action);
    return found ? found->value : 0.0F;
}
float ActionInput::axis(std::string_view set, std::string_view negative,
                        std::string_view positive) const {
    return std::clamp(value(set, positive) - value(set, negative), -1.0F, 1.0F);
}
bool ActionInput::known(std::string_view set, std::string_view action) const {
    return slot(set, action) != nullptr;
}
} // namespace yk
