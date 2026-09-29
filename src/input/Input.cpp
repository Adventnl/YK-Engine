#include "yk/input/Input.hpp"
#include <stdexcept>
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
ButtonState Keyboard::state(Key key) const {
    const auto index = static_cast<std::size_t>(key);
    return index < keys_.size() ? keys_[index] : ButtonState{};
}
ActionBinding::ActionBinding(std::initializer_list<Key> keys) {
    if (keys.size() > keys_.size())
        throw std::invalid_argument("Too many action keys");
    for (Key key : keys) {
        if (key >= Key::Count)
            throw std::invalid_argument("Invalid action key");
        keys_[count_++] = key;
    }
}
void ActionBinding::update(const Keyboard &keyboard) {
    bool down = false;
    bool pressed = false, released = false;
    for (std::size_t i = 0; i < count_; ++i) {
        const auto key = keyboard.state(keys_[i]);
        down = down || key.held;
        pressed = pressed || key.pressed;
        released = released || key.released;
    }
    // Preserve a full tap within one poll, but avoid false edges when switching aliases.
    const bool wasDown = state_.held;
    state_ = {down, !wasDown && (down || pressed), wasDown && !down};
    if (!wasDown && !down && pressed && released)
        state_.released = true;
}
} // namespace yk
