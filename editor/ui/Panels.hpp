#pragma once
#include "ui/EditorState.hpp"
#include "ui/UiCommon.hpp"

// One function per editor panel. Panels read EditorState, draw with Dear ImGui and change the
// document only through the editor core (never the scene directly), so every edit is undoable.
namespace yk::editor::ui {
// The whole window: title bar with the menus and Play controls, activity bar, side bar, editor
// tabs and groups, inspector, panel, status bar and the sashes between them.
void drawWorkbench(EditorState &state);
// The menus (File, Edit, View, Scene, Entity, Component, Build, Debug, Help), drawn inside the
// title bar's menu bar.
void drawMenus(EditorState &state);
// Keyboard shortcuts that are not attached to a menu item's focus.
void handleShortcuts(EditorState &state);
// The start page shown in the editor area while no project is open.
void welcomePage(EditorState &state);
// Clipboard commands shared by the menus, shortcuts and context menus.
void copySelection(EditorState &state, bool cut);
void pasteClipboard(EditorState &state);
// Launches the standalone player on the project as a separate process.
Status runInPlayer(EditorState &state);

void hierarchyPanel(EditorState &state);
void inspectorPanel(EditorState &state);
void sceneViewPanel(EditorState &state);
void gameViewPanel(EditorState &state);
void assetsPanel(EditorState &state);
void consolePanel(EditorState &state);
void problemsPanel(EditorState &state);
void outputPanel(EditorState &state);
void profilerPanel(EditorState &state);
void prefabsPanel(EditorState &state);
void componentsPanel(EditorState &state);
void buildPanel(EditorState &state);
void drawDialogs(EditorState &state);
// Opens a dialog with sensible defaults. `entity` is the entity a prefab is saved from.
void showDialog(EditorState &state, DialogKind kind, EntityId entity = {});

// Draws the scene and game views into their render targets. Called between beginFrame and present,
// after the UI was built and before its draw data is submitted.
Status renderViewports(EditorState &state);
Status renderGameViewport(EditorState &state); // The game half of renderViewports.

// Menu content shared by the menu bar and the context menus: create an entity from a template, or
// an empty one. `at` is where to place it (world), `parent` what to attach it to.
void createEntityMenu(EditorState &state, EntityId parent, std::optional<Vec2> at);
// Where new entities land when the position is not given: the middle of the scene view, on the
// grid.
Vec2 defaultSpawnPoint(const EditorState &state);
// Menu of components that can be added to `entity`, grouped by category.
void addComponentMenu(EditorState &state, EntityId entity);

// Makes sure `panel` owns a render target of at least `pixels` in size; returns false when none
// could be created.
bool ensureTarget(EditorState &state, ViewportPanel &panel, Vec2 pixels);
// The ImGui texture reference and UV range that show the used part of the panel's target.
struct TargetImage {
    ImTextureRef texture;
    ImVec2 uv1;
};
std::optional<TargetImage> targetImage(EditorState &state, const ViewportPanel &panel, Vec2 pixels);
// Sets ViewportPanel geometry from the current ImGui item and window (call right after the image).
void trackViewport(ViewportPanel &panel, ImVec2 origin, ImVec2 size);

// A short, human name for an entity in lists: its name, or "(unnamed)".
std::string entityLabel(const Scene &scene, EntityId id);
} // namespace yk::editor::ui
