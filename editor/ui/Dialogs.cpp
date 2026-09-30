#include "ui/Panels.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cfloat>

#ifndef YK_VERSION
#define YK_VERSION "dev"
#endif

namespace yk::editor::ui {
namespace {
std::filesystem::path startingFolder() {
    if (const char *documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS))
        return documents;
    if (const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME))
        return home;
    return std::filesystem::current_path();
}

std::string defaultPrefabName(const EditorState &state, EntityId entity) {
    std::string name = "prefab";
    if (state.document)
        if (const Entity *found = state.document->scene().find(entity))
            name = found->name();
    std::string slug;
    for (const char c : name) {
        const auto uc = static_cast<unsigned char>(c);
        slug += std::isalnum(uc) ? static_cast<char>(std::tolower(uc)) : '_';
    }
    return "prefabs/" + slug;
}

// Folder list with an editable path, shared by the new-project and open-project dialogs.
// Returns the folder the user double-clicked (to open directly), if any.
std::optional<std::filesystem::path> folderBrowser(DirectoryBrowser &browser,
                                                   const std::string &idPrefix) {
    std::optional<std::filesystem::path> opened;
    const float button = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button -
                            ImGui::GetStyle().ItemSpacing.x);
    inputText("##path", browser.pathText);
    const bool enterPressed = enterPressedInField();
    markItem(idPrefix + "/path");
    if (enterPressed)
        browser.go(browser.pathText);
    ImGui::SameLine();
    if (ImGui::Button("..", {button, button}) && browser.current.has_parent_path())
        browser.go(browser.current.parent_path());
    markItem(idPrefix + "/up");
    tooltip("Parent folder");
    if (!browser.error.empty())
        ImGui::TextColored(imColor(palette::error), "%s", browser.error.c_str());
    ImGui::BeginChild("##folders", {0.0F, 190.0F}, ImGuiChildFlags_Borders);
    if (browser.hasProject)
        ImGui::TextColored(imColor(palette::good), "This folder is a project.");
    for (const DirectoryBrowser::Entry &entry : browser.folders) {
        ImGui::PushID(entry.name.c_str());
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const bool clicked =
            ImGui::Selectable("##folder", false, ImGuiSelectableFlags_AllowDoubleClick);
        const bool doubleClicked =
            ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        markItem(idPrefix + "/folder/" + entry.name);
        ImGui::SetCursorScreenPos(origin);
        iconLabel(Icon::Folder, entry.name.c_str(),
                  entry.isProject ? packed(palette::good) : IM_COL32(200, 190, 130, 255));
        if (entry.isProject) {
            ImGui::SameLine();
            ImGui::TextColored(imColor(palette::good), "project");
        }
        if (doubleClicked) {
            if (entry.isProject)
                opened = browser.current / entry.name;
            else
                browser.go(browser.current / entry.name);
        } else if (clicked) {
            browser.go(browser.current / entry.name);
        }
        ImGui::PopID();
    }
    if (browser.folders.empty())
        ImGui::TextDisabled("(no sub-folders)");
    ImGui::EndChild();
    return opened;
}

void closeDialog(EditorState &state) {
    state.dialog = {};
    ImGui::CloseCurrentPopup();
}

bool validFileName(const std::string &name) {
    return !name.empty() && name.find_first_of("/\\:*?\"<>|") == std::string::npos && name != "." &&
           name != "..";
}

void newProjectDialog(EditorState &state) {
    DialogState &dialog = state.dialog;
    ImGui::TextUnformatted("Project name");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    inputText("##name", dialog.text);
    markItem("dialog/NewProject/name");
    ImGui::Spacing();
    ImGui::TextUnformatted("Create it in this folder");
    folderBrowser(dialog.browser, "dialog/NewProject");
    const std::filesystem::path target = dialog.browser.current / dialog.text;
    ImGui::TextColored(imColor(palette::dim), "%s", target.string().c_str());
    if (!dialog.error.empty())
        ImGui::TextColored(imColor(palette::error), "%s", dialog.error.c_str());
    ImGui::Spacing();
    const bool ready = validFileName(dialog.text) && !dialog.browser.current.empty();
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button("Create Project", {140.0F, 0.0F})) {
        if (auto created = state.createProject(target, dialog.text); created)
            closeDialog(state);
        else
            dialog.error = created.error();
    }
    markItem("dialog/NewProject/create");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {100.0F, 0.0F}))
        closeDialog(state);
    markItem("dialog/NewProject/cancel");
}

