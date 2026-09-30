#pragma once
#include <array>
#include <cstddef>
#include <string_view>

// One list drives the enum, display names, serialization names and (in the platform layer) the SDL
// scancode table, so they cannot drift apart. ENTRY(EnumName, "Display name", SDL scancode suffix).
// clang-format off
#define YK_KEY_LIST(ENTRY) \
    ENTRY(A, "A", A) ENTRY(B, "B", B) ENTRY(C, "C", C) ENTRY(D, "D", D) ENTRY(E, "E", E) \
    ENTRY(F, "F", F) ENTRY(G, "G", G) ENTRY(H, "H", H) ENTRY(I, "I", I) ENTRY(J, "J", J) \
    ENTRY(K, "K", K) ENTRY(L, "L", L) ENTRY(M, "M", M) ENTRY(N, "N", N) ENTRY(O, "O", O) \
    ENTRY(P, "P", P) ENTRY(Q, "Q", Q) ENTRY(R, "R", R) ENTRY(S, "S", S) ENTRY(T, "T", T) \
    ENTRY(U, "U", U) ENTRY(V, "V", V) ENTRY(W, "W", W) ENTRY(X, "X", X) ENTRY(Y, "Y", Y) \
    ENTRY(Z, "Z", Z) \
    ENTRY(Num0, "0", 0) ENTRY(Num1, "1", 1) ENTRY(Num2, "2", 2) ENTRY(Num3, "3", 3) \
    ENTRY(Num4, "4", 4) ENTRY(Num5, "5", 5) ENTRY(Num6, "6", 6) ENTRY(Num7, "7", 7) \
    ENTRY(Num8, "8", 8) ENTRY(Num9, "9", 9) \
    ENTRY(Left, "Left", LEFT) ENTRY(Right, "Right", RIGHT) ENTRY(Up, "Up", UP) \
    ENTRY(Down, "Down", DOWN) \
    ENTRY(Space, "Space", SPACE) ENTRY(Enter, "Enter", RETURN) \
    ENTRY(Escape, "Escape", ESCAPE) ENTRY(Tab, "Tab", TAB) \
    ENTRY(Backspace, "Backspace", BACKSPACE) ENTRY(Delete, "Delete", DELETE) \
    ENTRY(Insert, "Insert", INSERT) ENTRY(Home, "Home", HOME) ENTRY(End, "End", END) \
    ENTRY(PageUp, "PageUp", PAGEUP) ENTRY(PageDown, "PageDown", PAGEDOWN) \
    ENTRY(LeftShift, "LeftShift", LSHIFT) ENTRY(RightShift, "RightShift", RSHIFT) \
    ENTRY(LeftCtrl, "LeftCtrl", LCTRL) ENTRY(RightCtrl, "RightCtrl", RCTRL) \
    ENTRY(LeftAlt, "LeftAlt", LALT) ENTRY(RightAlt, "RightAlt", RALT) \
    ENTRY(Minus, "Minus", MINUS) ENTRY(Equals, "Equals", EQUALS) ENTRY(Comma, "Comma", COMMA) \
    ENTRY(Period, "Period", PERIOD) ENTRY(Slash, "Slash", SLASH) \
    ENTRY(Semicolon, "Semicolon", SEMICOLON) ENTRY(Apostrophe, "Apostrophe", APOSTROPHE) \
    ENTRY(LeftBracket, "LeftBracket", LEFTBRACKET) \
    ENTRY(RightBracket, "RightBracket", RIGHTBRACKET) \
    ENTRY(Grave, "Grave", GRAVE) ENTRY(Backslash, "Backslash", BACKSLASH) \
    ENTRY(F1, "F1", F1) ENTRY(F2, "F2", F2) ENTRY(F3, "F3", F3) ENTRY(F4, "F4", F4) \
    ENTRY(F5, "F5", F5) ENTRY(F6, "F6", F6) ENTRY(F7, "F7", F7) ENTRY(F8, "F8", F8) \
    ENTRY(F9, "F9", F9) ENTRY(F10, "F10", F10) ENTRY(F11, "F11", F11) ENTRY(F12, "F12", F12)
// clang-format on

namespace yk {
enum class Key : std::size_t {
#define YK_KEY_ENUM(name, display, sdl) name,
    YK_KEY_LIST(YK_KEY_ENUM)
#undef YK_KEY_ENUM
        Count
};
constexpr std::size_t keyCount = static_cast<std::size_t>(Key::Count);
// Stable serialization name ("A", "Left", "F5"). Empty for Key::Count.
std::string_view keyName(Key key);
// Exact, case-sensitive inverse of keyName; Key::Count when unknown.
Key keyFromName(std::string_view name);

struct ButtonState {
    bool held{}, pressed{}, released{};
};
class Keyboard {
  public:
    void beginFrame();
    void set(Key key, bool down);
    void releaseAll();
    // Overwrites a key's exact state, for replaying or re-timing input (fixed-step runtime, tests).
    void assign(Key key, ButtonState state);
    ButtonState state(Key key) const;

  private:
    std::array<ButtonState, keyCount> keys_{};
};
// ----- Gamepads ------------------------------------------------------------------------------
// Physical layout names follow the common "Xbox" positions (South = A, East = B, ...), so they mean
// the same thing on every controller SDL recognises.
enum class GamepadButton : std::size_t {
    South,
    East,
    West,
    North,
    Back,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Count
};
enum class GamepadAxis : std::size_t {
    LeftX, // -1 left .. +1 right
    LeftY, // -1 up .. +1 down
    RightX,
    RightY,
    LeftTrigger, // 0 .. 1
    RightTrigger,
    Count
};
constexpr std::size_t gamepadButtonCount = static_cast<std::size_t>(GamepadButton::Count);
constexpr std::size_t gamepadAxisCount = static_cast<std::size_t>(GamepadAxis::Count);
constexpr std::size_t maxGamepads = 4;
std::string_view gamepadButtonName(GamepadButton button);
GamepadButton gamepadButtonFromName(std::string_view name); // Count when unknown.
std::string_view gamepadAxisName(GamepadAxis axis);
GamepadAxis gamepadAxisFromName(std::string_view name); // Count when unknown.

class Gamepad {
  public:
    bool connected() const {
        return connected_;
    }
    void setConnected(bool connected);
    void beginFrame();
    void setButton(GamepadButton button, bool down);
    void setAxis(GamepadAxis axis, float value); // Clamped to [-1, 1]; non-finite becomes 0.
    void assignButton(GamepadButton button, ButtonState state);
    ButtonState button(GamepadButton button) const;
    float axis(GamepadAxis axis) const;

  private:
    bool connected_{};
    std::array<ButtonState, gamepadButtonCount> buttons_{};
    std::array<float, gamepadAxisCount> axes_{};
};

// Everything the player did during one frame (or one fixed tick): the keyboard and every gamepad.
// The runtime turns this into named actions through the project's InputMap, so gameplay code never
// sees a key or a button.
struct InputFrame {
    Keyboard keyboard;
    std::array<Gamepad, maxGamepads> gamepads{};
    void beginFrame();
};
} // namespace yk
