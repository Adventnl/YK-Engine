#include "yk/input/InputMap.hpp"
#include <algorithm>

namespace yk {
InputBinding InputBinding::fromKey(Key value) {
    InputBinding binding;
    binding.kind = Kind::Key;
    binding.key = value;
    return binding;
}
InputBinding InputBinding::fromButton(GamepadButton value) {
    InputBinding binding;
    binding.kind = Kind::GamepadButton;
    binding.button = value;
    return binding;
}
InputBinding InputBinding::fromAxis(GamepadAxis value, bool positive) {
    InputBinding binding;
    binding.kind = Kind::GamepadAxis;
    binding.axis = value;
    binding.axisPositive = positive;
    return binding;
}
bool InputBinding::valid() const {
    switch (kind) {
    case Kind::Key:
        return key != Key::Count;
    case Kind::GamepadButton:
        return button != GamepadButton::Count;
    case Kind::GamepadAxis:
        return axis != GamepadAxis::Count;
    }
    return false;
}

std::string formatBinding(const InputBinding &binding) {
    switch (binding.kind) {
    case InputBinding::Kind::Key:
        return "Key:" + std::string(keyName(binding.key));
    case InputBinding::Kind::GamepadButton:
        return "Pad:" + std::string(gamepadButtonName(binding.button));
    case InputBinding::Kind::GamepadAxis:
        return "PadAxis:" + std::string(gamepadAxisName(binding.axis)) +
               (binding.axisPositive ? "+" : "-");
    }
    return {};
}

Result<InputBinding> parseBinding(std::string_view text) {
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos)
        return Error{"Binding '" + std::string(text) +
                     "' must look like Key:A, Pad:South or PadAxis:LeftX-"};
    const std::string_view kind = text.substr(0, colon);
    std::string_view name = text.substr(colon + 1);
    if (kind == "Key") {
        const Key key = keyFromName(name);
        if (key == Key::Count)
            return Error{"Unknown key '" + std::string(name) + "'"};
        return InputBinding::fromKey(key);
    }
    if (kind == "Pad") {
        const GamepadButton button = gamepadButtonFromName(name);
        if (button == GamepadButton::Count)
            return Error{"Unknown gamepad button '" + std::string(name) + "'"};
        return InputBinding::fromButton(button);
    }
    if (kind == "PadAxis") {
        if (name.empty() || (name.back() != '+' && name.back() != '-'))
            return Error{"A gamepad axis binding needs a sign: PadAxis:LeftX- or PadAxis:LeftX+"};
        const bool positive = name.back() == '+';
        name.remove_suffix(1);
        const GamepadAxis axis = gamepadAxisFromName(name);
        if (axis == GamepadAxis::Count)
            return Error{"Unknown gamepad axis '" + std::string(name) + "'"};
        return InputBinding::fromAxis(axis, positive);
    }
    return Error{"Unknown binding kind '" + std::string(kind) + "' (use Key, Pad or PadAxis)"};
}

const InputAction *ActionSet::find(std::string_view action) const {
    for (const InputAction &candidate : actions)
        if (candidate.name == action)
            return &candidate;
    return nullptr;
}
InputAction *ActionSet::find(std::string_view action) {
    return const_cast<InputAction *>(std::as_const(*this).find(action));
}

const ActionSet *InputMap::findSet(std::string_view name) const {
    for (const ActionSet &set : sets)
        if (set.name == name)
            return &set;
    return nullptr;
}
ActionSet *InputMap::findSet(std::string_view name) {
    return const_cast<ActionSet *>(std::as_const(*this).findSet(name));
}
std::vector<std::string> InputMap::actionNames() const {
    std::vector<std::string> names;
    for (const ActionSet &set : sets)
        for (const InputAction &action : set.actions)
            if (std::find(names.begin(), names.end(), action.name) == names.end())
                names.push_back(action.name);
    return names;
}
std::vector<std::string> InputMap::setNames() const {
    std::vector<std::string> names;
    names.reserve(sets.size());
    for (const ActionSet &set : sets)
        names.push_back(set.name);
    return names;
}

