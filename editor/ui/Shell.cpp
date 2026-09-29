#include "imgui_internal.h"
#include "ui/Panels.hpp"
#include "yk/core/Log.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace yk::editor::ui {
namespace {
constexpr ImGuiID dockspaceId = 0x594B4453; // "YKDS"

void openDialog(EditorState &state, DialogKind kind, EntityId entity = {}) {
    showDialog(state, kind, entity);
}

// The demo game that ships with the engine: beside the editor in an install or a build tree, or in
// the source checkout.
std::optional<std::filesystem::path> findSampleProject() {
    std::vector<std::filesystem::path> candidates;
    if (const char *base = SDL_GetBasePath()) {
        const std::filesystem::path here(base);
        candidates.push_back(here / "YK-DemoGame");
        candidates.push_back(here / ".." / "YK-DemoGame");
        candidates.push_back(here / ".." / ".." / "YK-DemoGame");
        candidates.push_back(here / ".." / "share" / "yk-engine" / "YK-DemoGame");
    }
    candidates.push_back("YK-DemoGame");
    candidates.push_back("../YK-DemoGame");
#ifdef YK_SOURCE_DIR
    candidates.push_back(std::filesystem::path(YK_SOURCE_DIR) / "YK-DemoGame");
#endif
    for (const auto &candidate : candidates) {
        std::error_code error;
        if (std::filesystem::exists(candidate / Project::fileName, error))
            return std::filesystem::weakly_canonical(candidate, error);
    }
    return std::nullopt;
}

void copySelection(EditorState &state, bool cut) {
    if (!state.document || state.playing() || state.document->selection().empty())
        return;
    ImGui::SetClipboardText(state.document->copySelection().dump().c_str());
    log(LogLevel::Info, "editor",
        (cut ? "Cut " : "Copied ") + std::to_string(state.document->selectionRoots().size()) +
            " entities");
    if (cut)
        state.document->deleteSelection();
}

void pasteClipboard(EditorState &state) {
    if (!state.document || state.playing())
        return;
    const char *text = ImGui::GetClipboardText();
    if (!text || !*text)
        return;
    auto parsed = Json::parse(text);
    if (!parsed) {
        log(LogLevel::Warning, "editor", "The clipboard does not hold copied entities");
        return;
    }
    std::optional<Vec2> center;
    // Pasting while the pointer is over the scene view drops the copies under it.
    if (state.sceneView.hovered && state.interaction.bound()) {
        const ImGuiIO &io = ImGui::GetIO();
        const Vec2 local = vec(io.MousePos) - state.sceneView.origin;
        center = snapTo(state.interaction.toWorld(local),
                        state.interaction.snap.enabled ? state.interaction.snap.grid : 0.0F);
    }
    if (auto pasted = state.document->paste(parsed.value(), {}, center); !pasted)
        log(LogLevel::Warning, "editor", pasted.error());
}

void menuEdit(EditorState &state) {
    const bool editing = state.document && !state.playing();
    EditorDocument *doc = state.document.get();
    const bool selection = editing && !doc->selection().empty();
    const std::string undoText = editing && doc->canUndo() ? "Undo " + doc->undoLabel() : "Undo";
    const std::string redoText = editing && doc->canRedo() ? "Redo " + doc->redoLabel() : "Redo";
    bool clicked = ImGui::MenuItem(undoText.c_str(), "Ctrl+Z", false, editing && doc->canUndo());
    markItem("menu/Edit/Undo");
    if (clicked)
        doc->undo();
    clicked = ImGui::MenuItem(redoText.c_str(), "Ctrl+Y", false, editing && doc->canRedo());
    markItem("menu/Edit/Redo");
    if (clicked)
        doc->redo();
    ImGui::Separator();
    if (ImGui::MenuItem("Cut", "Ctrl+X", false, selection))
        copySelection(state, true);
    clicked = ImGui::MenuItem("Copy", "Ctrl+C", false, selection);
    markItem("menu/Edit/Copy");
    if (clicked)
        copySelection(state, false);
    clicked = ImGui::MenuItem("Paste", "Ctrl+V", false, editing);
    markItem("menu/Edit/Paste");
    if (clicked)
        pasteClipboard(state);
    clicked = ImGui::MenuItem("Duplicate", "Ctrl+D", false, selection);
    markItem("menu/Edit/Duplicate");
    if (clicked)
        doc->duplicateSelection();
    clicked = ImGui::MenuItem("Delete", "Del", false, selection);
    markItem("menu/Edit/Delete");
    if (clicked)
        doc->deleteSelection();
    ImGui::Separator();
    if (ImGui::MenuItem("Select All", "Ctrl+A", false, editing))
        doc->selectAll();
    if (ImGui::MenuItem("Deselect", nullptr, false, selection))
        doc->clearSelection();
    ImGui::Separator();
    if (ImGui::MenuItem("Frame Selected", "F", false, selection))
        state.interaction.frameSelection();
    if (ImGui::MenuItem("Frame All", "Home", false, state.document != nullptr))
        state.interaction.frameAll();
}

void menuEntity(EditorState &state) {
    const bool editing = state.document && !state.playing();
    const EntityId selected = editing ? state.document->primary() : EntityId{};
    if (ImGui::BeginMenu("Create", editing)) {
        createEntityMenu(state, {}, std::nullopt);
        ImGui::EndMenu();
    }
    markItem("menu/Entity/Create");
    if (ImGui::BeginMenu("Create Child of Selected", selected.value != 0)) {
        createEntityMenu(state, selected, std::nullopt);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::BeginMenu("Add Component", selected.value != 0)) {
        addComponentMenu(state, selected);
        ImGui::EndMenu();
    }
    markItem("menu/Entity/Add Component");
    ImGui::Separator();
    const bool savePrefab =
        ImGui::MenuItem("Save as Prefab...", nullptr, false, selected.value != 0);
    markItem("menu/Entity/Save as Prefab");
    if (savePrefab)
        openDialog(state, DialogKind::SavePrefab, selected);
    if (ImGui::MenuItem("Toggle Active", nullptr, false, selected.value != 0)) {
        const Entity *entity = state.document->scene().find(selected);
        state.document->setEntityActive(selected, !entity->active());
    }
}

void menuView(EditorState &state) {
    ImGui::MenuItem("Hierarchy", nullptr, &state.showHierarchy);
    ImGui::MenuItem("Inspector", nullptr, &state.showInspector);
    ImGui::MenuItem("Scene", nullptr, &state.showScene);
    ImGui::MenuItem("Game", nullptr, &state.showGame);
    ImGui::MenuItem("Assets", nullptr, &state.showAssets);
    ImGui::MenuItem("Console", nullptr, &state.showConsole);
    ImGui::Separator();
    ImGui::MenuItem("Grid", nullptr, &state.view.grid);
    ImGui::MenuItem("Collider Outlines", nullptr, &state.view.colliders);
    ImGui::MenuItem("Links of Selection", nullptr, &state.view.links);
    ImGui::MenuItem("All Links", nullptr, &state.view.allLinks);
    ImGui::MenuItem("Entity Names", nullptr, &state.view.labels);
    ImGui::MenuItem("Camera Frame", nullptr, &state.view.cameraFrame);
    ImGui::Separator();
    ImGui::MenuItem("Snap to Grid", nullptr, &state.interaction.snap.enabled);
    if (ImGui::MenuItem("Reset Layout"))
        state.resetLayout = true;
}

void menuPlay(EditorState &state) {
    const bool can = state.document != nullptr && state.project != nullptr;
    bool clicked = ImGui::MenuItem("Play", "F5", state.playing(), can && !state.playing());
    markItem("menu/Play/Play");
    if (clicked)
        state.startPlay();
    if (ImGui::MenuItem("Pause", "F6", state.playing() && state.play->paused(), state.playing()))
        state.togglePause();
    if (ImGui::MenuItem("Step", "F10", false, state.playing() && state.play->paused()))
        state.stepPlay(InputFrame{});
    clicked = ImGui::MenuItem("Stop", "Shift+F5", false, state.playing());
    markItem("menu/Play/Stop");
    if (clicked)
        state.stopPlay();
    if (ImGui::MenuItem("Restart", nullptr, false, state.playing()))
        state.restartPlay();
}

void mainMenu(EditorState &state) {
    if (!ImGui::BeginMainMenuBar())
        return;
    const bool hasProject = state.project != nullptr;
    const bool editing = state.document != nullptr && !state.playing();
    bool open = ImGui::BeginMenu("File");
    markItem("menu/File");
    if (open) {
        bool clicked = ImGui::MenuItem("New Project...");
        markItem("menu/File/New Project");
        if (clicked)
            state.guarded([&state] { openDialog(state, DialogKind::NewProject); });
        clicked = ImGui::MenuItem("Open Project...", "Ctrl+O");
        markItem("menu/File/Open Project");
        if (clicked)
            state.guarded([&state] { openDialog(state, DialogKind::OpenProject); });
        if (ImGui::BeginMenu("Recent Projects", !state.recent.paths().empty())) {
            const std::vector<std::string> paths = state.recent.paths();
            for (const std::string &path : paths)
                if (ImGui::MenuItem(path.c_str()))
                    state.guarded([&state, path] { state.openProject(path); });
            ImGui::EndMenu();
        }
        ImGui::Separator();
        clicked = ImGui::MenuItem("New Scene...", "Ctrl+N", false, hasProject && !state.playing());
        markItem("menu/File/New Scene");
        if (clicked)
            state.guarded([&state] { openDialog(state, DialogKind::NewScene); });
        if (ImGui::BeginMenu("Open Scene",
                             hasProject && !state.playing() && !state.scenePaths().empty())) {
            for (const std::string &path : state.scenePaths()) {
                clicked = ImGui::MenuItem(path.c_str());
                markItem("menu/File/Open Scene/" + path);
                if (clicked)
                    state.guarded([&state, path] { state.openScene(path); });
            }
            ImGui::EndMenu();
        }
        markItem("menu/File/Open Scene");
        clicked = ImGui::MenuItem("Save Scene", "Ctrl+S", false, editing);
        markItem("menu/File/Save Scene");
        if (clicked)
            state.saveScene();
        clicked = ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S", false, editing);
        markItem("menu/File/Save Scene As");
        if (clicked)
            openDialog(state, DialogKind::SaveSceneAs);
        ImGui::Separator();
        clicked = ImGui::MenuItem("Project Settings...", nullptr, false, hasProject);
        markItem("menu/File/Project Settings");
        if (clicked)
            openDialog(state, DialogKind::ProjectSettings);
        clicked = ImGui::MenuItem("Validate Project", nullptr, false, hasProject);
        markItem("menu/File/Validate Project");
        if (clicked)
            state.validateProject();
        clicked = ImGui::MenuItem("Export Game...", nullptr, false, hasProject && !state.playing());
        markItem("menu/File/Export Game");
        if (clicked)
            openDialog(state, DialogKind::Export);
        clicked = ImGui::MenuItem("Close Project", nullptr, false, hasProject);
        markItem("menu/File/Close Project");
        if (clicked)
            state.guarded([&state] { state.closeProject(); });
        ImGui::Separator();
        clicked = ImGui::MenuItem("Quit", "Ctrl+Q");
        markItem("menu/File/Quit");
        if (clicked)
            state.requestQuit();
        ImGui::EndMenu();
    }
    open = ImGui::BeginMenu("Edit");
    markItem("menu/Edit");
    if (open) {
        menuEdit(state);
        ImGui::EndMenu();
    }
    open = ImGui::BeginMenu("Entity");
    markItem("menu/Entity");
    if (open) {
        menuEntity(state);
        ImGui::EndMenu();
    }
    open = ImGui::BeginMenu("View");
    markItem("menu/View");
    if (open) {
        menuView(state);
        ImGui::EndMenu();
    }
    open = ImGui::BeginMenu("Play");
    markItem("menu/Play");
    if (open) {
        menuPlay(state);
        ImGui::EndMenu();
    }
    open = ImGui::BeginMenu("Help");
    markItem("menu/Help");
    if (open) {
        if (ImGui::MenuItem("Keyboard Shortcuts"))
            openDialog(state, DialogKind::Shortcuts);
        if (ImGui::MenuItem("About"))
            openDialog(state, DialogKind::About);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void toolbarSeparator() {
    ImGui::SameLine(0.0F, 10.0F);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine({origin.x, origin.y + 4.0F}, {origin.x, origin.y + 24.0F},
                                        IM_COL32(70, 76, 92, 255));
    ImGui::Dummy({1.0F, 28.0F});
    ImGui::SameLine(0.0F, 10.0F);
}

void toolbar(EditorState &state, ImGuiViewport *viewport) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10.0F, 6.0F});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoScrollWithMouse;
    const bool visible =
        ImGui::BeginViewportSideBar("##Toolbar", viewport, ImGuiDir_Up, 42.0F, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (visible) {
        const bool editing = state.document != nullptr && !state.playing();
        EditorDocument *doc = state.document.get();
        ImGui::BeginDisabled(!editing || !doc->canUndo());
        if (iconButton("toolbar/Undo", Icon::Undo, false, "Undo (Ctrl+Z)"))
            doc->undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!editing || !doc->canRedo());
        if (iconButton("toolbar/Redo", Icon::Redo, false, "Redo (Ctrl+Y)"))
            doc->redo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!editing);
        if (iconButton("toolbar/Save", Icon::Save, doc && doc->dirty(), "Save scene (Ctrl+S)"))
            state.saveScene();
        ImGui::EndDisabled();
        toolbarSeparator();

