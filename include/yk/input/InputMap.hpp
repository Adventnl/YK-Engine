#pragma once
#include "yk/core/Json.hpp"
#include "yk/core/Result.hpp"
#include "yk/input/Input.hpp"
#include <string>
#include <string_view>
#include <vector>

// The data half of the action system: which physical inputs drive which named actions. Gameplay
// asks for "Jump" in the "Player1" set; only this map knows that means the W key, the south button
// of the first gamepad, or both. Sets are stored in project.ykproj and can be edited in the editor.
namespace yk {
// One physical input that can hold an action.
struct InputBinding {
    enum class Kind { Key, GamepadButton, GamepadAxis };
    Kind kind{Kind::Key};
    Key key{Key::Count};
    GamepadButton button{GamepadButton::Count};
    GamepadAxis axis{GamepadAxis::Count};
    bool axisPositive{true}; // Which half of the axis counts (a stick left is X-, right is X+).

    static InputBinding fromKey(Key value);
    static InputBinding fromButton(GamepadButton value);
    static InputBinding fromAxis(GamepadAxis value, bool positive);
    bool valid() const;
    friend bool operator==(const InputBinding &, const InputBinding &) = default;
};
// "Key:A", "Pad:South", "PadAxis:LeftX-" (the sign selects the half of the axis).
std::string formatBinding(const InputBinding &binding);
Result<InputBinding> parseBinding(std::string_view text);

struct InputAction {
    std::string name;
    std::vector<InputBinding> bindings; // Any one of them holds the action.
};

// The actions one controller (a person, a seat) can perform.
struct ActionSet {
    std::string name;
    int gamepad{-1}; // Gamepad slot whose buttons and axes this set also listens to; -1 for none.
    std::vector<InputAction> actions;

    const InputAction *find(std::string_view action) const;
    InputAction *find(std::string_view action);
};

class InputMap {
  public:
    std::vector<ActionSet> sets;

    // What a new project starts with: Player1 (WASD, gamepad 0), Player2 (arrow keys, gamepad 1)
    // and Global (restart, pause), each with MoveLeft, MoveRight, Jump and Interact where they make
    // sense.
    static InputMap standard();

    const ActionSet *findSet(std::string_view name) const;
    ActionSet *findSet(std::string_view name);
    // Every distinct action name across all sets, in first-use order (for pickers).
    std::vector<std::string> actionNames() const;
    std::vector<std::string> setNames() const;

    // Names must be non-empty and unique (sets, and actions within a set); bindings must be
    // complete; gamepad slots must exist.
    Status validate() const;

    Json toJson() const;
    static Result<InputMap> fromJson(const Json &json);
};
} // namespace yk
