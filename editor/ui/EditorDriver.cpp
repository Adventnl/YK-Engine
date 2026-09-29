#include "ui/EditorDriver.hpp"
#include "core/EditorGeometry.hpp"
#include "imgui.h"
#include "ui/UiCommon.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace yk::editor {
namespace {
enum class Aim { Ready, Waiting, Failed };

std::string expandVariables(const std::string &text) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '{') {
            const auto close = text.find('}', i);
            if (close != std::string::npos) {
                const std::string name = text.substr(i + 2, close - i - 2);
                if (const auto value = environmentVariable(name))
                    out += *value;
                i = close + 1;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

// Splits a line into words; double quotes group words and \" is a quote character.
std::vector<std::string> tokenize(const std::string &line) {
    std::vector<std::string> words;
    std::string current;
    bool quoted = false, started = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '\\' && i + 1 < line.size() && line[i + 1] == '"') {
            current += '"';
            started = true;
            ++i;
        } else if (c == '"') {
            quoted = !quoted;
            started = true;
        } else if (!quoted && std::isspace(static_cast<unsigned char>(c))) {
            if (started)
                words.push_back(expandVariables(current));
            current.clear();
            started = false;
        } else {
            current += c;
            started = true;
        }
    }
    if (started)
        words.push_back(expandVariables(current));
    return words;
}

std::optional<double> number(const std::string &text) {
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0')
        return std::nullopt;
    return value;
}

std::optional<std::pair<double, double>> pair(const std::string &text) {
    const auto comma = text.find(',');
    if (comma == std::string::npos)
        return std::nullopt;
    const auto x = number(text.substr(0, comma));
    const auto y = number(text.substr(comma + 1));
    if (!x || !y)
        return std::nullopt;
    return std::make_pair(*x, *y);
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::optional<SDL_Scancode> scancodeFor(const std::string &name) {
    const std::string key = lower(name);
    if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z')
        return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (key[0] - 'a'));
    if (key.size() == 1 && key[0] >= '1' && key[0] <= '9')
        return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (key[0] - '1'));
    if (key == "0")
        return SDL_SCANCODE_0;
    if (key.size() >= 2 && key[0] == 'f') {
        if (const auto n = number(key.substr(1)); n && *n >= 1 && *n <= 12)
            return static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + static_cast<int>(*n) - 1);
    }
    static const std::map<std::string, SDL_Scancode> named = {
        {"escape", SDL_SCANCODE_ESCAPE}, {"esc", SDL_SCANCODE_ESCAPE},
        {"enter", SDL_SCANCODE_RETURN},  {"return", SDL_SCANCODE_RETURN},
        {"delete", SDL_SCANCODE_DELETE}, {"backspace", SDL_SCANCODE_BACKSPACE},
        {"tab", SDL_SCANCODE_TAB},       {"space", SDL_SCANCODE_SPACE},
        {"left", SDL_SCANCODE_LEFT},     {"right", SDL_SCANCODE_RIGHT},
        {"up", SDL_SCANCODE_UP},         {"down", SDL_SCANCODE_DOWN},
        {"home", SDL_SCANCODE_HOME},     {"end", SDL_SCANCODE_END},
        {"pageup", SDL_SCANCODE_PAGEUP}, {"pagedown", SDL_SCANCODE_PAGEDOWN},
        {"insert", SDL_SCANCODE_INSERT}, {"minus", SDL_SCANCODE_MINUS},
        {"equals", SDL_SCANCODE_EQUALS}};
    const auto found = named.find(key);
    if (found == named.end())
        return std::nullopt;
    return found->second;
}

struct KeyMods {
    bool ctrl{}, shift{}, alt{};
    bool any() const {
        return ctrl || shift || alt;
    }
};

struct SdlInput {
    SDL_Window *window;
    SDL_Keymod mods{SDL_KMOD_NONE};

    SDL_WindowID id() const {
        return SDL_GetWindowID(window);
    }
    void key(SDL_Scancode scancode, bool down) {
        SDL_Event event;
        SDL_zero(event);
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = id();
        event.key.scancode = scancode;
        event.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
        event.key.mod = mods;
        event.key.down = down;
        SDL_PushEvent(&event);
    }
    void setModifier(SDL_Scancode scancode, SDL_Keymod flag, bool down) {
        if (down)
            mods = static_cast<SDL_Keymod>(mods | flag);
        else
            mods = static_cast<SDL_Keymod>(mods & ~flag);
        key(scancode, down);
    }
    void modifiers(const KeyMods &wanted, bool down) {
        if (wanted.ctrl)
            setModifier(SDL_SCANCODE_LCTRL, SDL_KMOD_LCTRL, down);
        if (wanted.shift)
            setModifier(SDL_SCANCODE_LSHIFT, SDL_KMOD_LSHIFT, down);
        if (wanted.alt)
            setModifier(SDL_SCANCODE_LALT, SDL_KMOD_LALT, down);
    }
    void move(float x, float y) {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = id();
        event.motion.x = x;
        event.motion.y = y;
        SDL_PushEvent(&event);
    }
    void button(Uint8 which, bool down, int clicks, float x, float y) {
        SDL_Event event;
        SDL_zero(event);
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = id();
        event.button.button = which;
        event.button.down = down;
        event.button.clicks = static_cast<Uint8>(clicks);
        event.button.x = x;
        event.button.y = y;
        SDL_PushEvent(&event);
    }
    void wheel(float amount, float x, float y) {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.windowID = id();
        event.wheel.y = amount;
        event.wheel.mouse_x = x;
        event.wheel.mouse_y = y;
        SDL_PushEvent(&event);
    }
};