        ImGui::BeginDisabled(!editing);
        SceneInteraction &interaction = state.interaction;
        if (iconButton("toolbar/Move", Icon::Move, interaction.tool == Tool::Move, "Move (W)"))
            interaction.tool = Tool::Move;
        ImGui::SameLine();
        if (iconButton("toolbar/Resize", Icon::Resize, interaction.tool == Tool::Resize,
                       "Resize (R)"))
            interaction.tool = Tool::Resize;
        ImGui::SameLine();
        if (iconButton("toolbar/Rotate", Icon::Rotate, interaction.tool == Tool::Rotate,
                       "Rotate (E)"))
            interaction.tool = Tool::Rotate;
        toolbarSeparator();
        if (iconButton("toolbar/Snap", Icon::Grid, interaction.snap.enabled,
                       "Snap to grid (hold Ctrl while dragging to toggle)"))
            interaction.snap.enabled = !interaction.snap.enabled;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(72.0F);
        static constexpr float steps[] = {0.1F, 0.25F, 0.5F, 1.0F, 2.0F};
        char current[16];
        std::snprintf(current, sizeof current, "%g m", static_cast<double>(interaction.snap.grid));
        const bool comboOpen = ImGui::BeginCombo("##grid", current);
        markItem("toolbar/GridSize");
        if (comboOpen) {
            for (const float step : steps) {
                char item[16];
                std::snprintf(item, sizeof item, "%g m", static_cast<double>(step));
                if (ImGui::Selectable(item, std::abs(step - interaction.snap.grid) < 1e-4F))
                    interaction.snap.grid = step;
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();

        // Play controls, centered.
        const float groupWidth = 5.0F * 28.0F + 4.0F * ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine();
        const float centered = (ImGui::GetWindowWidth() - groupWidth) * 0.5F;
        if (centered > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(centered);
        const bool canPlay = state.document != nullptr && state.project != nullptr;
        ImGui::BeginDisabled(!canPlay || state.playing());
        if (iconButton("toolbar/Play", Icon::Play, state.playing(), "Play (F5)",
                       packed(palette::good)))
            state.startPlay();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!state.playing());
        if (iconButton("toolbar/Pause", Icon::Pause, state.playing() && state.play->paused(),
                       "Pause (F6)"))
            state.togglePause();
        ImGui::SameLine();
        ImGui::BeginDisabled(!state.playing() || !state.play->paused());
        if (iconButton("toolbar/Step", Icon::Step, false, "Step one tick (F10)"))
            state.stepPlay(InputFrame{});
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (iconButton("toolbar/Stop", Icon::Stop, false, "Stop (Shift+F5)",
                       packed(palette::error)))
            state.stopPlay();
        ImGui::SameLine();
        if (iconButton("toolbar/Restart", Icon::Restart, false, "Restart the scene"))
            state.restartPlay();
        ImGui::EndDisabled();
    }
    ImGui::End();
}

void statusBar(EditorState &state, ImGuiViewport *viewport) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10.0F, 4.0F});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoScrollWithMouse;
    const bool visible =
        ImGui::BeginViewportSideBar("##Status", viewport, ImGuiDir_Down, 26.0F, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (visible) {
        const ConsoleLog::Entry latest = state.console.latest();
        const Color tone = latest.level == LogLevel::Error     ? palette::error
                           : latest.level == LogLevel::Warning ? palette::warning
                                                               : palette::dim;
        const Icon icon = latest.level == LogLevel::Error     ? Icon::Error
                          : latest.level == LogLevel::Warning ? Icon::Warning
                                                              : Icon::Info;
        if (latest.serial != 0) {
            iconLabel(icon, latest.message.c_str(), packed(tone));
        } else {
            ImGui::TextDisabled("Ready");
        }
        std::string right;
        if (state.playing())
            right = state.play->paused() ? "PAUSED   " : "PLAYING   ";
        if (state.document && !state.playing()) {
            right += std::to_string(state.document->selection().size()) + " selected   ";
            right += std::to_string(state.document->scene().size()) + " entities   ";
            char zoom[32];
            std::snprintf(zoom, sizeof zoom, "%d%%   ",
                          static_cast<int>(state.interaction.camera.zoom / 48.0F * 100.0F));
            right += zoom;
        }
        char fps[32];
        std::snprintf(fps, sizeof fps, "%.0f fps", static_cast<double>(state.framesPerSecond));
        right += fps;
        const float width = ImGui::CalcTextSize(right.c_str()).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - width - 12.0F);
        ImGui::TextColored(imColor(state.playing() ? palette::good : palette::dim), "%s",
                           right.c_str());
    }
    ImGui::End();
}