void openProjectDialog(EditorState &state) {
    DialogState &dialog = state.dialog;
    ImGui::TextUnformatted("Choose a project folder (one that holds project.ykproj)");
    if (const auto direct = folderBrowser(dialog.browser, "dialog/OpenProject")) {
        if (auto opened = state.openProject(*direct); opened)
            closeDialog(state);
        else
            dialog.error = opened.error();
        return;
    }
    if (!dialog.error.empty())
        ImGui::TextColored(imColor(palette::error), "%s", dialog.error.c_str());
    ImGui::Spacing();
    ImGui::BeginDisabled(!dialog.browser.hasProject);
    if (ImGui::Button("Open Project", {140.0F, 0.0F})) {
        if (auto opened = state.openProject(dialog.browser.current); opened)
            closeDialog(state);
        else
            dialog.error = opened.error();
    }
    markItem("dialog/OpenProject/open");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {100.0F, 0.0F}))
        closeDialog(state);
    markItem("dialog/OpenProject/cancel");
}

// A text field plus OK/Cancel. `apply` returns an error message or "" on success.
void nameDialog(EditorState &state, const char *kind, const char *prompt, const char *okLabel,
                const std::function<std::string(const std::string &)> &apply) {
    DialogState &dialog = state.dialog;
    ImGui::TextUnformatted(prompt);
    ImGui::SetNextItemWidth(360.0F);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    inputText("##name", dialog.text);
    const bool entered = enterPressedInField();
    markItem(std::string("dialog/") + kind + "/name");
    if (!dialog.error.empty())
        ImGui::TextColored(imColor(palette::error), "%s", dialog.error.c_str());
    ImGui::Spacing();
    const bool ok = ImGui::Button(okLabel, {120.0F, 0.0F});
    markItem(std::string("dialog/") + kind + "/ok");
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel", {100.0F, 0.0F});
    markItem(std::string("dialog/") + kind + "/cancel");
    if (ok || entered) {
        const std::string error = apply(dialog.text);
        if (error.empty())
            closeDialog(state);
        else
            dialog.error = error;
    } else if (cancel) {
        closeDialog(state);
    }
}

