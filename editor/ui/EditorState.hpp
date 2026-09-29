#pragma once
#include "core/ConsoleLog.hpp"
#include "core/EditorDocument.hpp"
#include "core/EditorProject.hpp"
#include "core/PlaySession.hpp"
#include "core/SceneInteraction.hpp"
#include "yk/audio/SdlAudio.hpp"
#include "yk/graphics/GameView.hpp"
#include "yk/graphics/Renderer.hpp"
#include "yk/graphics/SceneRenderer.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
    bool gamePhysicsDebug{false};
    bool gameColliders{false};
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
    Message
};

struct DialogState {
    DialogKind kind{DialogKind::None};
    bool needsOpen{};
    std::string title;
    std::string text;    // Primary input (a name).
    std::string message; // Body of Message and Unsaved dialogs.
    std::string error;   // Shown in red under the inputs.
    DirectoryBrowser browser;
    EntityId entity;                    // The entity a prefab is saved from.
    std::function<void()> continuation; // What the Unsaved dialog resumes.
    std::vector<ProjectIssue> issues;   // Validation results.
    std::optional<Project> draft;       // Project settings being edited; applied on Save.
};

// Something the user asked for that the entity-reference field is waiting on ("click the door").
struct PickRequest {
    std::string prompt;
    std::function<void(EntityId)> onPick;
};

// Everything the editor knows at run time, and the actions that change it. Panels read this and
// call the actions; the actions never draw anything.
class EditorState {
  public:
    EditorState(const ComponentRegistry &registry, EditorOptions options);

    const ComponentRegistry &registry;
    EditorOptions options;
    Renderer *renderer{};
    std::unique_ptr<SceneRenderer> sceneRenderer;
    std::unique_ptr<SdlAudio> audio;
    ConsoleLog console;
    std::filesystem::path settingsDirectory;
    RecentProjects recent;

    std::unique_ptr<EditorProject> project;
    std::unique_ptr<EditorDocument> document;
    SceneInteraction interaction; // The scene view's camera, tool and drags; outlives documents.
    std::unique_ptr<PlaySession> play;
    EntityId playSelection; // Inspected entity while playing (read-only).

    ViewportPanel sceneView, gameView;
    ViewOptions view;
    DialogState dialog;
    std::optional<PickRequest> pick;
    std::string focusRequest; // Window to focus next frame ("Game", "Scene").
    bool showHierarchy{true}, showInspector{true}, showAssets{true}, showConsole{true};
    bool showScene{true}, showGame{true};
    bool resetLayout{};
    bool frameRequested{}; // Frame the whole scene once the scene view knows its size.
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
    Status openScene(const std::string &path);
    Status newScene(const std::string &path);
    Status saveScene();
    Status saveSceneAs(const std::string &path);
    Status savePrefab(EntityId entity, const std::string &path);
    Status instantiatePrefab(const std::string &path, Vec2 world);
    void validateProject();
    // Copies the project and the player next to it into a new folder (see
    // EditorProject::exportGame).
    Status exportGame(const std::filesystem::path &destination);
    std::filesystem::path playerExecutable() const;
    // Runs `action` now, or after the user decides what to do with unsaved changes.
    void guarded(std::function<void()> action);

    // Play mode.
    void startPlay();
    void stopPlay();
    void togglePause();
    void stepPlay(const Keyboard &keyboard);
    void restartPlay();

    void message(std::string title, std::string body);
    void requestQuit();
    // Scene names for menus, from the project's file list.
    std::vector<std::string> scenePaths() const;
    std::vector<std::string> prefabPaths() const;
    std::string windowTitle() const;
    // Advances play mode and background services once per frame.
    void tick(double seconds, const Keyboard &gameInput);

  private:
    void bindDocument();
    Status attachProject(std::unique_ptr<EditorProject> opened);
};

// The user's per-user data directory for the editor (created on demand).
std::filesystem::path defaultSettingsDirectory();
} // namespace yk::editor
