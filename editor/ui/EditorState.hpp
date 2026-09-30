#pragma once
#include "core/ConsoleLog.hpp"
#include "core/EditorDocument.hpp"
#include "core/EditorProject.hpp"
#include "core/PlaySession.hpp"
#include "core/SceneInteraction.hpp"
#include "core/WorkbenchLayout.hpp"
#include "yk/audio/SdlAudio.hpp"
#include "yk/graphics/GameView.hpp"
#include "yk/graphics/Renderer.hpp"
#include "yk/graphics/SceneRenderer.hpp"
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Process;

namespace yk::editor {
struct EditorOptions {
    std::filesystem::path project;           // Opened at startup when given.
    std::string scene;                       // Opened instead of the project's start scene.
    std::filesystem::path settingsDirectory; // Empty: the platform's per-user location.
    bool audio{true};
    bool testHooks{false}; // Record widget rectangles so a scripted driver can find them.
    bool persistLayout{true};
    // Play advances exactly one 1/60 s tick per editor frame, however long the frame took, so a
    // scripted run is deterministic (a script's "hold d 45" is 0.75 s of game time on any machine).
    bool fixedStep{false};
    // Diagnostics (set by the host, see yk/core/Diagnostics.hpp): where the log and crash reports
    // are, and whether the last session ended badly, so the editor can say so.
    std::filesystem::path logDirectory;
    bool previousSessionCrashed{false};
    std::filesystem::path previousCrashReport;
    std::string debugCrash; // Test hook: crash on purpose after a few frames.
};

// A panel that shows a render target: the scene view or the game view.
struct ViewportPanel {
    TextureHandle texture;
    Vec2 allocated;    // Texture size in pixels (grown in steps to avoid churn).
    Vec2 size;         // Size of the visible image in points, this frame.
    Vec2 origin;       // Screen position (points) of the image's top-left corner.
    float scale{1.0F}; // Pixels per point on HiDPI displays.
    bool visible{};
    bool hovered{};
    bool focused{};
    // Scene view input state: a left drag owned by the interaction, a pan in progress, and where a
    // context menu was requested.
    bool capturedLeft{};
    bool panning{};
    Vec2 rightPress;
    Vec2 contextWorld;
};

struct ViewOptions {
    bool grid{true};
    bool colliders{true};
    bool links{true};
    bool allLinks{false};
    bool labels{false};
    bool cameraFrame{true};
    bool spriteBounds{false};
    bool pivots{false};
    bool gamePhysicsDebug{false};
    bool gameColliders{false};
    bool gameStats{false};
};

// A folder listing for the project pickers.
struct DirectoryBrowser {
    struct Entry {
        std::string name;
        bool isProject{};
    };
    std::filesystem::path current;
    std::string pathText; // What the path field shows (editable; Enter navigates there).
    std::vector<Entry> folders;
    bool hasProject{}; // `current` itself holds a project.
    std::string error;
    void go(const std::filesystem::path &directory);
};

enum class DialogKind {
    None,
    NewProject,
    OpenProject,
    NewScene,
    SaveSceneAs,
    SavePrefab,
    Unsaved,
    ProjectSettings,
    Export,
    Validation,
    About,
    Shortcuts,
    Message,
    Confirm,
    MoveAsset
};

struct DialogState {
    DialogKind kind{DialogKind::None};
    bool needsOpen{};
    std::string title;
    std::string text;    // Primary input (a name).
    std::string message; // Body of Message and Unsaved dialogs.
    std::string error;   // Shown in red under the inputs.
    DirectoryBrowser browser;
    EntityId entity;                        // The entity a prefab is saved from.
    std::function<void()> continuation;     // What the Unsaved dialog resumes.
    std::vector<std::string> unsavedScenes; // What the Unsaved dialog offers to save.
    std::vector<ProjectIssue> issues;       // Validation results.
    std::optional<Project> draft;           // Project settings being edited; applied on Save.
    std::string confirmLabel{"OK"};         // The Confirm dialog's accept button.
    // The Export dialog.
    BuildTarget exportTarget{hostTarget()};
    std::string exportPlayer; // Player program override; empty: the one found for the target.
    bool exportZip{true};
    bool exportReplace{};
    // A folder the Message dialog offers to show in the file manager.
    std::filesystem::path revealPath;
    // The Rename or Move dialog: the file or folder being moved (`text` holds the new path).
    std::string assetPath;
};

// Something the user asked for that the entity-reference field is waiting on ("click the door").
struct PickRequest {
    std::string prompt;
    std::function<void(EntityId)> onPick;
};

// A scene that is open in a tab but is not the one being edited right now. Switching tabs swaps it
// with EditorState::document, so the rest of the editor only ever deals with one document.
struct BackgroundScene {
    std::unique_ptr<EditorDocument> document;
    ViewCamera camera; // Where its scene view was looking.
};

// Which editor the first group shows when it has more than one tab.
enum class EditorFocus { Scene, Game };

// One line of the Build Output panel.
struct OutputLine {
    LogLevel level{LogLevel::Info};
    std::string text;
};

// A program the editor started (Debug > Run in Player). The editor owns it: it is watched every
// frame, can be stopped from the Debug menu and is ended when the editor closes, so no game is left
// running behind a closed editor.
struct ChildProcess {
    SDL_Process *handle{};
    std::string name;
    std::chrono::steady_clock::time_point started;
};

// Everything the editor knows at run time, and the actions that change it. Panels read this and
// call the actions; the actions never draw anything.
class EditorState {
  public:
    EditorState(const ComponentRegistry &registry, EditorOptions options);
    ~EditorState();
    EditorState(const EditorState &) = delete;
    EditorState &operator=(const EditorState &) = delete;