// Rename or move a file or folder of the project: a path field that says at once why a path will
// not do, and what the change will rewrite.
void moveAssetDialog(EditorState &state) {
    DialogState &dialog = state.dialog;
    if (!state.project) {
        closeDialog(state);
        return;
    }
    ImGui::TextUnformatted("New name or path (inside the project)");
    ImGui::SetNextItemWidth(460.0F);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    inputText("##name", dialog.text);
    const bool entered = enterPressedInField();
    markItem("dialog/MoveAsset/name");
    const bool changed = dialog.text != dialog.assetPath;
    std::string problem;
    if (changed)
        if (const auto checked = state.project->checkMove(dialog.assetPath, dialog.text); !checked)
            problem = checked.error();
    ImGui::PushTextWrapPos(560.0F);
    ImGui::TextColored(imColor(palette::dim), "%s", dialog.message.c_str());
    if (!problem.empty())
        ImGui::TextColored(imColor(palette::error), "%s", problem.c_str());
    if (!dialog.error.empty())
        ImGui::TextColored(imColor(palette::error), "%s", dialog.error.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const bool ready = changed && problem.empty();
    ImGui::BeginDisabled(!ready);
    const bool ok = ImGui::Button("Rename", {120.0F, 0.0F});
    markItem("dialog/MoveAsset/ok");
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel", {100.0F, 0.0F});
    markItem("dialog/MoveAsset/cancel");
    if ((ok || entered) && ready) {
        if (auto moved = state.moveAsset(dialog.assetPath, dialog.text); moved)
            closeDialog(state);
        else
            dialog.error = moved.error();
    } else if (cancel) {
        closeDialog(state);
    }
}

// Picks the player program that goes with the chosen target: the one found next to the editor, or
// the path typed into the field.
std::filesystem::path chosenPlayer(EditorState &state) {
    if (!state.dialog.exportPlayer.empty())
        return state.dialog.exportPlayer;
    if (const auto found = state.playerFor(state.dialog.exportTarget))
        return *found;
    return {};
}

void exportDialog(EditorState &state) {
    DialogState &dialog = state.dialog;
    if (!state.project) {
        closeDialog(state);
        return;
    }
    const Project &project = state.project->project();
    ImGui::TextWrapped("Packages the game as a folder that runs on its own: the player program for "
                       "the chosen system, this project's data and the notices the player's "
                       "libraries require.");
    ImGui::Spacing();
    ImGui::PushFont(fonts().semibold, 13.0F);
    ImGui::TextUnformatted("Target system");
    ImGui::PopFont();
    for (const BuildTarget target : allBuildTargets) {
        ImGui::PushID(name(target));
        if (ImGui::RadioButton(displayName(target), dialog.exportTarget == target)) {
            dialog.exportTarget = target;
            dialog.exportPlayer.clear();
            dialog.error.clear();
        }
        markItem(std::string("dialog/Export/target/") + name(target));
        ImGui::SameLine(150.0F);
        if (const auto player = state.playerFor(target)) {
            ImGui::TextColored(imColor(palette::good), "player found");
            tooltip(player->string());
        } else {
            ImGui::TextColored(imColor(palette::dim), "no player program yet");
            tooltip("Build yk_player for " + std::string(displayName(target)) +
                    " and choose it below, or put it in templates/" + name(target) +
                    "/ next to the editor (docs/BUILDING.md).");
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Player program");
    ImGui::SetNextItemWidth(-FLT_MIN);
    const auto found = state.playerFor(dialog.exportTarget);
    inputText("##player", dialog.exportPlayer, 0,
              found ? found->string().c_str() : "Path of yk_player built for this system");
    markItem("dialog/Export/player");
    ImGui::Spacing();
    ImGui::TextUnformatted("Export into this folder");
    folderBrowser(dialog.browser, "dialog/Export");
    ImGui::Checkbox("Also write a .zip archive", &dialog.exportZip);
    markItem("dialog/Export/zip");
    ImGui::SameLine(0.0F, 24.0F);
    ImGui::Checkbox("Replace an earlier export", &dialog.exportReplace);
    markItem("dialog/Export/replace");
    tooltip("Only a folder that an earlier export created is ever replaced.");
    if (dialog.exportTarget == BuildTarget::MacOS) {
        // Signing and disk images use Apple's own tools, so they only exist on a Mac; elsewhere the
        // choices stay visible and switched off, with the reason.
        const bool onMac = hostTarget() == BuildTarget::MacOS;
        ImGui::BeginDisabled(!onMac);
        ImGui::Checkbox("Sign the app", &dialog.exportSign);
        markItem("dialog/Export/sign");
        tooltip(onMac ? "Seals the app so it verifies. Without an identity below it is signed ad "
                        "hoc: it runs on this Mac, other Macs ask before opening it."
                      : "Needs a Mac: signing uses Apple's codesign.");
        ImGui::SameLine(0.0F, 24.0F);
        ImGui::Checkbox("Also write a .dmg disk image", &dialog.exportDmg);
        markItem("dialog/Export/dmg");
        tooltip(onMac ? "A disk image with the app and an Applications link, ready to hand out."
                      : "Needs a Mac: disk images are made with Apple's hdiutil.");
        ImGui::BeginDisabled(!dialog.exportSign);
        ImGui::SetNextItemWidth(-FLT_MIN);
        inputText(
            "##identity", dialog.exportIdentity, 0,
            "Signing identity (empty: ad hoc), e.g. Developer ID Application: Studio (TEAMID)");
        markItem("dialog/Export/identity");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (!onMac)
            ImGui::TextColored(imColor(palette::dim),
                               "Export here, then sign and create the .dmg on a Mac.");
    }
    if (!dialog.browser.current.empty())
        ImGui::TextColored(imColor(palette::dim), "%s",
                           (dialog.browser.current / exportFolderName(project, dialog.exportTarget))
                               .string()
                               .c_str());
    if (!dialog.error.empty()) {
        ImGui::PushTextWrapPos(560.0F);
        ImGui::TextColored(imColor(palette::error), "%s", dialog.error.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    const std::filesystem::path player = chosenPlayer(state);
    ImGui::BeginDisabled(dialog.browser.current.empty() || player.empty());
    if (ImGui::Button("Export", {120.0F, 0.0F})) {
        ExportOptions options;
        options.target = dialog.exportTarget;
        options.destination = dialog.browser.current;
        options.player = player;
        options.archive = dialog.exportZip;
        options.overwrite = dialog.exportReplace;
        if (dialog.exportTarget == BuildTarget::MacOS && hostTarget() == BuildTarget::MacOS) {
            options.dmg = dialog.exportDmg;
            if (dialog.exportSign)
                options.codesign = dialog.exportIdentity.empty() ? "-" : dialog.exportIdentity;
        }
        if (auto exported = state.exportGame(options); exported) {
            const ExportReport &report = exported.value();
            std::string text = "The game is in:\n" + report.output.string();
            if (!report.archive.empty())
                text += "\n\nArchive:\n" + report.archive.string();
            if (!report.diskImage.empty())
                text += "\n\nDisk image:\n" + report.diskImage.string();
            for (const std::string &warning : report.warnings)
                text += "\n\nNote: " + warning;
            const std::filesystem::path reveal = report.output.parent_path();
            state.message("Game exported", text);
            state.dialog.revealPath = reveal;
        } else {
            dialog.error = exported.error();
        }
    }
    markItem("dialog/Export/export");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {100.0F, 0.0F}))
        closeDialog(state);
    markItem("dialog/Export/cancel");
}

// A question with an accept button; the accepted action is the dialog's continuation.
void confirmDialog(EditorState &state) {
    DialogState &dialog = state.dialog;
    ImGui::PushTextWrapPos(460.0F);
    ImGui::TextWrapped("%s", dialog.message.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const std::string label = dialog.confirmLabel;
    const bool accepted = ImGui::Button(label.c_str(), {110.0F, 0.0F});
    markItem("dialog/Confirm/ok");
    ImGui::SameLine();
    const bool cancelled = ImGui::Button("Cancel", {100.0F, 0.0F});
    markItem("dialog/Confirm/cancel");
    if (accepted) {
        const auto continuation = dialog.continuation;
        closeDialog(state);
        if (continuation)
            continuation();
    } else if (cancelled) {
        closeDialog(state);
    }
}

void unsavedDialog(EditorState &state) {
    DialogState &dialog = state.dialog;
    ImGui::TextWrapped("%s", dialog.message.c_str());
    ImGui::TextUnformatted("Save them before continuing?");
    ImGui::Spacing();
    const auto resume = [&] {
        const auto continuation = dialog.continuation;
        closeDialog(state);
        if (continuation)
            continuation();
    };
    if (ImGui::Button("Save", {110.0F, 0.0F})) {
        if (auto saved = state.saveScenes(dialog.unsavedScenes); saved) {
            resume();
        } else {
            dialog.error = saved.error();
        }
    }
    markItem("dialog/Unsaved/save");
    ImGui::SameLine();
    if (ImGui::Button("Don't Save", {110.0F, 0.0F}))
        resume();
    markItem("dialog/Unsaved/discard");
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {100.0F, 0.0F}))
        closeDialog(state);
    markItem("dialog/Unsaved/cancel");
    if (!dialog.error.empty())
        ImGui::TextColored(imColor(palette::error), "%s", dialog.error.c_str());
}

void settingsDialog(EditorState &state) {
    DialogState &dialog = state.dialog;
    if (!dialog.draft || !state.project) {
        closeDialog(state);
        return;
    }
    Project &draft = *dialog.draft;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginTabBar("##settings")) {
        const bool tabGeneral = ImGui::BeginTabItem("General");
        markItem("dialog/Settings/tab/General");
        if (tabGeneral) {
            ImGui::Spacing();
            ImGui::TextUnformatted("Project name");
            ImGui::SetNextItemWidth(-FLT_MIN);
            inputText("##projectname", draft.name);
            markItem("dialog/Settings/name");
            ImGui::TextUnformatted("Window title");
            ImGui::SetNextItemWidth(-FLT_MIN);
            inputText("##windowtitle", draft.window.title);
            ImGui::TextUnformatted("Window size");
            ImGui::SetNextItemWidth(220.0F);
            int size[2] = {draft.window.width, draft.window.height};
            if (ImGui::DragInt2("##windowsize", size, 4.0F, 160, 8192)) {
                draft.window.width = std::clamp(size[0], 160, 8192);
                draft.window.height = std::clamp(size[1], 120, 8192);
            }
            ImGui::TextUnformatted("Start scene");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##startscene",
                                  draft.startScene.empty() ? "(none)" : draft.startScene.c_str())) {
                for (const std::string &path : state.scenePaths())
                    if (ImGui::Selectable(path.c_str(), path == draft.startScene))
                        draft.startScene = path;
                ImGui::EndCombo();
            }
            ImGui::EndTabItem();
        }
        const bool tabCollisionLayers = ImGui::BeginTabItem("Collision Layers");
        markItem("dialog/Settings/tab/Collision Layers");
        if (tabCollisionLayers) {
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Which layers touch each other. Solid collisions and trigger overlaps both "
                "follow this table.");
            LayerConfig &layers = draft.layers;
            const std::size_t count = layers.size();
            if (ImGui::BeginTable("##matrix", static_cast<int>(count) + 1,
                                  ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_ScrollX,
                                  {0.0F, 260.0F})) {
                ImGui::TableSetupColumn("##layer", ImGuiTableColumnFlags_WidthFixed, 110.0F);
                for (std::size_t j = 0; j < count; ++j)
                    ImGui::TableSetupColumn(layers.names[j].c_str(),
                                            ImGuiTableColumnFlags_WidthFixed, 26.0F);
                ImGui::TableSetupScrollFreeze(1, 1);
                ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("");
                for (std::size_t j = 0; j < count; ++j) {
                    ImGui::TableSetColumnIndex(static_cast<int>(j) + 1);
                    ImGui::TextUnformatted(layers.names[j].substr(0, 3).c_str());
                    tooltip(layers.names[j]);
                }
                for (std::size_t i = 0; i < count; ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(layers.names[i].c_str());
                    for (std::size_t j = 0; j < count; ++j) {
                        ImGui::TableSetColumnIndex(static_cast<int>(j) + 1);
                        if (j > i) {
                            ImGui::TextDisabled("-");
                            continue;
                        }
                        ImGui::PushID(static_cast<int>(i * 64 + j));
                        bool interacts = layers.interacts(i, j);
                        if (ImGui::Checkbox("##cell", &interacts))
                            layers.setInteraction(i, j, interacts);
                        markItem("dialog/Settings/layer/" + layers.names[i] + "/" +
                                 layers.names[j]);
                        tooltip(layers.names[i] + " <-> " + layers.names[j]);
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
            static std::string newLayer;
            ImGui::SetNextItemWidth(180.0F);
            inputText("##newlayer", newLayer, 0, "New layer name");
            markItem("dialog/Settings/newlayer");
            ImGui::SameLine();
            if (ImGui::Button("Add Layer") && !newLayer.empty()) {
                if (auto added = layers.addLayer(newLayer); added)
                    newLayer.clear();
                else
                    dialog.error = added.error();
            }
            markItem("dialog/Settings/addlayer");
            ImGui::EndTabItem();
        }
        const bool tabInput = ImGui::BeginTabItem("Input");
        markItem("dialog/Settings/tab/Input");
        if (tabInput) {
            settingsInputTab(draft, dialog.error);
            ImGui::EndTabItem();
        }
        const bool tabRendering = ImGui::BeginTabItem("Rendering");
        markItem("dialog/Settings/tab/Rendering");
        if (tabRendering) {
            settingsRenderingTab(draft);
            ImGui::EndTabItem();
        }
        const bool tabBuild = ImGui::BeginTabItem("Build");
        markItem("dialog/Settings/tab/Build");
        if (tabBuild) {
            settingsBuildTab(draft);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (!dialog.error.empty())
        ImGui::TextColored(imColor(palette::error), "%s", dialog.error.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Save", {110.0F, 0.0F})) {
        if (auto valid = draft.layers.validate(); !valid) {
            dialog.error = valid.error();
        } else if (auto inputValid = draft.input.validate(); !inputValid) {
            dialog.error = "Input: " + inputValid.error();
        } else {
            state.project->project() = draft;
            if (auto saved = state.project->save(); saved) {
                log(LogLevel::Info, "editor", "Saved project settings");
                closeDialog(state);
            } else {
                dialog.error = saved.error();
            }
        }
    }
    markItem("dialog/Settings/save");
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {100.0F, 0.0F}))
        closeDialog(state);
    markItem("dialog/Settings/cancel");
}

void validationDialog(EditorState &state) {
    DialogState &dialog = state.dialog;
    if (dialog.issues.empty())
        ImGui::TextColored(imColor(palette::good), "No problems found in any scene or prefab.");
    ImGui::BeginChild("##issues", {560.0F, 240.0F}, ImGuiChildFlags_Borders);
    for (const ProjectIssue &issue : dialog.issues) {
        const bool isError = issue.severity == ProjectIssue::Severity::Error;
        iconLabel(isError ? Icon::Error : Icon::Warning, "",
                  packed(isError ? palette::error : palette::warning));
        ImGui::SameLine(0.0F, 0.0F);
        ImGui::TextColored(imColor(palette::dim), "%s", issue.path.c_str());
        ImGui::SameLine();
        ImGui::TextWrapped("%s", issue.message.c_str());
    }
    ImGui::EndChild();
    if (ImGui::Button("Close", {100.0F, 0.0F}))
        closeDialog(state);
    markItem("dialog/Validation/close");
}

void shortcutsDialog(EditorState &state) {
    struct Row {
        const char *keys;
        const char *action;
    };
    static constexpr Row rows[] = {
        {"Ctrl+N / O / S / Shift+S / Alt+S", "New scene, open project, save, save as, save all"},
        {"Ctrl+W / Ctrl+Tab", "Close the scene, next scene tab"},
        {"Ctrl+Z / Ctrl+Y", "Undo / Redo"},
        {"Ctrl+C / X / V", "Copy / Cut / Paste entities"},
        {"Ctrl+D, Delete, Ctrl+A", "Duplicate, delete the selection, select all"},
        {"W, R, E", "Move, Resize, Rotate tool"},
        {"F, Home", "Focus the selection, fit everything you can edit"},
        {"Ctrl+= / Ctrl+- / Ctrl+0", "Zoom in / zoom out / back to 100%"},
        {"Arrow keys", "Nudge the selection (Shift for bigger steps)"},
        {"Alt+Up / Alt+Down", "Move the selected entity among its siblings"},
        {"Ctrl (while dragging)", "Toggle grid snapping"},
        {"Shift (while dragging)", "Constrain to an axis, or keep proportions when resizing"},
        {"Alt (resizing)", "Resize from the center"},
        {"Alt+click", "Select the next entity underneath"},
        {"Middle or right drag", "Pan the scene view (also Space + left drag)"},
        {"Mouse wheel", "Zoom at the cursor (a sideways wheel or swipe pans)"},
        {"F5 / Shift+F5", "Play / Stop"},
        {"F6 / F10 / Ctrl+Shift+F5", "Pause / Step one tick while paused / Restart"},
        {"Ctrl+F5", "Run the project in the standalone player"},
        {"Ctrl+B / Ctrl+Alt+B / Ctrl+J", "Toggle the side bar / inspector / panel"},
        {"Ctrl+Shift+E H K X B", "Explorer, Scene, Prefabs, Components, Build view"},
        {"Ctrl+Shift+Y M U", "Console, Problems, Build Output panel"},
        {"Ctrl+\\", "Split the editor area"},
        {"F2", "Rename an entity (Hierarchy) or a file (Explorer)"},
        {"Delete (in the Explorer)", "Delete the selected file or folder, after asking"},
        {"Escape", "Cancel a drag or a pending pick"},
    };
    if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        for (const Row &row : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(imColor(palette::selection), "%s", shortcutText(row.keys));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(row.action);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ImGui::Button("Close", {100.0F, 0.0F}))
        closeDialog(state);
    markItem("dialog/Shortcuts/close");
}

void aboutDialog(EditorState &state) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5F);
    ImGui::TextUnformatted("YK Engine Editor");
    ImGui::PopFont();
    ImGui::TextColored(imColor(palette::dim), "Version %s", YK_VERSION);
    ImGui::Spacing();
    ImGui::TextWrapped(
        "A 2D game engine and editor. Built with SDL3 (zlib), Box2D (MIT), Dear ImGui (MIT) and "
        "stb_image (public domain). See THIRD_PARTY.md.");
    ImGui::Spacing();
    if (ImGui::Button("Close", {100.0F, 0.0F}))
        closeDialog(state);
    markItem("dialog/About/close");
}