KeyMods modifiersFrom(const std::vector<std::string> &words, std::size_t first) {
    KeyMods result;
    for (std::size_t i = first; i < words.size(); ++i) {
        const std::string word = lower(words[i]);
        result.ctrl = result.ctrl || word == "ctrl";
        result.shift = result.shift || word == "shift";
        result.alt = result.alt || word == "alt";
    }
    return result;
}

struct Combo {
    KeyMods modifiers;
    SDL_Scancode key{SDL_SCANCODE_UNKNOWN};
};
std::optional<Combo> parseCombo(const std::string &text) {
    Combo combo;
    std::stringstream parts(text);
    std::string part;
    std::string keyName;
    while (std::getline(parts, part, '+')) {
        const std::string word = lower(part);
        if (word == "ctrl")
            combo.modifiers.ctrl = true;
        else if (word == "shift")
            combo.modifiers.shift = true;
        else if (word == "alt")
            combo.modifiers.alt = true;
        else
            keyName = part;
    }
    const auto scancode = scancodeFor(keyName);
    if (!scancode)
        return std::nullopt;
    combo.key = *scancode;
    return combo;
}

// The property's value as text a script can compare against.
std::string formatValue(const Scene &scene, const PropertyInfo &property,
                        const PropertyValue &value) {
    const auto trim = [](double v) {
        char buffer[48];
        std::snprintf(buffer, sizeof buffer, "%.4f", v);
        std::string text = buffer;
        while (text.size() > 1 && text.back() == '0')
            text.pop_back();
        if (!text.empty() && text.back() == '.')
            text.pop_back();
        return text;
    };
    switch (property.type) {
    case PropertyType::Bool:
        return std::get<bool>(value) ? "true" : "false";
    case PropertyType::Int:
        return std::to_string(std::get<std::int64_t>(value));
    case PropertyType::Float:
        return trim(std::get<double>(value));
    case PropertyType::String:
        return std::get<std::string>(value);
    case PropertyType::Vec2: {
        const Vec2 v = std::get<Vec2>(value);
        return trim(static_cast<double>(v.x)) + "," + trim(static_cast<double>(v.y));
    }
    case PropertyType::Color:
        return formatColor(std::get<Color>(value));
    case PropertyType::Enum: {
        const auto index = static_cast<std::size_t>(std::get<std::int64_t>(value));
        return index < property.options.size() ? property.options[index] : "?";
    }
    case PropertyType::EntityReference: {
        const EntityId id = std::get<EntityId>(value);
        if (!id)
            return "none";
        const Entity *entity = scene.find(id);
        return entity ? entity->name() : "missing";
    }
    case PropertyType::EntityReferenceList: {
        std::string text;
        for (const EntityId id : std::get<std::vector<EntityId>>(value)) {
            const Entity *entity = scene.find(id);
            text += (text.empty() ? "" : ",") + (entity ? entity->name() : std::string("missing"));
        }
        return text.empty() ? "empty" : text;
    }
    case PropertyType::StringList: {
        std::string text;
        for (const std::string &item : std::get<std::vector<std::string>>(value))
            text += (text.empty() ? "" : ",") + item;
        return text.empty() ? "empty" : text;
    }
    case PropertyType::Asset:
        return std::get<AssetRef>(value).path;
    }
    return {};
}

// Numbers (and comma separated numbers) match within a small tolerance; everything else exactly.
bool sameValue(const std::string &actual, const std::string &expected, double tolerance = 0.011) {
    if (actual == expected)
        return true;
    const auto near = [&](const std::string &a, const std::string &b) {
        const auto x = number(a), y = number(b);
        return x && y && std::abs(*x - *y) <= tolerance;
    };
    if (near(actual, expected))
        return true;
    const auto actualPair = pair(actual), expectedPair = pair(expected);
    return actualPair && expectedPair &&
           std::abs(actualPair->first - expectedPair->first) <= tolerance &&
           std::abs(actualPair->second - expectedPair->second) <= tolerance;
}

const Entity *findNamed(const Scene *scene, const std::string &name) {
    if (!scene)
        return nullptr;
    return scene->findByName(name);
}
} // namespace

Result<std::unique_ptr<EditorDriver>> EditorDriver::parse(const std::string &script) {
    auto driver = std::unique_ptr<EditorDriver>(new EditorDriver());
    std::stringstream lines(script);
    std::string text;
    int number = 0;
    while (std::getline(lines, text)) {
        ++number;
        const auto hash = text.find('#');
        if (hash != std::string::npos &&
            (hash == 0 || std::isspace(static_cast<unsigned char>(text[hash - 1]))))
            text.erase(hash);
        auto words = tokenize(text);
        if (words.empty())
            continue;
        words[0] = lower(words[0]);
        const auto add = [&](std::vector<std::string> command) {
            command[0] = lower(command[0]);
            driver->commands_.push_back({std::move(command), number});
        };
        if (words[0] == "edit" && words.size() >= 3) {
            // Types a value into a drag/text field: double-click to edit, replace everything,
            // Enter.
            std::string value;
            for (std::size_t i = 2; i < words.size(); ++i)
                value += (value.empty() ? "" : " ") + words[i];
            add({"doubleclick", words[1]});
            add({"wait", "2"});
            add({"key", "ctrl+a"});
            add({"type", value});
            add({"key", "enter"});
            add({"wait", "2"});
        } else if (words[0] == "menu" && words.size() >= 2) {
            // menu File/Save Scene -> click each level of the menu path. Items whose own name has a
            // slash (a scene path) need '|' as the separator: menu "File|Open
            // Scene|scenes/a.ykscene".
            std::string path = "menu";
            std::stringstream parts(words[1]);
            std::string part;
            const char separator = words[1].find('|') != std::string::npos ? '|' : '/';
            while (std::getline(parts, part, separator)) {
                path += "/" + part;
                add({"click", path});
            }
        } else if (words[0] == "create" && words.size() >= 2) {
            // create Level/Platform -> hierarchy "+" button, category, template.
            const auto slash = words[1].find('/');
            add({"click", "hierarchy/create"});
            if (slash != std::string::npos) {
                add({"click", "create-category/" + words[1].substr(0, slash)});
                add({"click", "create/" + words[1].substr(slash + 1)});
            } else {
                add({"click", "create/" + words[1]});
            }
            add({"wait", "2"});
        } else {
            driver->commands_.push_back({std::move(words), number});
        }
    }
    return driver;
}