    const ComponentRegistry &registry;
    EditorOptions options;
    Renderer *renderer{};
    std::unique_ptr<SceneRenderer> sceneRenderer;
    std::unique_ptr<SdlAudio> audio;
    ConsoleLog console;
    std::filesystem::path settingsDirectory;
    RecentProjects recent;

    std::unique_ptr<EditorProject> project;
    // The scene being edited (the active tab). The other open scenes wait in `background`;
    // `sceneTabs` lists them all, in tab order.
    std::unique_ptr<EditorDocument> document;
    std::vector<std::string> sceneTabs;
    std::map<std::string, BackgroundScene> background;
    SceneInteraction interaction; // The scene view's camera, tool and drags; outlives documents.
    std::unique_ptr<PlaySession> play;
    EntityId playSelection; // Inspected entity while playing (read-only).

    ViewportPanel sceneView, gameView;
    ViewOptions view;
    DialogState dialog;
    std::optional<PickRequest> pick;
    EditorFocus editorFocus{EditorFocus::Scene};
    bool focusGame{}; // Play just started: give the game view the keyboard.
    WorkbenchLayout layout;
    WorkbenchLayout savedLayout;        // What is on disk; the layout is written when it differs.
    std::vector<ProjectIssue> problems; // Result of the last project check (Problems panel).
    bool problemsChecked{};
    std::vector<float> frameMilliseconds; // Recent editor frame times, for the Profiler.
    static constexpr std::size_t frameMillisecondsCapacity = 240;
    std::vector<OutputLine> output; // Build Output panel.
    std::string componentFilter, prefabFilter;
    std::string selectedAsset; // Project file picked in the Explorer (Inspector shows it).
    // The entity selection at the moment `selectedAsset` was picked: once the selection changes the
    // Inspector goes back to showing entities.
    std::vector<EntityId> assetSelectionMark;
    std::string explorerReveal; // File the Explorer should open its folders for and scroll to.
    std::string explorerTarget; // The row F2 and Delete act on (a file or a folder); empty: none.
    bool explorerFocused{};     // The Explorer has the keyboard (so Delete is not an entity's).
    bool frameRequested{};      // Frame the whole scene once the scene view knows its size.
    bool quit{};
    float framesPerSecond{};
    std::string hierarchyFilter;
    EntityId renaming; // Hierarchy row being renamed.
    std::string renameBuffer;
    bool renameFocus{};
    EntityId selectionAnchor; // For shift-click range selection in the hierarchy.
    EntityId pressedRow;      // Hierarchy row the mouse went down on; it is selected on release.
    std::string assetFilter;

    bool playing() const {
        return play != nullptr;
    }
    // The scene panels show: the running copy while playing, else the open document.
    const Scene *visibleScene() const;
    // The primary selection of whichever scene is visible.
    EntityId inspected() const;
    void inspect(EntityId id, SelectMode mode = SelectMode::Replace);

    // Called once the renderer exists.
    Status initialize(Renderer &renderer);