void buildDefaultLayout(ImGuiViewport *viewport) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);
    ImGuiID center = dockspaceId, left = 0, right = 0, bottom = 0, leftBottom = 0;
    left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19F, nullptr, &center);
    right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27F, nullptr, &center);
    bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25F, nullptr, &center);
    leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.42F, nullptr, &left);
    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Assets", leftBottom);
    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderDockWindow("Game", center);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderFinish(dockspaceId);
}
} // namespace

std::string entityLabel(const Scene &scene, EntityId id) {
    const Entity *entity = scene.find(id);
    if (!entity)
        return "(missing)";
    return entity->name().empty() ? "(unnamed)" : entity->name();
}

Vec2 defaultSpawnPoint(const EditorState &state) {
    if (!state.interaction.bound())
        return {};
    return snapTo(state.interaction.camera.center,
                  state.interaction.snap.enabled ? state.interaction.snap.grid : 0.0F);
}

void createEntityMenu(EditorState &state, EntityId parent, std::optional<Vec2> at) {
    if (!state.document || state.playing())
        return;
    const Vec2 world = at ? *at : defaultSpawnPoint(state);
    const bool empty = ImGui::MenuItem("Empty Entity");
    markItem("create/Empty Entity");
    if (empty)
        state.document->createEntity("Entity", parent, world);
    std::vector<std::string> categories;
    for (const EntityTemplate &entityTemplate : state.registry.templates())
        if (std::find(categories.begin(), categories.end(), entityTemplate.category) ==
            categories.end())
            categories.push_back(entityTemplate.category);
    for (const std::string &category : categories) {
        const bool categoryOpen = ImGui::BeginMenu(category.c_str());
        markItem("create-category/" + category);
        if (!categoryOpen)
            continue;
        for (const EntityTemplate &entityTemplate : state.registry.templates()) {
            if (entityTemplate.category != category)
                continue;
            const bool clicked = ImGui::MenuItem(entityTemplate.name.c_str());
            markItem("create/" + entityTemplate.name);
            if (clicked)
                if (auto created =
                        state.document->createFromTemplate(entityTemplate, world, parent);
                    !created)
                    log(LogLevel::Error, "editor", created.error());
        }
        ImGui::EndMenu();
    }
    const std::vector<std::string> prefabs = state.prefabPaths();
    if (!prefabs.empty() && ImGui::BeginMenu("Prefabs")) {
        for (const std::string &path : prefabs)
            if (ImGui::MenuItem(path.c_str()))
                state.instantiatePrefab(path, world);
        ImGui::EndMenu();
    }
}