Result<std::unique_ptr<EditorDriver>> EditorDriver::load(const std::filesystem::path &file) {
    auto text = readTextFile(file);
    if (!text)
        return Error{text.error()};
    return parse(text.value());
}

bool EditorDriver::onlyTrivialCommandsLeft() const {
    for (std::size_t i = index_; i < commands_.size(); ++i) {
        const auto &words = commands_[i].words;
        const std::string &name = words[0];
        const bool trivial = name == "wait" || name == "log" || name == "quit" ||
                             name == "settle" ||
                             (name == "expect" && words.size() > 1 && words[1] == "quitting");
        if (!trivial)
            return false;
    }
    return true;
}

void EditorDriver::fail(EditorApp &app, const Command &command, const std::string &detail) {
    ++failures_;
    std::string line;
    for (const std::string &word : command.words)
        line += (line.empty() ? "" : " ") + word;
    std::fprintf(stderr, "SCRIPT FAIL line %d: %s\n    %s\n", command.line, line.c_str(),
                 detail.c_str());
    if (!failureDirectory_.empty()) {
        std::error_code error;
        std::filesystem::create_directories(failureDirectory_, error);
        app.requestCapture(failureDirectory_ /
                           ("failure-line-" + std::to_string(command.line) + ".bmp"));
    }
}

bool EditorDriver::resolve(EditorApp &app, const std::string &target, Point &out,
                           std::string &problem) {
    EditorState &state = app.state();
    std::string spec = target;
    if (spec.starts_with("widget:"))
        spec = spec.substr(7);
    if (spec.starts_with("world:")) {
        const auto point = pair(spec.substr(6));
        if (!point) {
            problem = "bad world point '" + target + "'";
            return false;
        }
        if (!state.sceneView.visible || !state.interaction.bound()) {
            problem = "the scene view is not showing a scene";
            return false;
        }
        const Vec2 screen = state.sceneView.origin +
                            state.interaction.toScreen({static_cast<float>(point->first),
                                                        static_cast<float>(point->second)});
        out = {screen.x, screen.y};
        return true;
    }
    if (spec.starts_with("px:")) {
        const auto point = pair(spec.substr(3));
        if (!point) {
            problem = "bad pixel position '" + target + "'";
            return false;
        }
        out = {static_cast<float>(point->first), static_cast<float>(point->second)};
        return true;
    }
    if (const auto rect = ui::widgets().find(spec)) {
        if (!ui::widgets().visible(spec)) {
            // Scrolled out of its panel: ask the panel to bring it into view and try again.
            ui::widgets().requestReveal(spec);
            problem = "widget '" + spec + "' stays out of view";
            return false;
        }
        out = {rect->position.x + rect->size.x * 0.5F, rect->position.y + rect->size.y * 0.5F};
        return true;
    }
    problem = "no widget '" + spec + "'";
    return false;
}

void EditorDriver::rememberRuntime(EditorApp &app) {
    EditorState &state = app.state();
    const bool playing = state.playing();
    if (playing && !wasPlaying_) {
        runtimeStart_.clear();
        state.play->runtime().scene().forEach([&](const Entity &entity) {
            const Vec2 at = entity.worldPosition();
            runtimeStart_.emplace(entity.name(), Point{at.x, at.y});
        });
    }
    wasPlaying_ = playing;
}