void messageDialog(EditorState &state) {
    ImGui::PushTextWrapPos(460.0F);
    ImGui::TextWrapped("%s", state.dialog.message.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const bool ok = ImGui::Button("OK", {100.0F, 0.0F}) || ImGui::IsKeyPressed(ImGuiKey_Enter);
    markItem("dialog/Message/ok");
    if (!state.dialog.revealPath.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Show Folder", {120.0F, 0.0F})) {
            // The file manager opens the folder; nothing is done with the result.
            const std::string url = toFileUrl(state.dialog.revealPath);
            SDL_OpenURL(url.c_str());
        }
        markItem("dialog/Message/reveal");
    }
    if (ok)
        closeDialog(state);
}
} // namespace

void showDialog(EditorState &state, DialogKind kind, EntityId entity) {
    state.dialog = {};
    state.dialog.kind = kind;
    state.dialog.needsOpen = true;
    state.dialog.entity = entity;
    switch (kind) {
    case DialogKind::NewProject:
        state.dialog.title = "New Project";
        state.dialog.text = "My Game";
        state.dialog.browser.go(startingFolder());
        break;
    case DialogKind::OpenProject: {
        state.dialog.title = "Open Project";
        std::filesystem::path start = startingFolder();
        if (state.project && state.project->project().root.has_parent_path())
            start = state.project->project().root.parent_path();
        else if (!state.recent.paths().empty())
            start = std::filesystem::path(state.recent.paths().front()).parent_path();
        state.dialog.browser.go(start);
        break;
    }
    case DialogKind::NewScene:
        state.dialog.title = "New Scene";
        state.dialog.text = "scenes/new_scene";
        break;
    case DialogKind::SaveSceneAs:
        state.dialog.title = "Save Scene As";
        state.dialog.text =
            state.document && !state.document->path().empty() ? state.document->path() : "scenes/";
        break;
    case DialogKind::SavePrefab:
        state.dialog.title = "Save as Prefab";
        state.dialog.text = defaultPrefabName(state, entity);
        break;
    case DialogKind::ProjectSettings:
        state.dialog.title = "Project Settings";
        if (state.project)
            state.dialog.draft = state.project->project();
        break;
    case DialogKind::Export: {
        state.dialog.title = "Export Game";
        state.dialog.exportTarget = hostTarget();
        state.dialog.exportZip = true;
        std::filesystem::path start = startingFolder();
        if (state.project && state.project->project().root.has_parent_path())
            start = state.project->project().root.parent_path();
        state.dialog.browser.go(start);
        break;
    }
    case DialogKind::Validation:
        state.dialog.title = "Project Validation";
        break;
    case DialogKind::About:
        state.dialog.title = "About";
        break;
    case DialogKind::Shortcuts:
        state.dialog.title = "Keyboard Shortcuts";
        break;
    default:
        break;
    }
}

