#include "ui/Panels.hpp"
#include <algorithm>
#include <cfloat>
#include <cstdio>

// The tabs of the Project Settings dialog that edit a project's data rather than the window:
// input action sets, texture defaults and the export settings. Dialogs.cpp owns the dialog frame,
// the Save and Cancel buttons and the draft these functions edit.
namespace yk::editor::ui {
namespace {
struct KeyName {
    ImGuiKey imgui;
    Key key;
};
// The keys a binding can name, in ImGui's terms, for "press the key you want".
const std::vector<KeyName> &keyTable() {
    static const std::vector<KeyName> table = [] {
        std::vector<KeyName> made;
        for (int i = 0; i < 26; ++i)
            made.push_back({static_cast<ImGuiKey>(ImGuiKey_A + i),
                            static_cast<Key>(static_cast<int>(Key::A) + i)});
        for (int i = 0; i < 10; ++i)
            made.push_back({static_cast<ImGuiKey>(ImGuiKey_0 + i),
                            static_cast<Key>(static_cast<int>(Key::Num0) + i)});
        for (int i = 0; i < 12; ++i)
            made.push_back({static_cast<ImGuiKey>(ImGuiKey_F1 + i),
                            static_cast<Key>(static_cast<int>(Key::F1) + i)});
        const KeyName others[] = {{ImGuiKey_LeftArrow, Key::Left},
                                  {ImGuiKey_RightArrow, Key::Right},
                                  {ImGuiKey_UpArrow, Key::Up},
                                  {ImGuiKey_DownArrow, Key::Down},
                                  {ImGuiKey_Space, Key::Space},
                                  {ImGuiKey_Enter, Key::Enter},
                                  {ImGuiKey_Escape, Key::Escape},
                                  {ImGuiKey_Tab, Key::Tab},
                                  {ImGuiKey_Backspace, Key::Backspace},
                                  {ImGuiKey_Delete, Key::Delete},
                                  {ImGuiKey_Insert, Key::Insert},
                                  {ImGuiKey_Home, Key::Home},
                                  {ImGuiKey_End, Key::End},
                                  {ImGuiKey_PageUp, Key::PageUp},
                                  {ImGuiKey_PageDown, Key::PageDown},
                                  {ImGuiKey_LeftShift, Key::LeftShift},
                                  {ImGuiKey_RightShift, Key::RightShift},
                                  {ImGuiKey_LeftCtrl, Key::LeftCtrl},
                                  {ImGuiKey_RightCtrl, Key::RightCtrl},
                                  {ImGuiKey_LeftAlt, Key::LeftAlt},
                                  {ImGuiKey_RightAlt, Key::RightAlt},
                                  {ImGuiKey_Minus, Key::Minus},
                                  {ImGuiKey_Equal, Key::Equals},
                                  {ImGuiKey_Comma, Key::Comma},
                                  {ImGuiKey_Period, Key::Period},
                                  {ImGuiKey_Slash, Key::Slash},
                                  {ImGuiKey_Semicolon, Key::Semicolon},
                                  {ImGuiKey_Apostrophe, Key::Apostrophe},
                                  {ImGuiKey_LeftBracket, Key::LeftBracket},
                                  {ImGuiKey_RightBracket, Key::RightBracket},
                                  {ImGuiKey_GraveAccent, Key::Grave},
                                  {ImGuiKey_Backslash, Key::Backslash}};
        made.insert(made.end(), std::begin(others), std::end(others));
        return made;
    }();
    return table;
}

std::optional<Key> pressedKey() {
    for (const KeyName &entry : keyTable())
        if (ImGui::IsKeyPressed(entry.imgui, false))
            return entry.key;
    return std::nullopt;
}

// "Key:A" reads as "A", "Pad:South" as "Pad South", "PadAxis:LeftX-" as "Left stick X-".
std::string bindingLabel(const InputBinding &binding) {
    switch (binding.kind) {
    case InputBinding::Kind::Key:
        return std::string(keyName(binding.key));
    case InputBinding::Kind::GamepadButton:
        return "Pad " + std::string(gamepadButtonName(binding.button));
    case InputBinding::Kind::GamepadAxis:
        return "Axis " + std::string(gamepadAxisName(binding.axis)) +
               (binding.axisPositive ? "+" : "-");
    }
    return "?";
}

struct InputEditor {
    int set{0};
    int listening{-1}; // Action index that is waiting for a key press.
    std::string newSet, newAction;
};
InputEditor editor;

// One action row: the name field, a chip per binding (click the x to remove it) and a "+" that
// opens the picker.
void actionRow(InputMap &map, ActionSet &set, std::size_t index, std::string &error) {
    InputAction &action = set.actions[index];
    ImGui::PushID(static_cast<int>(index));
    ImGui::AlignTextToFramePadding();
    ImGui::SetNextItemWidth(130.0F);
    inputText("##action", action.name);
    markItem("dialog/Settings/input/action/" + set.name + "/" + std::to_string(index));
    ImGui::SameLine();
    int removeBinding = -1;
    for (std::size_t b = 0; b < action.bindings.size(); ++b) {
        ImGui::PushID(static_cast<int>(b));
        const std::string text = bindingLabel(action.bindings[b]);
        const ImVec2 size = ImGui::CalcTextSize(text.c_str());
        const float chipWidth = size.x + 34.0F;
        if (ImGui::GetContentRegionAvail().x < chipWidth + 30.0F)
            ImGui::NewLine();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##chip", {chipWidth, ImGui::GetFrameHeight()});
        const bool hovered = ImGui::IsItemHovered();
        markItem("dialog/Settings/input/binding/" + set.name + "/" + action.name + "/" + text);
        const bool removeHovered = ImGui::IsMouseHoveringRect(
            {at.x + chipWidth - 22.0F, at.y}, {at.x + chipWidth, at.y + ImGui::GetFrameHeight()});
        ImDrawList &list = *ImGui::GetWindowDrawList();
        list.AddRectFilled(at, {at.x + chipWidth, at.y + ImGui::GetFrameHeight()},
                           packed(hovered ? vs::secondaryHover : vs::secondaryBg), 3.0F);
        list.AddText({at.x + 8.0F, at.y + (ImGui::GetFrameHeight() - size.y) * 0.5F},
                     packed(vs::text), text.c_str());
        drawIcon(list, Icon::Cross,
                 {at.x + chipWidth - 12.0F, at.y + ImGui::GetFrameHeight() * 0.5F}, 11.0F,
                 packed(removeHovered ? vs::error : vs::textDim));
        if (hovered)
            tooltip("Click to remove " + text);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            removeBinding = static_cast<int>(b);
        ImGui::PopID();
        ImGui::SameLine(0.0F, 4.0F);
    }
    if (removeBinding >= 0)
        action.bindings.erase(action.bindings.begin() + removeBinding);

    // The "+" button: a popup with three ways to name an input.
    const std::string popup = "##addbinding" + std::to_string(index);
    if (iconButton(("dialog/Settings/input/add/" + set.name + "/" + std::to_string(index)).c_str(),
                   Icon::Plus, false, "Add a key or gamepad input", 0, ImGui::GetFrameHeight())) {
        ImGui::OpenPopup(popup.c_str());
        editor.listening = -1;
    }
    if (ImGui::BeginPopup(popup.c_str())) {
        {
            const PopupLook look; // Ends before EndPopup, which pops the window's own state.
            const auto add = [&](const InputBinding &binding) {
                if (std::find(action.bindings.begin(), action.bindings.end(), binding) ==
                    action.bindings.end())
                    action.bindings.push_back(binding);
                ImGui::CloseCurrentPopup();
            };
            if (editor.listening == static_cast<int>(index)) {
                ImGui::TextColored(imColor(vs::warning), "Press the key to use...");
                markItem("dialog/Settings/input/listening");
                if (const auto key = pressedKey()) {
                    add(InputBinding::fromKey(*key));
                    editor.listening = -1;
                }
                if (ImGui::Button("Cancel"))
                    editor.listening = -1;
            } else {
                // Stays open: the next frame says "Press the key to use...".
                if (ImGui::Selectable("Listen for a key...", false,
                                      ImGuiSelectableFlags_NoAutoClosePopups)) {
                    editor.listening = static_cast<int>(index);
                }
                markItem("dialog/Settings/input/listen");
                const bool keyMenu = ImGui::BeginMenu("Key");
                markItem("dialog/Settings/input/menu/Key");
                if (keyMenu) {
                    for (std::size_t k = 0; k < keyCount; ++k) {
                        const auto key = static_cast<Key>(k);
                        if (ImGui::MenuItem(std::string(keyName(key)).c_str()))
                            add(InputBinding::fromKey(key));
                    }
                    ImGui::EndMenu();
                }
                const bool buttonMenu = ImGui::BeginMenu("Gamepad button");
                markItem("dialog/Settings/input/menu/GamepadButton");
                if (buttonMenu) {
                    for (std::size_t k = 0; k < gamepadButtonCount; ++k) {
                        const auto button = static_cast<GamepadButton>(k);
                        if (ImGui::MenuItem(std::string(gamepadButtonName(button)).c_str()))
                            add(InputBinding::fromButton(button));
                        markItem("dialog/Settings/input/pad/" +
                                 std::string(gamepadButtonName(button)));
                    }
                    ImGui::EndMenu();
                }
                const bool axisMenu = ImGui::BeginMenu("Gamepad axis");
                markItem("dialog/Settings/input/menu/GamepadAxis");
                if (axisMenu) {
                    for (std::size_t k = 0; k < gamepadAxisCount; ++k) {
                        const auto axis = static_cast<GamepadAxis>(k);
                        for (const bool positive : {false, true}) {
                            const std::string text =
                                std::string(gamepadAxisName(axis)) + (positive ? "+" : "-");
                            if (ImGui::MenuItem(text.c_str()))
                                add(InputBinding::fromAxis(axis, positive));
                        }
                    }
                    ImGui::EndMenu();
                }
            }
        }
        ImGui::EndPopup();
    } else if (editor.listening == static_cast<int>(index)) {
        editor.listening = -1;
    }
    ImGui::SameLine(0.0F, 8.0F);
    if (iconButton(
            ("dialog/Settings/input/remove/" + set.name + "/" + std::to_string(index)).c_str(),
            Icon::Trash, false, "Remove this action", 0, ImGui::GetFrameHeight())) {
        set.actions.erase(set.actions.begin() + static_cast<std::ptrdiff_t>(index));
        (void)map;
        (void)error;
    }
    ImGui::PopID();
}
} // namespace

void settingsInputTab(Project &draft, std::string &error) {
    InputMap &map = draft.input;
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Gameplay asks for actions by name (\"Jump\"); this table says which keys and gamepad "
        "inputs "
        "drive them. Each action set is one controller: a person at the keyboard, a gamepad, a "
        "second player.");
    ImGui::Spacing();
    editor.set = std::clamp(editor.set, 0, std::max(0, static_cast<int>(map.sets.size()) - 1));
    constexpr float height = 300.0F;

    // The list of sets, with the field that adds another one under it.
    ImGui::BeginChild("##sets", {160.0F, height}, ImGuiChildFlags_Borders);
    for (std::size_t i = 0; i < map.sets.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(map.sets[i].name.c_str(), editor.set == static_cast<int>(i)))
            editor.set = static_cast<int>(i);
        markItem("dialog/Settings/input/set/" + map.sets[i].name);
        ImGui::PopID();
    }
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeight() * 2.0F - 14.0F);
    ImGui::Separator();
    ImGui::SetNextItemWidth(-FLT_MIN);
    inputText("##newset", editor.newSet, 0, "New set name");
    markItem("dialog/Settings/input/newset");
    if (ImGui::Button("Add Set", {-FLT_MIN, 0.0F}) && !editor.newSet.empty()) {
        if (map.findSet(editor.newSet)) {
            error = "There is already an action set called '" + editor.newSet + "'";
        } else {
            ActionSet set;
            set.name = editor.newSet;
            map.sets.push_back(set);
            editor.set = static_cast<int>(map.sets.size()) - 1;
            editor.newSet.clear();
            error.clear();
        }
    }
    markItem("dialog/Settings/input/addset");
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##setEditor", {0.0F, height}, ImGuiChildFlags_None);
    if (map.sets.empty()) {
        ImGui::TextDisabled("No action sets. Add one to start.");
        ImGui::EndChild();
        return;
    }
    ActionSet &set = map.sets[static_cast<std::size_t>(editor.set)];
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0F);
    inputText("##setname", set.name);
    markItem("dialog/Settings/input/setname");
    ImGui::SameLine();
    ImGui::TextUnformatted("Gamepad");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0F);
    const std::string slotText = set.gamepad < 0 ? "none" : std::to_string(set.gamepad + 1);
    if (ImGui::BeginCombo("##pad", slotText.c_str())) {
        if (ImGui::Selectable("none", set.gamepad < 0))
            set.gamepad = -1;
        for (int slot = 0; slot < static_cast<int>(maxGamepads); ++slot)
            if (ImGui::Selectable(std::to_string(slot + 1).c_str(), set.gamepad == slot))
                set.gamepad = slot;
        ImGui::EndCombo();
    }
    markItem("dialog/Settings/input/gamepad");
    tooltip("Which connected gamepad this set also listens to (1 is the first one plugged in).");
    ImGui::SameLine();
    const bool removeSet = ImGui::Button("Remove Set");
    markItem("dialog/Settings/input/removeset");
    if (removeSet) {
        map.sets.erase(map.sets.begin() + editor.set);
        editor.set = std::max(0, editor.set - 1);
        ImGui::EndChild();
        return;
    }
    ImGui::Separator();
    ImGui::BeginChild("##actions", {0.0F, height - ImGui::GetFrameHeight() * 3.0F - 24.0F},
                      ImGuiChildFlags_None);
    for (std::size_t i = 0; i < set.actions.size(); ++i) {
        const std::size_t before = set.actions.size();
        actionRow(map, set, i, error);
        if (set.actions.size() != before)
            break; // A row removed itself; draw the rest next frame.
    }
    ImGui::EndChild();
    ImGui::SetNextItemWidth(160.0F);
    inputText("##newaction", editor.newAction, 0, "New action name");
    markItem("dialog/Settings/input/newaction");
    ImGui::SameLine();
    if (ImGui::Button("Add Action") && !editor.newAction.empty()) {
        if (set.find(editor.newAction)) {
            error = "'" + set.name + "' already has an action called '" + editor.newAction + "'";
        } else {
            set.actions.push_back({editor.newAction, {}});
            editor.newAction.clear();
            error.clear();
        }
    }
    markItem("dialog/Settings/input/addaction");
    ImGui::EndChild();
}