int EditorDriver::check(EditorApp &app, const Command &command, std::string &detail) {
    EditorState &state = app.state();
    const auto &w = command.words;
    if (w.size() < 2) {
        detail = "expect needs a condition";
        return -1;
    }
    const std::string what = lower(w[1]);
    const Scene *scene = state.visibleScene();
    const auto need = [&](std::size_t count) {
        if (w.size() < count + 2) {
            detail = "'expect " + what + "' needs " + std::to_string(count) + " argument(s)";
            return false;
        }
        return true;
    };
    const auto report = [&](bool ok, const std::string &why) {
        if (!ok)
            detail = why;
        return ok ? 1 : 0;
    };

    if (what == "playing")
        return report(state.playing(), "the editor is not playing");
    if (what == "editing")
        return report(!state.playing(), "the editor is still playing");
    if (what == "paused")
        return report(state.playing() && state.play->paused(), "play mode is not paused");
    if (what == "running")
        return report(state.playing() && !state.play->paused(), "play mode is not running");
    if (what == "dirty")
        return report(state.document && state.document->dirty(),
                      "the scene has no unsaved changes");
    if (what == "clean")
        return report(state.document && !state.document->dirty(), "the scene has unsaved changes");
    if (what == "can-undo")
        return report(state.document && state.document->canUndo(), "nothing to undo");
    if (what == "cannot-undo")
        return report(state.document && !state.document->canUndo(), "there is something to undo");
    if (what == "no-dialog")
        return report(state.dialog.kind == DialogKind::None, "a dialog is open");
    if (what == "no-project")
        return report(!state.project, "a project is open");
    if (what == "no-asset")
        return report(state.selectedAsset.empty(), "'" + state.selectedAsset + "' is selected");
    if (what == "no-pick")
        return report(!state.pick, "waiting for the user to pick an entity");
    if (what == "pick")
        return report(state.pick.has_value(), "not waiting for a pick");
    if (what == "layout-saved") { // expect layout-saved : workbench.json matches the live layout
        std::error_code error;
        const auto file = state.settingsDirectory / "workbench.json";
        if (!std::filesystem::exists(file, error))
            return report(false, "workbench.json has not been written");
        const auto text = readTextFile(file);
        const auto json = text ? Json::parse(text.value()) : Result<Json>(Error{"unreadable"});
        return report(json && WorkbenchLayout::fromJson(json.value()) == state.layout,
                      "workbench.json differs from the live layout");
    }
    if (!need(1))
        return -1;
    const std::string &arg = w[2];

    if (what == "selected") {
        const Entity *entity = scene ? scene->find(state.inspected()) : nullptr;
        if (lower(arg) == "none")
            return report(!entity, "'" + entity->name() + "' is selected");
        return report(entity && entity->name() == arg,
                      "selected is " +
                          (entity ? "'" + entity->name() + "'" : std::string("nothing")));
    }
    if (what == "selection-count") {
        const std::size_t count = state.document ? state.document->selection().size() : 0;
        return report(count == static_cast<std::size_t>(number(arg).value_or(-1)),
                      "selection holds " + std::to_string(count));
    }
    if (what == "selection-at-least") {
        const std::size_t count = state.document ? state.document->selection().size() : 0;
        return report(count >= static_cast<std::size_t>(number(arg).value_or(1e9)),
                      "selection holds only " + std::to_string(count));
    }
    if (what == "entity")
        return report(findNamed(scene, arg) != nullptr, "there is no entity named '" + arg + "'");
    if (what == "no-entity")
        return report(findNamed(scene, arg) == nullptr, "an entity named '" + arg + "' exists");
    if (what == "entity-count") {
        const std::size_t count = scene ? scene->size() : 0;
        return report(count == static_cast<std::size_t>(number(arg).value_or(-1)),
                      "the scene has " + std::to_string(count) + " entities");
    }
    if (what == "component-count") { // expect component-count TYPE N
        if (!need(2))
            return -1;
        std::size_t count = 0;
        if (scene)
            scene->forEach(
                [&](const Entity &entity) { count += entity.findComponent(arg) ? 1 : 0; });
        return report(count == static_cast<std::size_t>(number(w[3]).value_or(-1)),
                      std::to_string(count) + " entities have " + arg);
    }
    if (what == "has-component") { // expect has-component ENTITY TYPE
        if (!need(2))
            return -1;
        const Entity *entity = findNamed(scene, arg);
        return report(entity && entity->findComponent(w[3]), "'" + arg + "' has no " + w[3]);
    }
    if (what == "no-component") {
        if (!need(2))
            return -1;
        const Entity *entity = findNamed(scene, arg);
        return report(entity && !entity->findComponent(w[3]), "'" + arg + "' still has " + w[3]);
    }
    if (what == "prop" || what == "prop-not") { // expect prop ENTITY Component.property VALUE
        if (!need(3))
            return -1;
        const Entity *entity = findNamed(scene, arg);
        if (!entity)
            return report(false, "there is no entity named '" + arg + "'");
        const std::string &path = w[3];
        const auto dot = path.find('.');
        if (dot == std::string::npos) {
            detail = "property must look like Component.property";
            return -1;
        }
        const std::string componentName = path.substr(0, dot), propertyName = path.substr(dot + 1);
        std::string actual;
        if (componentName == "Transform") {
            const Transform2D &t = entity->transform();
            char buffer[64];
            if (propertyName == "position")
                std::snprintf(buffer, sizeof buffer, "%.4f,%.4f", static_cast<double>(t.position.x),
                              static_cast<double>(t.position.y));
            else if (propertyName == "scale")
                std::snprintf(buffer, sizeof buffer, "%.4f,%.4f", static_cast<double>(t.scale.x),
                              static_cast<double>(t.scale.y));
            else if (propertyName == "rotation")
                std::snprintf(buffer, sizeof buffer, "%.4f",
                              static_cast<double>(t.rotationDegrees));
            else {
                detail = "Transform has position, rotation, scale";
                return -1;
            }
            actual = buffer;
        } else if (componentName == "Entity") {
            actual = propertyName == "name" ? entity->name() : entity->active() ? "true" : "false";
        } else {
            const Component *component = entity->findComponent(componentName);
            if (!component)
                return report(false, "'" + arg + "' has no " + componentName);
            const PropertyInfo *property = component->type().find(propertyName);
            if (!property) {
                detail = componentName + " has no property '" + propertyName + "'";
                return -1;
            }
            actual = formatValue(*scene, *property, property->get(*component));
        }
        const bool same = sameValue(actual, w[4]);
        return report(what == "prop" ? same : !same,
                      "'" + arg + "." + path + "' is '" + actual + "'");
    }
    if (what == "position") { // expect position ENTITY x,y
        if (!need(2))
            return -1;
        const Entity *entity = findNamed(scene, arg);
        const auto wanted = pair(w[3]);
        if (!entity || !wanted) {
            detail = !wanted ? "position must be x,y" : "there is no entity named '" + arg + "'";
            return !wanted ? -1 : 0;
        }
        const Vec2 at = entity->worldPosition();
        const double tolerance = w.size() > 4 ? number(w[4]).value_or(0.02) : 0.02;
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "at %.3f,%.3f", static_cast<double>(at.x),
                      static_cast<double>(at.y));
        return report(std::abs(static_cast<double>(at.x) - wanted->first) <= tolerance &&
                          std::abs(static_cast<double>(at.y) - wanted->second) <= tolerance,
                      "'" + arg + "' is " + buffer);
    }
    if (what == "size") { // expect size ENTITY w,h : the size of the entity's selectable box
        if (!need(2))
            return -1;
        const Entity *entity = findNamed(scene, arg);
        const auto wanted = pair(w[3]);
        const auto bounds = entity ? localBounds(*entity) : std::nullopt;
        if (!wanted) {
            detail = "size must be w,h";
            return -1;
        }
        if (!bounds)
            return report(false, "'" + arg + "' has no size");
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "is %.3f x %.3f", static_cast<double>(bounds->size.x),
                      static_cast<double>(bounds->size.y));
        return report(std::abs(static_cast<double>(bounds->size.x) - wanted->first) <= 0.02 &&
                          std::abs(static_cast<double>(bounds->size.y) - wanted->second) <= 0.02,
                      "'" + arg + "' " + buffer);
    }
    if (what == "parent") { // expect parent CHILD PARENT|none
        if (!need(2))
            return -1;
        const Entity *child = findNamed(scene, arg);
        if (!child)
            return report(false, "there is no entity named '" + arg + "'");
        const Entity *parent = child->parent();
        const std::string actual = parent ? parent->name() : "none";
        return report(actual == w[3], "'" + arg + "' has parent '" + actual + "'");
    }
    if (what == "no-links") { // expect no-links FROM TO
        if (!need(2))
            return -1;
        const Entity *from = findNamed(scene, arg), *to = findNamed(scene, w[3]);
        if (!from || !to)
            return report(false, "unknown entity in the link check");
        for (const Link &link : linksFrom(*scene, from->id()))
            if (link.to == to->id())
                return report(false, "'" + arg + "' still links to '" + w[3] + "'");
        return 1;
    }
    if (what == "links") { // expect links FROM TO
        if (!need(2))
            return -1;
        const Entity *from = findNamed(scene, arg), *to = findNamed(scene, w[3]);
        if (!from || !to)
            return report(false, "unknown entity in the link check");
        for (const Link &link : linksFrom(*scene, from->id()))
            if (link.to == to->id())
                return 1;
        return report(false, "'" + arg + "' does not link to '" + w[3] + "'");
    }
    if (what == "runtime-position") { // expect runtime-position ENTITY x,y [tolerance]
        if (!need(2))
            return -1;
        const Entity *entity = state.playing() ? findNamed(scene, arg) : nullptr;
        const auto wanted = pair(w[3]);
        if (!wanted) {
            detail = "position must be x,y";
            return -1;
        }
        if (!entity)
            return report(false, "not playing, or no entity '" + arg + "'");
        const Vec2 at = entity->worldPosition();
        const double tolerance = w.size() > 4 ? number(w[4]).value_or(0.1) : 0.1;
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "at %.3f,%.3f", static_cast<double>(at.x),
                      static_cast<double>(at.y));
        return report(std::abs(static_cast<double>(at.x) - wanted->first) <= tolerance &&
                          std::abs(static_cast<double>(at.y) - wanted->second) <= tolerance,
                      "'" + arg + "' is " + buffer);
    }
    if (what == "runtime-moved") { // expect runtime-moved ENTITY x>=2 y<=0.5 ...
        const Entity *entity = state.playing() ? findNamed(scene, arg) : nullptr;
        const auto start = runtimeStart_.find(arg);
        if (!entity || start == runtimeStart_.end())
            return report(false, "not playing, or no entity '" + arg + "'");
        const Vec2 at = entity->worldPosition();
        const double dx = static_cast<double>(at.x) - static_cast<double>(start->second.x);
        const double dy = static_cast<double>(at.y) - static_cast<double>(start->second.y);
        for (std::size_t i = 3; i < w.size(); ++i) {
            const std::string &condition = w[i];
            if (condition.size() < 3 || (condition[0] != 'x' && condition[0] != 'y')) {
                detail = "conditions look like x>=2, y<=-1, x~0";
                return -1;
            }
            const double value = condition[0] == 'x' ? dx : dy;
            const std::string op = condition[1] == '~' ? "~" : condition.substr(1, 2);
            const auto limit = number(condition.substr(op.size() + 1));
            if (!limit) {
                detail = "bad number in '" + condition + "'";
                return -1;
            }
            const bool ok = op == ">="   ? value >= *limit
                            : op == "<=" ? value <= *limit
                                         : std::abs(value - *limit) <= 0.15;
            if (!ok) {
                char buffer[96];
                std::snprintf(buffer, sizeof buffer, "moved by %.3f,%.3f; wanted %s", dx, dy,
                              condition.c_str());
                return report(false, "'" + arg + "' " + buffer);
            }
        }
        return 1;
    }
    if (what == "widget")
        return report(ui::widgets().find(arg).has_value(), "no widget '" + arg + "' on screen");
    if (what == "no-widget")
        return report(!ui::widgets().find(arg).has_value(), "widget '" + arg + "' is on screen");
    if (what == "dialog") {
        static const std::map<std::string, DialogKind> kinds = {
            {"newproject", DialogKind::NewProject},    {"openproject", DialogKind::OpenProject},
            {"newscene", DialogKind::NewScene},        {"savesceneas", DialogKind::SaveSceneAs},
            {"saveprefab", DialogKind::SavePrefab},    {"unsaved", DialogKind::Unsaved},
            {"settings", DialogKind::ProjectSettings}, {"export", DialogKind::Export},
            {"validation", DialogKind::Validation},    {"about", DialogKind::About},
            {"shortcuts", DialogKind::Shortcuts},      {"message", DialogKind::Message},
            {"confirm", DialogKind::Confirm},          {"moveasset", DialogKind::MoveAsset}};
        const auto found = kinds.find(lower(arg));
        if (found == kinds.end()) {
            detail = "unknown dialog '" + arg + "'";
            return -1;
        }
        return report(state.dialog.kind == found->second, "that dialog is not open");
    }
    if (what == "dialog-text") // expect dialog-text TEXT: the open dialog's message contains it
        return report(state.dialog.kind != DialogKind::None &&
                          state.dialog.message.find(arg) != std::string::npos,
                      "the dialog says '" + state.dialog.message + "'");
    if (what == "console") {
        for (const auto &entry : state.console.snapshot())
            if (entry.message.find(arg) != std::string::npos)
                return 1;
        return report(false, "the console never said '" + arg + "'");
    }
    if (what == "tool") {
        const Tool wanted = lower(arg) == "resize"   ? Tool::Resize
                            : lower(arg) == "rotate" ? Tool::Rotate
                                                     : Tool::Move;
        return report(state.interaction.tool == wanted, "another tool is active");
    }
    if (what == "undo")
        return report(state.document && state.document->canUndo() &&
                          state.document->undoLabel() == arg,
                      "the next undo is '" +
                          (state.document ? state.document->undoLabel() : std::string()) + "'");
    if (what == "scene")
        return report(state.document && state.document->path() == arg,
                      "the open scene is '" +
                          (state.document ? state.document->path() : std::string("none")) + "'");
    if (what == "project")
        return report(state.project && state.project->project().name == arg,
                      "the open project is '" +
                          (state.project ? state.project->project().name : std::string("none")) +
                          "'");
    if (what == "project-root") { // expect project-root DIRECTORY
        if (!need(1))
            return -1;
        std::error_code error;
        const auto wanted = std::filesystem::weakly_canonical(arg, error);
        const auto actual =
            state.project ? std::filesystem::weakly_canonical(state.project->project().root, error)
                          : std::filesystem::path();
        return report(state.project && actual == wanted,
                      "the open project is in '" + actual.string() + "'");
    }
    if (what == "file") { // expect file PATH exists|missing
        if (!need(2))
            return -1;
        std::error_code error;
        std::filesystem::path path = arg;
        if (path.is_relative() && state.project)
            path = state.project->project().root / path;
        const bool exists = std::filesystem::exists(path, error);
        return report(lower(w[3]) == "exists" ? exists : !exists,
                      "'" + path.string() + (exists ? "' exists" : "' does not exist"));
    }
    if (what == "file-contains" || what == "file-lacks") { // expect file-contains PATH TEXT
        if (!need(2))
            return -1;
        std::filesystem::path path = arg;
        if (path.is_relative() && state.project)
            path = state.project->project().root / path;
        const auto text = readTextFile(path);
        const bool found = text && text.value().find(w[3]) != std::string::npos;
        return report(what == "file-contains" ? found : !found,
                      "'" + path.string() + (found ? "' contains '" : "' does not contain '") +
                          w[3] + "'");
    }
    if (what == "asset") // expect asset PATH : the Explorer's selection, shown in the Inspector
        return report(state.selectedAsset == arg,
                      "the selected asset is '" + state.selectedAsset + "'");
    if (what == "start-scene")
        return report(state.project && state.project->project().startScene == arg,
                      "the start scene differs");
    // The workbench: which side bar view and panel are showing, how the editor area is split.
    if (what == "side-view") { // expect side-view Explorer|Scene|Prefabs|Components|Build|none
        if (lower(arg) == "none")
            return report(!state.layout.sideBarVisible,
                          "the side bar is showing " + std::string(name(state.layout.sideView)));
        const auto wanted = sideViewFromName(lower(arg));
        if (!wanted) {
            detail = "unknown side bar view '" + arg + "'";
            return -1;
        }
        return report(state.layout.sideBarVisible && state.layout.sideView == *wanted,
                      state.layout.sideBarVisible
                          ? "the side bar shows " + std::string(name(state.layout.sideView))
                          : std::string("the side bar is hidden"));
    }
    if (what == "panel-view") { // expect panel-view Console|Problems|Output|Profiler|none
        if (lower(arg) == "none")
            return report(!state.layout.panelVisible,
                          "the panel is showing " + std::string(name(state.layout.panelView)));
        const auto wanted = panelViewFromName(lower(arg));
        if (!wanted) {
            detail = "unknown panel '" + arg + "'";
            return -1;
        }
        return report(state.layout.panelVisible && state.layout.panelView == *wanted,
                      state.layout.panelVisible
                          ? "the panel shows " + std::string(name(state.layout.panelView))
                          : std::string("the panel is hidden"));
    }
    if (what == "inspector") // expect inspector visible|hidden
        return report(state.layout.inspectorVisible == (lower(arg) == "visible"),
                      state.layout.inspectorVisible ? "the inspector is visible"
                                                    : "the inspector is hidden");
    if (what == "split") { // expect split none|right|down
        const auto wanted = editorSplitFromName(lower(arg));
        if (!wanted) {
            detail = "unknown split '" + arg + "'";
            return -1;
        }
        return report(state.layout.split == *wanted,
                      "the editor area is split " + std::string(name(state.layout.split)));
    }
    if (what == "scene-tabs") { // expect scene-tabs N
        return report(state.sceneTabs.size() == static_cast<std::size_t>(number(arg).value_or(-1)),
                      std::to_string(state.sceneTabs.size()) + " scenes are open");
    }
    if (what == "scene-open") { // expect scene-open PATH
        return report(std::find(state.sceneTabs.begin(), state.sceneTabs.end(), arg) !=
                          state.sceneTabs.end(),
                      "'" + arg + "' is not open in a tab");
    }
    if (what == "scene-closed") {
        return report(std::find(state.sceneTabs.begin(), state.sceneTabs.end(), arg) ==
                          state.sceneTabs.end(),
                      "'" + arg + "' is still open in a tab");
    }
    if (what == "editor-focus") // expect editor-focus scene|game
        return report((state.editorFocus == EditorFocus::Game) == (lower(arg) == "game"),
                      "another editor tab is showing");
    if (what == "problems") { // expect problems N (after a validation)
        return report(state.problems.size() == static_cast<std::size_t>(number(arg).value_or(-1)),
                      std::to_string(state.problems.size()) + " problems are listed");
    }
    if (what == "problems-at-least")
        return report(state.problems.size() >= static_cast<std::size_t>(number(arg).value_or(1e9)),
                      std::to_string(state.problems.size()) + " problems are listed");
    if (what == "locked" || what == "unlocked") { // expect locked ENTITY
        const Entity *entity = findNamed(scene, arg);
        if (!entity) {
            detail = "there is no entity named '" + arg + "'";
            return 0;
        }
        return report(entity->locked() == (what == "locked"),
                      "'" + arg + "' is " + (entity->locked() ? "locked" : "not locked"));
    }
    if (what == "hidden" || what == "shown") { // expect hidden ENTITY : hidden in the editor
        const Entity *entity = findNamed(scene, arg);
        if (!entity) {
            detail = "there is no entity named '" + arg + "'";
            return 0;
        }
        return report(entity->editorHidden() == (what == "hidden"),
                      "'" + arg + "' is " + (entity->editorHidden() ? "hidden" : "shown"));
    }
    if (what == "layout") { // expect layout KEY VALUE [TOLERANCE] : a number or flag in the
                            // workbench layout
        if (!need(2))
            return -1;
        const Json layoutJson = state.layout.toJson();
        const Json &value = layoutJson.get(arg);
        if (value.isBool()) { // a flag: true/false (or on/off, 1/0)
            const std::string word = lower(w[3]);
            const bool wantedFlag = word == "true" || word == "on" || word == "1";
            return report(value.asBool() == wantedFlag,
                          arg + " is " + (value.asBool() ? "true" : "false"));
        }
        const auto wanted = number(w[3]);
        if (!value.isNumber() || !wanted) {
            detail = "'" + arg + "' is not a number in the layout";
            return -1;
        }
        const double tolerance = w.size() > 4 ? number(w[4]).value_or(0.5) : 0.5;
        return report(std::abs(value.asNumber() - *wanted) <= tolerance,
                      arg + " is " + std::to_string(value.asNumber()));
    }
    detail = "unknown expectation '" + what + "'";
    return -1;
}

