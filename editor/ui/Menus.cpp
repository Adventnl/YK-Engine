#include "ui/Panels.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>

namespace yk::editor::ui {
namespace {
// A menu entry that records where it is under "menu/<path>" for scripts and tests.
bool item(const std::string &path, const char *label, const char *shortcut = nullptr,
          bool enabled = true, bool selected = false) {
    const bool clicked =
        ImGui::MenuItem(label, shortcut ? shortcutText(shortcut) : nullptr, selected, enabled);
    markItem("menu/" + path);
    return clicked;
}

bool beginMenu(const std::string &path, const char *label, bool enabled = true) {
    const bool open = ImGui::BeginMenu(label, enabled);
    markItem("menu/" + path);
    return open;
}

void openDialog(EditorState &state, DialogKind kind, EntityId entity = {}) {
    showDialog(state, kind, entity);
}

void menuFile(EditorState &state) {
    const bool hasProject = state.project != nullptr;
    const bool editing = state.document != nullptr && !state.playing();
    if (item("File/New Project", "New Project..."))
        state.guarded([&state] { openDialog(state, DialogKind::NewProject); });
    if (item("File/Open Project", "Open Project...", "Ctrl+O"))
        state.guarded([&state] { openDialog(state, DialogKind::OpenProject); });
    if (beginMenu("File/Open Recent", "Open Recent", !state.recent.paths().empty())) {
        const std::vector<std::string> paths = state.recent.paths();
        for (const std::string &path : paths)
            if (ImGui::MenuItem(path.c_str()))
                state.guarded([&state, path] { state.openProject(path); });
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (item("File/New Scene", "New Scene...", "Ctrl+N", hasProject && !state.playing()))
        openDialog(state, DialogKind::NewScene);
    if (beginMenu("File/Open Scene", "Open Scene",
                  hasProject && !state.playing() && !state.scenePaths().empty())) {
        for (const std::string &path : state.scenePaths())
            if (item("File/Open Scene/" + path, path.c_str()))
                state.openScene(path);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (item("File/Save Scene", "Save Scene", "Ctrl+S", editing))
        state.saveScene();
    if (item("File/Save Scene As", "Save Scene As...", "Ctrl+Shift+S", editing))
        openDialog(state, DialogKind::SaveSceneAs);
    if (item("File/Save All", "Save All", "Ctrl+Alt+S",
             hasProject && !state.playing() && state.anyDirty()))
        state.saveAll();
    if (item("File/Close Scene", "Close Scene", "Ctrl+W", state.document != nullptr))
        state.closeScene(state.document->path());
    ImGui::Separator();
    if (item("File/Project Settings", "Project Settings...", nullptr, hasProject))
        openDialog(state, DialogKind::ProjectSettings);
    if (item("File/Close Project", "Close Project", nullptr, hasProject))
        state.guarded([&state] { state.closeProject(); });
    ImGui::Separator();
    if (item("File/Quit", "Quit", "Ctrl+Q"))
        state.requestQuit();
}

void menuEdit(EditorState &state) {
    const bool editing = state.document && !state.playing();
    EditorDocument *doc = state.document.get();
    const bool selection = editing && !doc->selection().empty();
    const std::string undoText = editing && doc->canUndo() ? "Undo " + doc->undoLabel() : "Undo";
    const std::string redoText = editing && doc->canRedo() ? "Redo " + doc->redoLabel() : "Redo";
    if (item("Edit/Undo", undoText.c_str(), "Ctrl+Z", editing && doc->canUndo()))
        doc->undo();
    if (item("Edit/Redo", redoText.c_str(), "Ctrl+Y", editing && doc->canRedo()))
        doc->redo();
    ImGui::Separator();
    if (item("Edit/Cut", "Cut", "Ctrl+X", selection))
        copySelection(state, true);
    if (item("Edit/Copy", "Copy", "Ctrl+C", selection))
        copySelection(state, false);
    if (item("Edit/Paste", "Paste", "Ctrl+V", editing))
        pasteClipboard(state);
    if (item("Edit/Duplicate", "Duplicate", "Ctrl+D", selection))
        doc->duplicateSelection();
    if (item("Edit/Delete", "Delete", "Del", selection))
        doc->deleteSelection();
    ImGui::Separator();
    if (item("Edit/Select All", "Select All", "Ctrl+A", editing))
        doc->selectAll();
    if (item("Edit/Deselect", "Deselect", nullptr, selection))
        doc->clearSelection();
    ImGui::Separator();
    if (item("Edit/Frame Selected", "Frame Selected", "F", selection))
        state.interaction.frameSelection();
    if (item("Edit/Frame All", "Frame All", "Home", state.document != nullptr))
        state.interaction.frameAll();
}

void menuView(EditorState &state) {
    WorkbenchLayout &layout = state.layout;
    struct SideEntry {
        SideView view;
        const char *label;
        const char *shortcut;
    };
    const SideEntry sides[] = {{SideView::Explorer, "Explorer", "Ctrl+Shift+E"},
                               {SideView::Scene, "Scene Hierarchy", "Ctrl+Shift+H"},
                               {SideView::Prefabs, "Prefabs", "Ctrl+Shift+K"},
                               {SideView::Components, "Components", "Ctrl+Shift+X"},
                               {SideView::Build, "Build and Run", "Ctrl+Shift+B"}};
    for (const SideEntry &entry : sides)
        if (item(std::string("View/") + entry.label, entry.label, entry.shortcut, true,
                 layout.sideBarVisible && layout.sideView == entry.view)) {
            layout.sideView = entry.view;
            layout.sideBarVisible = true;
        }
    ImGui::Separator();
    if (item("View/Sidebar", "Side Bar", "Ctrl+B", true, layout.sideBarVisible))
        layout.sideBarVisible = !layout.sideBarVisible;
    if (item("View/Inspector", "Inspector", "Ctrl+Alt+B", true, layout.inspectorVisible))
        layout.inspectorVisible = !layout.inspectorVisible;
    if (item("View/Panel", "Panel", "Ctrl+J", true, layout.panelVisible))
        layout.panelVisible = !layout.panelVisible;
    if (item("View/Maximize Panel", "Maximize Panel", nullptr, layout.panelVisible,
             layout.panelMaximized))
        layout.panelMaximized = !layout.panelMaximized;
    ImGui::Separator();
    struct PanelEntry {
        PanelView view;
        const char *label;
        const char *shortcut;
    };
    const PanelEntry panels[] = {{PanelView::Console, "Console", "Ctrl+Shift+Y"},
                                 {PanelView::Problems, "Problems", "Ctrl+Shift+M"},
                                 {PanelView::Output, "Build Output", "Ctrl+Shift+U"},
                                 {PanelView::Profiler, "Profiler", nullptr}};
    for (const PanelEntry &entry : panels)
        if (item(std::string("View/") + entry.label, entry.label, entry.shortcut, true,
                 layout.panelVisible && layout.panelView == entry.view)) {
            layout.panelView = entry.view;
            layout.panelVisible = true;
        }
    ImGui::Separator();
    if (beginMenu("View/Editor Layout", "Editor Layout")) {
        if (item("View/Editor Layout/Single", "Single", nullptr, true,
                 layout.split == EditorSplit::None))
            layout.split = EditorSplit::None;
        if (item("View/Editor Layout/Split Right", "Split Right", "Ctrl+\\", true,
                 layout.split == EditorSplit::Right))
            layout.split = EditorSplit::Right;
        if (item("View/Editor Layout/Split Down", "Split Down", nullptr, true,
                 layout.split == EditorSplit::Down))
            layout.split = EditorSplit::Down;
        ImGui::EndMenu();
    }
    if (beginMenu("View/Overlays", "Overlays")) {
        ViewOptions &view = state.view;
        const auto toggle = [&](const char *label, bool &value) {
            if (item(std::string("View/Overlays/") + label, label, nullptr, true, value))
                value = !value;
        };
        toggle("Grid", view.grid);
        toggle("Collider Outlines", view.colliders);
        toggle("Sprite Bounds", view.spriteBounds);
        toggle("Pivot Points", view.pivots);
        toggle("Links of Selection", view.links);
        toggle("All Links", view.allLinks);
        toggle("Entity Names", view.labels);
        toggle("Camera Frame", view.cameraFrame);
        ImGui::EndMenu();
    }
    if (item("View/Snap to Grid", "Snap to Grid", nullptr, true, state.interaction.snap.enabled))
        state.interaction.snap.enabled = !state.interaction.snap.enabled;
    ImGui::Separator();
    const bool hasView = state.interaction.bound();
    if (item("View/Zoom In", "Zoom In", "Ctrl+=", hasView))
        state.interaction.zoomStep(1);
    if (item("View/Zoom Out", "Zoom Out", "Ctrl+-", hasView))
        state.interaction.zoomStep(-1);
    if (item("View/Reset Zoom", "Reset Zoom to 100%", "Ctrl+0", hasView))
        state.interaction.zoomTo(100.0F);
    ImGui::Separator();
    if (item("View/Reset Layout", "Reset Layout"))
        state.layout = {};
}

void menuScene(EditorState &state) {
    const bool hasProject = state.project != nullptr;
    const bool editing = state.document != nullptr && !state.playing();
    if (item("Scene/New Scene", "New Scene...", nullptr, hasProject && !state.playing()))
        openDialog(state, DialogKind::NewScene);
    if (beginMenu("Scene/Open Scene", "Open Scene",
                  hasProject && !state.playing() && !state.scenePaths().empty())) {
        for (const std::string &path : state.scenePaths())
            if (item("Scene/Open Scene/" + path, path.c_str()))
                state.openScene(path);
        ImGui::EndMenu();
    }
    if (item("Scene/Save Scene", "Save Scene", "Ctrl+S", editing))
        state.saveScene();
    ImGui::Separator();
    if (item("Scene/Scene Settings", "Scene Settings", nullptr, editing)) {
        state.document->clearSelection();
        state.layout.inspectorVisible = true;
    }
    ImGui::Separator();
    if (item("Scene/Frame All", "Frame All", "Home", state.document != nullptr))
        state.interaction.frameAll();
    if (item("Scene/Play Scene", "Play Scene", "F5",
             state.document != nullptr && state.project != nullptr && !state.playing()))
        state.startPlay();
}

void menuEntity(EditorState &state) {
    const bool editing = state.document && !state.playing();
    const EntityId selected = editing ? state.document->primary() : EntityId{};
    if (beginMenu("Entity/Create", "Create", editing)) {
        createEntityMenu(state, {}, std::nullopt);
        ImGui::EndMenu();
    }
    if (beginMenu("Entity/Create Child", "Create Child of Selected", selected.value != 0)) {
        createEntityMenu(state, selected, std::nullopt);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (item("Entity/Save as Prefab", "Save as Prefab...", nullptr, selected.value != 0))
        openDialog(state, DialogKind::SavePrefab, selected);
    const bool instance = selected.value != 0 && state.document->prefabRootOf(selected).value != 0;
    if (beginMenu("Entity/Prefab", "Prefab", instance)) {
        prefabMenuItems(state, selected, "Entity/Prefab");
        ImGui::EndMenu();
    }
    const std::vector<std::string> prefabs = state.prefabPaths();
    if (beginMenu("Entity/Instantiate Prefab", "Instantiate Prefab", editing && !prefabs.empty())) {
        for (const std::string &path : prefabs)
            if (item("Entity/Instantiate Prefab/" + path, path.c_str()))
                state.instantiatePrefab(path, defaultSpawnPoint(state));
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (item("Entity/Duplicate", "Duplicate", "Ctrl+D", selected.value != 0))
        state.document->duplicateSelection();
    if (item("Entity/Delete", "Delete", "Del", selected.value != 0))
        state.document->deleteSelection();
    ImGui::Separator();
    const Entity *entity = selected.value != 0 ? state.document->scene().find(selected) : nullptr;
    if (item("Entity/Active", entity && entity->active() ? "Deactivate" : "Activate", nullptr,
             entity != nullptr))
        state.document->setEntityActive(selected, !entity->active());
    if (item("Entity/Hide", entity && entity->editorHidden() ? "Show in Editor" : "Hide in Editor",
             nullptr, entity != nullptr))
        state.document->setEditorHidden(state.document->selectionRoots(), !entity->editorHidden());
    if (item("Entity/Lock", entity && entity->locked() ? "Unlock" : "Lock", nullptr,
             entity != nullptr))
        state.document->setLocked(state.document->selectionRoots(), !entity->locked());
    ImGui::Separator();
    if (item("Entity/Move Up", "Move Up", "Alt+Up",
             entity != nullptr && state.document->canMoveAmongSiblings(selected, -1)))
        state.document->moveAmongSiblings(selected, -1);
    if (item("Entity/Move Down", "Move Down", "Alt+Down",
             entity != nullptr && state.document->canMoveAmongSiblings(selected, 1)))
        state.document->moveAmongSiblings(selected, 1);
}

void menuComponent(EditorState &state) {
    const bool editing = state.document && !state.playing();
    const EntityId selected = editing ? state.document->primary() : EntityId{};
    if (beginMenu("Component/Add Component", "Add Component", selected.value != 0)) {
        addComponentMenu(state, selected);
        ImGui::EndMenu();
    }
    const Entity *entity = selected.value != 0 ? state.document->scene().find(selected) : nullptr;
    if (beginMenu("Component/Remove Component", "Remove Component",
                  entity && !entity->components().empty())) {
        for (std::size_t i = 0; i < entity->components().size(); ++i) {
            const Component &component = *entity->components()[i];
            const std::string blocker = entity->removalBlocker(component);
            if (item("Component/Remove Component/" + component.type().name,
                     component.type().name.c_str(), nullptr, blocker.empty()))
                if (auto removed = state.document->removeComponent(selected, i); !removed)
                    log(LogLevel::Warning, "editor", removed.error());
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (item("Component/Browse Components", "Browse Components", "Ctrl+Shift+X")) {
        state.layout.sideView = SideView::Components;
        state.layout.sideBarVisible = true;
    }
}

void menuBuild(EditorState &state) {
    const bool hasProject = state.project != nullptr;
    if (item("Build/Validate Project", "Validate Project", nullptr, hasProject))
        state.validateProject();
    if (item("Build/Export Game", "Export Game...", nullptr, hasProject && !state.playing()))
        openDialog(state, DialogKind::Export);
    if (item("Build/Run in Player", "Run in Player", "Ctrl+F5", hasProject && !state.playing()))
        runInPlayer(state);
    if (item("Build/Stop Player", "Stop Player", nullptr, state.playerProcessRunning()))
        state.stopPlayerProcesses();
    ImGui::Separator();
    if (item("Build/Build and Run", "Build and Run Panel", "Ctrl+Shift+B")) {
        state.layout.sideView = SideView::Build;
        state.layout.sideBarVisible = true;
    }
    if (item("Build/Build Output", "Build Output", "Ctrl+Shift+U")) {
        state.layout.panelView = PanelView::Output;
        state.layout.panelVisible = true;
    }
}

void menuDebug(EditorState &state) {
    const bool can = state.document != nullptr && state.project != nullptr;
    if (item("Debug/Play", "Play", "F5", can && !state.playing(), state.playing()))
        state.startPlay();
    if (item("Debug/Pause", "Pause", "F6", state.playing(),
             state.playing() && state.play->paused()))
        state.togglePause();
    if (item("Debug/Step", "Step", "F10", state.playing() && state.play->paused()))
        state.stepPlay(InputFrame{});
    if (item("Debug/Stop", "Stop", "Shift+F5", state.playing()))
        state.stopPlay();
    if (item("Debug/Restart", "Restart", "Ctrl+Shift+F5", state.playing()))
        state.restartPlay();
    ImGui::Separator();
    const auto toggle = [&](const char *label, bool &value) {
        if (item(std::string("Debug/") + label, label, nullptr, true, value))
            value = !value;
    };
    toggle("Physics Shapes", state.view.gamePhysicsDebug);
    toggle("Game Collider Outlines", state.view.gameColliders);
    toggle("Game Statistics", state.view.gameStats);
}

void menuHelp(EditorState &state) {
    if (item("Help/Keyboard Shortcuts", "Keyboard Shortcuts"))
        openDialog(state, DialogKind::Shortcuts);
    std::error_code error;
    const bool hasLogs = !state.options.logDirectory.empty() &&
                         std::filesystem::is_directory(state.options.logDirectory, error);
    if (item("Help/Open Logs Folder", "Open Logs Folder", nullptr, hasLogs)) {
        const std::string url = toFileUrl(state.options.logDirectory);
        SDL_OpenURL(url.c_str());
    }
    if (item("Help/About", "About YK Engine"))
        openDialog(state, DialogKind::About);
}
} // namespace

Status runInPlayer(EditorState &state) {
    return state.startPlayerProcess();
}

void drawMenus(EditorState &state) {
    struct Entry {
        const char *id;
        void (*content)(EditorState &);
    };
    static const Entry entries[] = {
        {"File", menuFile},   {"Edit", menuEdit},     {"View", menuView},
        {"Scene", menuScene}, {"Entity", menuEntity}, {"Component", menuComponent},
        {"Build", menuBuild}, {"Debug", menuDebug},   {"Help", menuHelp}};
    for (const Entry &entry : entries) {
        const bool open = ImGui::BeginMenu(entry.id);
        markItem(std::string("menu/") + entry.id);
        if (open) {
            {
                const PopupLook look; // Must end before EndMenu: the popup checks its style stack.
                entry.content(state);
            }
            ImGui::EndMenu();
        }
    }
}
} // namespace yk::editor::ui