    // Project and scene lifecycle. Errors are logged; the returned status carries the reason.
    Status createProject(const std::filesystem::path &directory, const std::string &name);
    Status openProject(const std::filesystem::path &fileOrDirectory);
    void closeProject();
    // Opens a scene in a tab (or shows its tab when it is already open).
    Status openScene(const std::string &path);
    Status newScene(const std::string &path);
    // Makes an open scene the one being edited.
    Status activateScene(const std::string &path);
    // Closes a scene's tab, asking about unsaved changes first.
    void closeScene(const std::string &path);
    // Closes a scene's tab without asking (its file is gone, or it is being reloaded).
    void dropScene(const std::string &path);
    // Closes every scene tab and opens them again from disk, in the same order and with the same
    // one active. `remap` gives a scene's path after a change of files (empty: it is gone).
    void reloadScenes(const std::function<std::string(const std::string &)> &remap);
    // True when any open scene has changes that are not saved.
    bool anyDirty() const;
    std::vector<std::string> dirtyScenes() const;
    Status saveScene();
    Status saveAll();
    // Saves the listed open scenes (the ones that have unsaved changes).
    Status saveScenes(const std::vector<std::string> &paths);
    Status saveSceneAs(const std::string &path);
    Status savePrefab(EntityId entity, const std::string &path);
    Status instantiatePrefab(const std::string &path, Vec2 world);
    // Checks the project and shows the result in the Problems panel.
    void validateProject();
    // Checks the project quietly (after opening or saving); the Problems panel and status bar
    // update.
    void refreshProblems();
    void addOutput(LogLevel level, const std::string &text); // A line for the Build Output panel.
    // Copies files into the project (Explorer's Import Assets, files dropped on the window). Every
    // file is reported in the console; the first imported one is selected in the Explorer.
    Status importAssets(const std::vector<std::filesystem::path> &files,
                        const std::string &folder = {});
    // Files chosen in the system's file dialog or dropped on the window arrive here from wherever
    // SDL calls back (possibly another thread); tick() imports them on the main thread.
    void queueImport(std::vector<std::filesystem::path> files);
    // Prefab instances (the entity may be any part of the instance). Revert puts it back to its
    // prefab file; Apply writes it to the prefab file and updates the other instances in the open
    // scenes; Unpack forgets the link. All of them tell the console what they did.
    Status revertPrefab(EntityId entity);
    Status applyPrefab(EntityId entity);
    // Puts every other instance of the same prefab (in all open scenes) back to the prefab file's
    // contents, after asking: their own changes other than name and placement are replaced.
    void updateOtherInstances(EntityId entity);
    void unpackPrefab(EntityId entity);
    // Selects a project file: the Explorer opens its folders and scrolls to it, and the Inspector
    // shows it until something else is selected.
    void showAssetInExplorer(const std::string &path);
    // Explorer file operations (the Explorer's menu and F2 / Delete). Both first ask about unsaved
    // scenes, because open scenes are reloaded from the rewritten files. Renaming or moving asks
    // for the new path in a dialog; deleting says what would be left dangling and asks first.
    void askToMoveAsset(const std::string &path);
    void askToDeleteAsset(const std::string &path);
    // What those dialogs do once accepted; both tell the console what they did. Moving rewrites
    // every reference to the file or folder (EditorProject::moveAsset) and reloads the open scenes.
    Status moveAsset(const std::string &from, const std::string &to);
    Status deleteAsset(const std::string &path);
    // Opens the system's file dialog to pick files to import.
    void chooseAssetsToImport(SDL_Window *window);
    // The folder the Explorer last selected or opened (imports go there); project-relative.
    std::string explorerFolder;
    // The folder the editor runs from: the player, `yk` and export templates live beside it.
    std::filesystem::path executableDirectory() const;
    // The player program for `target`, when one can be found (Export.hpp: findPlayer).
    std::optional<std::filesystem::path> playerFor(BuildTarget target) const;
    std::filesystem::path playerExecutable() const; // The one for this system (may not exist).
    // Packages the game (see yk/assets/Export.hpp). Every step goes to the Build Output panel and
    // the console; the panel opens so the result is seen.
    Result<ExportReport> exportGame(ExportOptions request);
    // Runs `action` now, or after the user decides what to do with unsaved changes.
    void guarded(std::function<void()> action);

    // Runs the open project in the standalone player, as a process the editor keeps track of (a
    // player already started from here is ended first). Errors are reported in a dialog.
    Status startPlayerProcess();
    // Asks the players to close, then ends any that are still running after a moment.
    void stopPlayerProcesses();
    bool playerProcessRunning() const {
        return !children.empty();
    }
    std::vector<ChildProcess> children;

    // Play mode.
    void startPlay();
    void stopPlay();
    void togglePause();
    void stepPlay(const InputFrame &input);
    void restartPlay();

    void message(std::string title, std::string body);
    void requestQuit();
    // Scene names for menus, from the project's file list.
    std::vector<std::string> scenePaths() const;
    std::vector<std::string> prefabPaths() const;
    std::string windowTitle() const;
    // Advances play mode and background services once per frame.
    void tick(double seconds, const InputFrame &gameInput);

  private:
    std::mutex importMutex_;
    std::vector<std::filesystem::path> importQueue_;
    void bindDocument();
    void stashActive();
    Status attachProject(std::unique_ptr<EditorProject> opened);
};

// The user's per-user data directory for the editor (created on demand).
std::filesystem::path defaultSettingsDirectory();
} // namespace yk::editor
