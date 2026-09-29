#include "imgui_internal.h"
#include "ui/Panels.hpp"
#include "yk/core/Log.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace yk::editor::ui {
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

void prefabMenuItems(EditorState &state, EntityId entity, const std::string &menuId) {
    if (!state.document || state.playing())
        return;
    const EntityId root = state.document->prefabRootOf(entity);
    if (!root)
        return;
    const std::string source = state.document->scene().find(root)->prefabSource();
    const auto id = [&](const char *leaf) { return "menu/" + menuId + "/" + leaf; };
    if (ImGui::MenuItem("Show Prefab in Explorer"))
        state.showAssetInExplorer(source);
    if (!menuId.empty())
        markItem(id("Show Prefab in Explorer"));
    ImGui::Separator();
    if (ImGui::MenuItem("Revert to Prefab"))
        state.revertPrefab(entity);
    if (!menuId.empty())
        markItem(id("Revert to Prefab"));
    tooltip("Put this instance back to what the prefab file says. Its name, position, rotation and "
            "size stay; its parts and settings come from the prefab.");
    if (ImGui::MenuItem("Apply to Prefab"))
        state.applyPrefab(entity);
    if (!menuId.empty())
        markItem(id("Apply to Prefab"));
    tooltip("Write this instance to the prefab file and update the other instances in the open "
            "scenes.");
    ImGui::Separator();
    if (ImGui::MenuItem("Unpack Prefab"))
        state.unpackPrefab(entity);
    if (!menuId.empty())
        markItem(id("Unpack Prefab"));
    tooltip("Forget where this instance came from. Its entities stay as they are.");
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

void handleShortcuts(EditorState &state) {
    const auto pressed = [](ImGuiKeyChord chord) {
        return ImGui::Shortcut(chord, ImGuiInputFlags_RouteGlobal);
    };
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Q))
        state.requestQuit();
    if (state.dialog.kind != DialogKind::None)
        return; // Dialogs own the keyboard.
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_O))
        state.guarded([&state] { showDialog(state, DialogKind::OpenProject); });

    // The workbench: side bar views, panels and the editor layout.
    WorkbenchLayout &layout = state.layout;
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_B))
        layout.sideBarVisible = !layout.sideBarVisible;
    if (pressed(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_B))
        layout.inspectorVisible = !layout.inspectorVisible;
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_J))
        layout.panelVisible = !layout.panelVisible;
    const auto showSide = [&](ImGuiKeyChord chord, SideView view) {
        if (pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | chord))
            layout.toggleSideView(view);
    };
    showSide(ImGuiKey_E, SideView::Explorer);
    showSide(ImGuiKey_H, SideView::Scene);
    showSide(ImGuiKey_K, SideView::Prefabs);
    showSide(ImGuiKey_X, SideView::Components);
    showSide(ImGuiKey_B, SideView::Build);
    const auto showPanel = [&](ImGuiKeyChord chord, PanelView view) {
        if (pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | chord)) {
            layout.panelView = view;
            layout.panelVisible = true;
        }
    };
    showPanel(ImGuiKey_Y, PanelView::Console);
    showPanel(ImGuiKey_M, PanelView::Problems);
    showPanel(ImGuiKey_U, PanelView::Output);
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Backslash))
        layout.split = layout.split == EditorSplit::None ? EditorSplit::Right : EditorSplit::None;

    if (pressed(ImGuiKey_F5)) {
        if (state.playing())
            state.stopPlay();
        else
            state.startPlay();
    }
    if (pressed(ImGuiMod_Shift | ImGuiKey_F5))
        state.stopPlay();
    if (pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F5))
        state.restartPlay();
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_F5) && !state.playing())
        runInPlayer(state);
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
    if (pressed(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_S))
        state.saveAll();
    if (pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S))
        showDialog(state, DialogKind::SaveSceneAs);
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_N))
        showDialog(state, DialogKind::NewScene);
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_W))
        state.closeScene(doc.path());
    if (state.sceneTabs.size() > 1) {
        const auto cycle = [&](int step) {
            const auto found =
                std::find(state.sceneTabs.begin(), state.sceneTabs.end(), doc.path());
            const auto count = static_cast<int>(state.sceneTabs.size());
            const int at = static_cast<int>(found - state.sceneTabs.begin());
            state.activateScene(
                state.sceneTabs[static_cast<std::size_t>((at + step + count) % count)]);
        };
        if (pressed(ImGuiMod_Ctrl | ImGuiKey_Tab))
            cycle(1);
        if (pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Tab))
            cycle(-1);
        if (state.document.get() != &doc)
            return; // The tab changed; the rest waits for the next frame.
    }
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