void addComponentMenu(EditorState &state, EntityId entity) {
    if (!state.document || state.playing())
        return;
    const Entity *target = state.document->scene().find(entity);
    if (!target)
        return;
    std::vector<std::string> categories;
    for (const auto &type : state.registry.types())
        if (!type->hiddenInMenus &&
            std::find(categories.begin(), categories.end(), type->category) == categories.end())
            categories.push_back(type->category);
    std::sort(categories.begin(), categories.end());
    for (const std::string &category : categories) {
        const bool categoryOpen = ImGui::BeginMenu(category.c_str());
        markItem("add-category/" + category);
        if (!categoryOpen)
            continue;
        for (const auto &type : state.registry.types()) {
            if (type->hiddenInMenus || type->category != category)
                continue;
            const bool present =
                target->findComponent(type->name) != nullptr && !type->allowMultiple;
            const bool clicked = ImGui::MenuItem(type->name.c_str(), nullptr, false, !present);
            markItem("add/" + type->name);
            tooltip(type->description);
            if (clicked)
                state.document->addComponent(entity, type->name);
        }
        ImGui::EndMenu();
    }
}

void drawWelcome(EditorState &state) {
    if (state.project)
        return;
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x * 0.5F,
                             viewport->WorkPos.y + viewport->WorkSize.y * 0.45F},
                            ImGuiCond_Always, {0.5F, 0.5F});
    ImGui::SetNextWindowSize({560.0F, 0.0F});
    ImGui::SetNextWindowBgAlpha(0.97F);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {24.0F, 20.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
    if (ImGui::Begin("Welcome##YK", nullptr, flags)) {
        markWindow("panel/Welcome");
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.7F);
        ImGui::TextUnformatted("YK Engine");
        ImGui::PopFont();
        ImGui::TextColored(imColor(palette::dim), "2D game editor");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("New Project...", {160.0F, 32.0F}))
            openDialog(state, DialogKind::NewProject);
        markItem("welcome/New Project");
        ImGui::SameLine();
        if (ImGui::Button("Open Project...", {160.0F, 32.0F}))
            openDialog(state, DialogKind::OpenProject);
        markItem("welcome/Open Project");
        if (const auto sample = findSampleProject()) {
            ImGui::SameLine();
            if (ImGui::Button("Open Sample", {160.0F, 32.0F}))
                state.openProject(*sample);
            markItem("welcome/Open Sample");
            tooltip(
                "Cinder Vale: the demo game, a two-player co-op puzzle level built from the engine's "
                "reusable components.");
        }
        if (!state.recent.paths().empty()) {
            ImGui::Spacing();
            ImGui::TextColored(imColor(palette::dim), "Recent projects");
            const std::vector<std::string> recent = state.recent.paths();
            for (const std::string &path : recent) {
                iconLabel(Icon::Folder, "");
                ImGui::SameLine(0.0F, 0.0F);
                if (ImGui::Selectable(path.c_str())) {
                    state.openProject(path);
                    break;
                }
                markItem("welcome/recent/" + path);
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void drawShell(EditorState &state) {
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    mainMenu(state);
    toolbar(state, viewport);
    statusBar(state, viewport);
    ImGui::DockSpaceOverViewport(dockspaceId, viewport);
    const ImGuiDockNode *node = ImGui::DockBuilderGetNode(dockspaceId);
    if (state.resetLayout || !node || (node->IsLeafNode() && node->Windows.Size == 0)) {
        buildDefaultLayout(viewport);
        state.resetLayout = false;
    }
}

void handleShortcuts(EditorState &state) {
    const auto pressed = [](ImGuiKeyChord chord) {
        return ImGui::Shortcut(chord, ImGuiInputFlags_RouteGlobal);
    };
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Q))
        state.requestQuit();
    if (state.dialog.kind != DialogKind::None)
        return; // Dialogs own the keyboard.
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_O))
        state.guarded([&state] { openDialog(state, DialogKind::OpenProject); });
    if (pressed(ImGuiKey_F5)) {
        if (state.playing())
            state.stopPlay();
        else
            state.startPlay();
    }
    if (pressed(ImGuiMod_Shift | ImGuiKey_F5))
        state.stopPlay();
    if (pressed(ImGuiKey_F6))
        state.togglePause();
    if (pressed(ImGuiKey_F10) && state.playing() && state.play->paused())
        state.stepPlay(InputFrame{});
    if (!state.document)
        return;
    EditorDocument &doc = *state.document;
    if (state.playing()) {
        if (pressed(ImGuiKey_Escape) && state.pick)
            state.pick.reset();
        return;
    }
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_S))
        state.saveScene();
    if (pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S))
        openDialog(state, DialogKind::SaveSceneAs);
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_N))
        state.guarded([&state] { openDialog(state, DialogKind::NewScene); });
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Z))
        doc.undo();
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Y) || pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
        doc.redo();
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_C))
        copySelection(state, false);
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_X))
        copySelection(state, true);
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_V))
        pasteClipboard(state);
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_D))
        doc.duplicateSelection();
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_A))
        doc.selectAll();
    if (pressed(ImGuiKey_Delete))
        doc.deleteSelection();
    if (pressed(ImGuiKey_Escape)) {
        if (state.pick)
            state.pick.reset();
        else if (state.interaction.dragging())
            state.interaction.cancelDrag();
    }
    if (pressed(ImGuiKey_Home))
        state.interaction.frameAll();
    const bool sceneActive = state.sceneView.hovered || state.sceneView.focused;
    if (sceneActive) {
        if (pressed(ImGuiKey_F))
            state.interaction.frameSelection();
        if (pressed(ImGuiKey_W))
            state.interaction.tool = Tool::Move;
        if (pressed(ImGuiKey_R))
            state.interaction.tool = Tool::Resize;
        if (pressed(ImGuiKey_E))
            state.interaction.tool = Tool::Rotate;
        const auto nudge = [&](ImGuiKey key, Vec2 direction) {
            if (ImGui::IsKeyPressed(key, true) && !ImGui::GetIO().WantTextInput)
                state.interaction.nudge(direction, ImGui::GetIO().KeyShift);
        };
        nudge(ImGuiKey_LeftArrow, {-1.0F, 0.0F});
        nudge(ImGuiKey_RightArrow, {1.0F, 0.0F});
        nudge(ImGuiKey_UpArrow, {0.0F, -1.0F});
        nudge(ImGuiKey_DownArrow, {0.0F, 1.0F});
    }
}
} // namespace yk::editor::ui
