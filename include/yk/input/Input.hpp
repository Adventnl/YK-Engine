#pragma once
#include <array>
#include <cstddef>
#include <initializer_list>
namespace yk {
enum class Key : std::size_t { A, D, W, S, Left, Right, Up, Down, Escape, Space, Q, E, Count };
struct ButtonState {
    bool held{}, pressed{}, released{};
};
class Keyboard {
  public:
    void beginFrame();
    void set(Key key, bool down);
    void releaseAll();
    ButtonState state(Key key) const;

  private:
    std::array<ButtonState, static_cast<std::size_t>(Key::Count)> keys_{};
};
// Any bound key can hold an action. Query edges for the combined held state.
class ActionBinding {
  public:
    ActionBinding(std::initializer_list<Key> keys);
    void update(const Keyboard &keyboard);
    ButtonState state() const {
        return state_;
    }

  private:
    std::array<Key, static_cast<std::size_t>(Key::Count)> keys_{};
    std::size_t count_{};
    ButtonState state_{};
};
} // namespace yk
