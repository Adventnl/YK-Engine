#include "yk/input/Input.hpp"
#include <algorithm>
#include <cmath>
namespace yk {
namespace {
constexpr std::array<std::string_view, keyCount> keyNames = {
#define YK_KEY_NAME(name, display, sdl) std::string_view(#name),
    YK_KEY_LIST(YK_KEY_NAME)
#undef YK_KEY_NAME
};
} // namespace
std::string_view keyName(Key key) {
    const auto index = static_cast<std::size_t>(key);
    return index < keyNames.size() ? keyNames[index] : std::string_view{};
}
Key keyFromName(std::string_view name) {
    for (std::size_t i = 0; i < keyNames.size(); ++i)
        if (keyNames[i] == name)
            return static_cast<Key>(i);
    return Key::Count;
}
void Keyboard::beginFrame() {
    for (auto &key : keys_) {
        key.pressed = false;
        key.released = false;
    }
}
void Keyboard::set(Key key, bool down) {
    const auto index = static_cast<std::size_t>(key);
    if (index >= keys_.size())
        return;
    auto &state = keys_[index];
    if (state.held == down)
        return;
    state.held = down;
    if (down)
        state.pressed = true;
    else
        state.released = true;
}
void Keyboard::releaseAll() {
    for (std::size_t i = 0; i < keys_.size(); ++i)
        set(static_cast<Key>(i), false);
}
void Keyboard::assign(Key key, ButtonState state) {
    const auto index = static_cast<std::size_t>(key);
    if (index < keys_.size())
        keys_[index] = state;
}
ButtonState Keyboard::state(Key key) const {
    const auto index = static_cast<std::size_t>(key);
    return index < keys_.size() ? keys_[index] : ButtonState{};
}
namespace {
constexpr std::array<std::string_view, gamepadButtonCount> buttonNames = {
    "South",        "East",         "West",         "North",   "Back",
    "Start",        "LeftStick",    "RightStick",   "LeftShoulder", "RightShoulder",
    "DPadUp",       "DPadDown",     "DPadLeft",     "DPadRight"};
constexpr std::array<std::string_view, gamepadAxisCount> axisNames = {
    "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger"};
} // namespace
std::string_view gamepadButtonName(GamepadButton button) {
    const auto index = static_cast<std::size_t>(button);
    return index < buttonNames.size() ? buttonNames[index] : std::string_view{};
}
GamepadButton gamepadButtonFromName(std::string_view name) {
    for (std::size_t i = 0; i < buttonNames.size(); ++i)
        if (buttonNames[i] == name)
            return static_cast<GamepadButton>(i);
    return GamepadButton::Count;
}
std::string_view gamepadAxisName(GamepadAxis axis) {
    const auto index = static_cast<std::size_t>(axis);
    return index < axisNames.size() ? axisNames[index] : std::string_view{};
}
GamepadAxis gamepadAxisFromName(std::string_view name) {
    for (std::size_t i = 0; i < axisNames.size(); ++i)
        if (axisNames[i] == name)
            return static_cast<GamepadAxis>(i);
    return GamepadAxis::Count;
}

void Gamepad::setConnected(bool connected) {
    if (connected == connected_)
        return;
    connected_ = connected;
    if (!connected) { // A pulled controller must not leave buttons stuck down.
        for (auto &button : buttons_)
            button = {};
        axes_.fill(0.0F);
    }
}
void Gamepad::beginFrame() {
    for (auto &button : buttons_) {
        button.pressed = false;
        button.released = false;
    }
}
void Gamepad::setButton(GamepadButton button, bool down) {
    const auto index = static_cast<std::size_t>(button);
    if (index >= buttons_.size())
        return;
    auto &state = buttons_[index];
    if (state.held == down)
        return;
    state.held = down;
    if (down)
        state.pressed = true;
    else
        state.released = true;
}
void Gamepad::setAxis(GamepadAxis axis, float value) {
    const auto index = static_cast<std::size_t>(axis);
    if (index >= axes_.size())
        return;
    axes_[index] = std::isfinite(value) ? std::clamp(value, -1.0F, 1.0F) : 0.0F;
}
void Gamepad::assignButton(GamepadButton button, ButtonState state) {
    const auto index = static_cast<std::size_t>(button);
    if (index < buttons_.size())
        buttons_[index] = state;
}
ButtonState Gamepad::button(GamepadButton button) const {
    const auto index = static_cast<std::size_t>(button);
    return index < buttons_.size() ? buttons_[index] : ButtonState{};
}
float Gamepad::axis(GamepadAxis axis) const {
    const auto index = static_cast<std::size_t>(axis);
    return index < axes_.size() ? axes_[index] : 0.0F;
}

void InputFrame::beginFrame() {
    keyboard.beginFrame();
    for (auto &pad : gamepads)
        pad.beginFrame();
}
} // namespace yk