namespace {
// Dialogs are cards too: rounded, outlined, with a title bar of the raised color and roomy padding.
struct DialogLook {
    DialogLook() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, dp(metrics::cardRadius + 2.0F));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(16.0F), dp(12.0F)});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {dp(8.0F), dp(6.0F)});
        ImGui::PushStyleColor(ImGuiCol_TitleBg, imColor(vs::raisedBg));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, imColor(vs::raisedBg));
        ImGui::PushStyleColor(ImGuiCol_Border, imColor(vs::menuBorder));
    }
    ~DialogLook() {
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(4);
    }
    DialogLook(const DialogLook &) = delete;
    DialogLook &operator=(const DialogLook &) = delete;
};
} // namespace

void drawDialogs(EditorState &state) {
    DialogState &dialog = state.dialog;
    if (dialog.kind == DialogKind::None)
        return;
    const DialogLook look;
    const std::string popupId = dialog.title + "###yk_dialog";
    if (dialog.needsOpen) {
        ImGui::OpenPopup(popupId.c_str());
        dialog.needsOpen = false;
    }
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->GetCenter().x, viewport->GetCenter().y * 0.9F},
                            ImGuiCond_Appearing, {0.5F, 0.5F});
    const bool wide = dialog.kind == DialogKind::ProjectSettings ||
                      dialog.kind == DialogKind::Validation ||
                      dialog.kind == DialogKind::NewProject ||
                      dialog.kind == DialogKind::OpenProject || dialog.kind == DialogKind::Export;
    ImGui::SetNextWindowSize(
        {wide ? (dialog.kind == DialogKind::ProjectSettings ? 760.0F : 600.0F) : 0.0F, 0.0F});
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;
    if (!wide)
        flags |= ImGuiWindowFlags_AlwaysAutoResize;
    bool open = true;
    if (!ImGui::BeginPopupModal(popupId.c_str(), &open, flags)) {
        if (!open || !ImGui::IsPopupOpen(popupId.c_str()))
            dialog = {}; // Closed by its X button.
        return;
    }
    markWindow("dialog");
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && dialog.kind != DialogKind::Message) {
        closeDialog(state);
        ImGui::EndPopup();
        return;
    }
    switch (dialog.kind) {
    case DialogKind::NewProject:
        newProjectDialog(state);
        break;
    case DialogKind::OpenProject:
        openProjectDialog(state);
        break;
    case DialogKind::NewScene:
        nameDialog(state, "NewScene", "Scene file (inside the project)", "Create",
                   [&](const std::string &text) {
                       const auto created = state.newScene(text);
                       return created ? std::string{} : created.error();
                   });
        break;
    case DialogKind::SaveSceneAs:
        nameDialog(state, "SaveSceneAs", "Scene file (inside the project)", "Save",
                   [&](const std::string &text) {
                       const auto saved = state.saveSceneAs(text);
                       return saved ? std::string{} : saved.error();
                   });
        break;
    case DialogKind::SavePrefab:
        nameDialog(state, "SavePrefab", "Prefab file (inside the project)", "Save",
                   [&](const std::string &text) {
                       const auto saved = state.savePrefab(dialog.entity, text);
                       return saved ? std::string{} : saved.error();
                   });
        break;
    case DialogKind::Unsaved:
        unsavedDialog(state);
        break;
    case DialogKind::ProjectSettings:
        settingsDialog(state);
        break;
    case DialogKind::Export:
        exportDialog(state);
        break;
    case DialogKind::Validation:
        validationDialog(state);
        break;
    case DialogKind::About:
        aboutDialog(state);
        break;
    case DialogKind::Shortcuts:
        shortcutsDialog(state);
        break;
    case DialogKind::Message:
        messageDialog(state);
        break;
    case DialogKind::Confirm:
        confirmDialog(state);
        break;
    case DialogKind::MoveAsset:
        moveAssetDialog(state);
        break;
    case DialogKind::None:
        break;
    }
    ImGui::EndPopup();
}
} // namespace yk::editor::ui