void settingsRenderingTab(Project &draft) {
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Defaults for textures that have no import settings of their own (see the Explorer's "
        "asset inspector for per-texture overrides).");
    ImGui::Spacing();
    ImGui::TextUnformatted("Pixels per world unit");
    ImGui::SetNextItemWidth(160.0F);
    float ppu = draft.textures.pixelsPerUnit;
    if (ImGui::DragFloat("##ppu", &ppu, 1.0F, 1.0F, 4096.0F, "%.0f"))
        draft.textures.pixelsPerUnit = std::clamp(ppu, 1.0F, 4096.0F);
    markItem("dialog/Settings/rendering/ppu");
    tooltip("How many texture pixels make one world unit (one meter of physics). A 64 pixel tile "
            "with a value of 64 is one unit wide.");
    ImGui::TextUnformatted("Texture filtering");
    ImGui::SetNextItemWidth(160.0F);
    const bool nearest = draft.textures.filter == TextureFilter::Nearest;
    if (ImGui::BeginCombo("##filter", nearest ? "Nearest (crisp pixels)" : "Linear (smooth)")) {
        if (ImGui::Selectable("Linear (smooth)", !nearest))
            draft.textures.filter = TextureFilter::Linear;
        if (ImGui::Selectable("Nearest (crisp pixels)", nearest))
            draft.textures.filter = TextureFilter::Nearest;
        ImGui::EndCombo();
    }
    markItem("dialog/Settings/rendering/filter");
}