InputMap InputMap::standard() {
    const auto key = [](Key value) { return InputBinding::fromKey(value); };
    const auto pad = [](GamepadButton value) { return InputBinding::fromButton(value); };
    const auto axis = [](GamepadAxis value, bool positive) {
        return InputBinding::fromAxis(value, positive);
    };
    InputMap map;
    ActionSet first;
    first.name = "Player1";
    first.gamepad = 0;
    first.actions = {
        {"MoveLeft",
         {key(Key::A), pad(GamepadButton::DPadLeft), axis(GamepadAxis::LeftX, false)}},
        {"MoveRight",
         {key(Key::D), pad(GamepadButton::DPadRight), axis(GamepadAxis::LeftX, true)}},
        {"Jump", {key(Key::W), pad(GamepadButton::South)}},
        {"Interact", {key(Key::S), key(Key::E), pad(GamepadButton::West)}},
    };
    ActionSet second;
    second.name = "Player2";
    second.gamepad = 1;
    second.actions = {
        {"MoveLeft",
         {key(Key::Left), pad(GamepadButton::DPadLeft), axis(GamepadAxis::LeftX, false)}},
        {"MoveRight",
         {key(Key::Right), pad(GamepadButton::DPadRight), axis(GamepadAxis::LeftX, true)}},
        {"Jump", {key(Key::Up), pad(GamepadButton::South)}},
        {"Interact", {key(Key::Down), key(Key::RightCtrl), pad(GamepadButton::West)}},
    };
    ActionSet global;
    global.name = "Global";
    global.gamepad = 0;
    global.actions = {
        {"Restart", {key(Key::R), pad(GamepadButton::Back)}},
        {"Pause", {key(Key::P), pad(GamepadButton::Start)}},
    };
    map.sets = {std::move(first), std::move(second), std::move(global)};
    return map;
}

Status InputMap::validate() const {
    for (std::size_t i = 0; i < sets.size(); ++i) {
        const ActionSet &set = sets[i];
        if (set.name.empty())
            return Error{"Input set #" + std::to_string(i) + " has no name"};
        for (std::size_t other = 0; other < i; ++other)
            if (sets[other].name == set.name)
                return Error{"Input set '" + set.name + "' is defined twice"};
        if (set.gamepad < -1 || set.gamepad >= static_cast<int>(maxGamepads))
            return Error{"Input set '" + set.name + "': gamepad must be -1 or 0-" +
                         std::to_string(maxGamepads - 1)};
        for (std::size_t j = 0; j < set.actions.size(); ++j) {
            const InputAction &action = set.actions[j];
            if (action.name.empty())
                return Error{"Input set '" + set.name + "' has an action without a name"};
            for (std::size_t other = 0; other < j; ++other)
                if (set.actions[other].name == action.name)
                    return Error{"Input set '" + set.name + "' defines action '" + action.name +
                                 "' twice"};
            for (const InputBinding &binding : action.bindings)
                if (!binding.valid())
                    return Error{"Input action '" + set.name + "/" + action.name +
                                 "' has an incomplete binding"};
        }
    }
    return success();
}

Json InputMap::toJson() const {
    Json root = Json::object();
    Json array = Json::array();
    for (const ActionSet &set : sets) {
        Json setJson = Json::object();
        setJson.set("name", set.name);
        if (set.gamepad >= 0)
            setJson.set("gamepad", set.gamepad);
        Json actions = Json::array();
        for (const InputAction &action : set.actions) {
            Json actionJson = Json::object();
            actionJson.set("name", action.name);
            Json bindings = Json::array();
            for (const InputBinding &binding : action.bindings)
                bindings.push(formatBinding(binding));
            actionJson.set("bindings", bindings);
            actions.push(std::move(actionJson));
        }
        setJson.set("actions", actions);
        array.push(std::move(setJson));
    }
    root.set("sets", array);
    return root;
}

Result<InputMap> InputMap::fromJson(const Json &json) {
    if (!json.isObject() || !json.get("sets").isArray())
        return Error{"'input' must be an object with a 'sets' array"};
    InputMap map;
    for (const Json &setJson : json.get("sets").items()) {
        if (!setJson.isObject() || !setJson.get("name").isString())
            return Error{"Each input set needs a 'name' string"};
        ActionSet set;
        set.name = setJson.get("name").asString();
        if (const Json *gamepad = setJson.find("gamepad")) {
            if (!gamepad->isNumber())
                return Error{"Input set '" + set.name + "': 'gamepad' must be a number"};
            set.gamepad = static_cast<int>(gamepad->asInt());
        }
        const Json &actions = setJson.get("actions");
        if (setJson.contains("actions") && !actions.isArray())
            return Error{"Input set '" + set.name + "': 'actions' must be an array"};
        for (const Json &actionJson : actions.items()) {
            if (!actionJson.isObject() || !actionJson.get("name").isString())
                return Error{"Input set '" + set.name + "': each action needs a 'name' string"};
            InputAction action;
            action.name = actionJson.get("name").asString();
            const Json &bindings = actionJson.get("bindings");
            if (actionJson.contains("bindings") && !bindings.isArray())
                return Error{"Input action '" + set.name + "/" + action.name +
                             "': 'bindings' must be an array of strings"};
            for (const Json &text : bindings.items()) {
                if (!text.isString())
                    return Error{"Input action '" + set.name + "/" + action.name +
                                 "': bindings must be strings such as \"Key:A\""};
                auto binding = parseBinding(text.asString());
                if (!binding)
                    return Error{"Input action '" + set.name + "/" + action.name +
                                 "': " + binding.error()};
                action.bindings.push_back(binding.value());
            }
            set.actions.push_back(std::move(action));
        }
        map.sets.push_back(std::move(set));
    }
    if (auto status = map.validate(); !status)
        return Error{status.error()};
    return map;
}
} // namespace yk