bool EditorDriver::step(EditorApp &app) {
    if (!focused_) {
        // A test window under the dummy video driver never receives focus by itself.
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        event.window.windowID = SDL_GetWindowID(app.window());
        SDL_PushEvent(&event);
        focused_ = true;
        return true;
    }
    rememberRuntime(app);
    if (finished()) {
        app.quit();
        return false;
    }
    const Command &command = commands_[index_];
    const bool done = execute(app, command);
    if (done) {
        ++index_;
        phase_ = 0;
        waited_ = 0;
    }
    return true;
}

bool EditorDriver::execute(EditorApp &app, const Command &command) {
    const auto &w = command.words;
    const std::string &name = w[0];
    SdlInput input{app.window()};

    const auto aim = [&](const std::string &target, Point &out) {
        std::string problem;
        if (resolve(app, target, out, problem))
            return Aim::Ready;
        if (++waited_ > timeout_) {
            fail(app, command, problem);
            return Aim::Failed;
        }
        return Aim::Waiting;
    };
    const auto argument = [&](std::size_t index) -> std::string {
        return index < w.size() ? w[index] : std::string();
    };
    if (name == "wait") {
        const auto frames = number(argument(1));
        if (!frames) {
            fail(app, command, "wait needs a number of frames");
            return true;
        }
        return ++waited_ >= static_cast<int>(*frames);
    }
    if (name == "settle")
        return ++waited_ >= 4;
    if (name == "timeout") {
        timeout_ = static_cast<int>(number(argument(1)).value_or(240));
        return true;
    }
    if (name == "log") {
        std::string text;
        for (std::size_t i = 1; i < w.size(); ++i)
            text += (text.empty() ? "" : " ") + w[i];
        log(LogLevel::Info, "script", text);
        return true;
    }
    if (name == "quit") {
        app.quit();
        return true;
    }
    if (name == "import") { // import FILE... : as if the files were dropped on the window
        std::vector<std::filesystem::path> files;
        for (std::size_t i = 1; i < w.size(); ++i)
            files.emplace_back(argument(i));
        if (auto imported = app.state().importAssets(files); !imported)
            fail(app, command, imported.error());
        return true;
    }
    if (name == "closewindow") { // The window's close button: unsaved work must be asked about.
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        event.window.windowID = SDL_GetWindowID(app.window());
        SDL_PushEvent(&event);
        return true;
    }
    if (name ==
        "report") { // report ENTITY: logs where it is and whether it is active (for debugging)
        const Scene *scene = app.state().visibleScene();
        const Entity *entity = findNamed(scene, argument(1));
        if (!entity) {
            log(LogLevel::Info, "script", "report: no entity '" + argument(1) + "'");
        } else {
            const Vec2 at = entity->worldPosition();
            char buffer[160];
            std::snprintf(buffer, sizeof buffer, "report: '%s' at %.3f,%.3f active=%d",
                          entity->name().c_str(), static_cast<double>(at.x),
                          static_cast<double>(at.y), entity->activeInHierarchy() ? 1 : 0);
            log(LogLevel::Info, "script", buffer);
        }
        return true;
    }
    if (name == "expect") {
        std::string detail;
        const int result = check(app, command, detail);
        if (result > 0)
            return true;
        if (result < 0 || ++waited_ > timeout_) {
            fail(app, command, detail);
            return true;
        }
        return false;
    }
    if (name == "capture") {
        if (phase_ == 0) {
            app.requestCapture(argument(1));
            phase_ = 1;
            return false;
        }
        if (!app.captureError().empty())
            fail(app, command, app.captureError());
        return true;
    }
    if (name == "type") {
        std::string text;
        for (std::size_t i = 1; i < w.size(); ++i)
            text += (text.empty() ? "" : " ") + w[i];
        if (phase_ == 0)
            ImGui::GetIO().AddInputCharactersUTF8(text.c_str());
        return ++phase_ >= 2;
    }
    if (name == "key") {
        const auto combo = parseCombo(argument(1));
        if (!combo) {
            fail(app, command, "unknown key '" + argument(1) + "'");
            return true;
        }
        if (phase_ == 0) {
            input.modifiers(combo->modifiers, true);
            input.key(combo->key, true);
            phase_ = 1;
            return false;
        }
        input.key(combo->key, false);
        input.modifiers(combo->modifiers, false);
        return true;
    }
    if (name == "keydown" || name == "keyup" || name == "hold") {
        const auto scancode = scancodeFor(argument(1));
        if (!scancode) {
            fail(app, command, "unknown key '" + argument(1) + "'");
            return true;
        }
        if (name == "keydown") {
            input.key(*scancode, true);
            return true;
        }
        if (name == "keyup") {
            input.key(*scancode, false);
            return true;
        }
        const int frames = static_cast<int>(number(argument(2)).value_or(1));
        if (phase_ == 0) {
            input.key(*scancode, true);
            phase_ = 1;
            return false;
        }
        if (++waited_ < frames)
            return false;
        input.key(*scancode, false);
        return true;
    }

    // Everything below points the mouse at a target first.
    const KeyMods modifiers = modifiersFrom(w, name == "drag" ? 3 : name == "wheel" ? 3 : 2);
    if (name == "move" || name == "click" || name == "doubleclick" || name == "rightclick" ||
        name == "press" || name == "wheel") {
        if (phase_ == 0) {
            const Aim result = aim(argument(1), to_);
            if (result == Aim::Failed)
                return true;
            if (result == Aim::Waiting)
                return false;
            input.modifiers(modifiers, true);
            input.move(to_.x, to_.y);
            mouse_ = to_;
            phase_ = 1;
            return false;
        }
        const Uint8 button = name == "rightclick" ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
        if (name == "move")
            return true;
        if (name == "wheel") {
            input.wheel(static_cast<float>(number(argument(2)).value_or(1.0)), mouse_.x, mouse_.y);
            input.modifiers(modifiers, false);
            return true;
        }
        switch (phase_) {
        case 1:
            input.button(button, true, 1, mouse_.x, mouse_.y);
            phase_ = name == "press" ? 9 : 2;
            return false;
        case 2:
            input.button(button, false, 1, mouse_.x, mouse_.y);
            phase_ = name == "doubleclick" ? 3 : 5;
            return false;
        case 3:
            input.button(button, true, 2, mouse_.x, mouse_.y);
            phase_ = 4;
            return false;
        case 4:
            input.button(button, false, 2, mouse_.x, mouse_.y);
            phase_ = 5;
            return false;
        case 9: // press: stay down
            return true;
        default:
            input.modifiers(modifiers, false);
            return true;
        }
    }
    if (name == "release") {
        input.button(SDL_BUTTON_LEFT, false, 1, mouse_.x, mouse_.y);
        return true;
    }
    if (name == "drag") {
        constexpr int steps = 8;
        switch (phase_) {
        case 0: {
            const Aim result = aim(argument(1), from_);
            if (result == Aim::Failed)
                return true;
            if (result == Aim::Waiting)
                return false;
            input.modifiers(modifiers, true);
            input.move(from_.x, from_.y);
            mouse_ = from_;
            phase_ = 1;
            waited_ = 0;
            return false;
        }
        case 1:
            input.button(SDL_BUTTON_LEFT, true, 1, mouse_.x, mouse_.y);
            phase_ = 2;
            waited_ = 0;
            return false;
        case 2: {
            const Aim result = aim(argument(2), to_);
            if (result == Aim::Failed) {
                input.button(SDL_BUTTON_LEFT, false, 1, mouse_.x, mouse_.y);
                input.modifiers(modifiers, false);
                return true;
            }
            if (result == Aim::Waiting)
                return false;
            phase_ = 3;
            waited_ = 0;
            [[fallthrough]];
        }
        default: {
            if (phase_ < 3 + steps) {
                const float t = static_cast<float>(phase_ - 2) / static_cast<float>(steps);
                mouse_ = {from_.x + (to_.x - from_.x) * t, from_.y + (to_.y - from_.y) * t};
                input.move(mouse_.x, mouse_.y);
                ++phase_;
                return false;
            }
            if (phase_ == 3 + steps) {
                input.button(SDL_BUTTON_LEFT, false, 1, mouse_.x, mouse_.y);
                ++phase_;
                return false;
            }
            input.modifiers(modifiers, false);
            return true;
        }
        }
    }
    fail(app, command, "unknown command '" + name + "'");
    return true;
}
} // namespace yk::editor