void settingsBuildTab(Project &draft) {
    ImGui::Spacing();
    ImGui::TextWrapped("How the game is named and what goes into it when it is exported "
                       "(Build > Export Game, or `yk export`).");
    ImGui::Spacing();
    BuildSettings &build = draft.build;
    const auto field = [&](const char *title, const char *id, std::string &value, const char *hint,
                           const char *help) {
        ImGui::TextUnformatted(title);
        ImGui::SetNextItemWidth(-FLT_MIN);
        inputText((std::string("##") + id).c_str(), value, 0, hint);
        markItem(std::string("dialog/Settings/build/") + id);
        tooltip(help);
    };
    field("Product name", "product", build.productName, draft.name.c_str(),
          "Shown to players: the folder, the macOS bundle and the README. Empty uses the project "
          "name.");
    field("Executable name", "executable", build.executable, "derived from the product name",
          "The game program's file name, without .exe. Letters, digits, '-' and '_' only.");
    field("Version", "version", build.version, "1.0.0",
          "Recorded in the README and the macOS bundle.");
    field("macOS bundle identifier", "identifier", build.identifier, "com.yk.<product>",
          "Reverse-domain name that identifies the app on macOS, e.g. com.mystudio.mygame.");
    field("App icon", "icon", build.icon, "assets/icon.png",
          "A square PNG of the game, 1024 x 1024 pixels is best (at least 512). It becomes the "
          "macOS app's icon and the window icon on Windows and Linux. Empty: the default icon.");
    field("Copyright", "copyright", build.copyright, "(c) 2026 My Studio",
          "Shown in the macOS app's information. Optional.");
    ImGui::TextUnformatted("Leave out of the game");
    static std::string excludeText;
    // One path per line; tools/, docs/, scripts and notes never ship anyway.
    excludeText.clear();
    for (const std::string &path : build.exclude)
        excludeText += path + "\n";
    static bool editing = false;
    static std::string buffer;
    if (!editing)
        buffer = excludeText;
    if (inputTextMultiline("##exclude", buffer, {-FLT_MIN, 90.0F})) {
        build.exclude.clear();
        std::size_t start = 0;
        while (start <= buffer.size()) {
            const std::size_t end = buffer.find('\n', start);
            std::string line =
                buffer.substr(start, end == std::string::npos ? std::string::npos : end - start);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!line.empty())
                build.exclude.push_back(line);
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
    }
    editing = ImGui::IsItemActive();
    markItem("dialog/Settings/build/exclude");
    tooltip("Project-relative files or folders, one per line (for example art/source). "
            "Development folders such as tools/ and docs/ are never included.");
}
} // namespace yk::editor::ui
