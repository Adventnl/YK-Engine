#pragma once
#include "core/EditorDocument.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Export.hpp"
#include "yk/assets/Project.hpp"
#include "yk/assets/Validation.hpp"
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace yk::editor {
// A project open in the editor: its files on disk plus everything needed to create, open and save
// scenes and prefabs inside it. Paths in the API are project-relative with '/' separators, exactly
// as project data stores them.
class EditorProject {
  public:
    // Creates project.ykproj, a start scene with a camera, and empty scenes/, prefabs/ and assets/
    // folders. Fails when the directory already holds a project.
    static Result<std::unique_ptr<EditorProject>> create(const std::filesystem::path &directory,
                                                         std::string name,
                                                         const ComponentRegistry &registry);
    // Accepts the project file or its directory.
    static Result<std::unique_ptr<EditorProject>> open(const std::filesystem::path &fileOrDirectory,
                                                       const ComponentRegistry &registry);

    EditorProject(const EditorProject &) = delete;
    EditorProject &operator=(const EditorProject &) = delete;

    Project &project() {
        return project_;
    }
    const Project &project() const {
        return project_;
    }
    const ComponentRegistry &registry() const {
        return *registry_;
    }
    const AssetSource &assets() const {
        return assets_;
    }
    // Writes project.ykproj (name, window, layers, start scene).
    Status save() const;

    // Files below the project root, refreshed by refresh().
    const std::vector<AssetEntry> &files() const {
        return files_;
    }
    void refresh();

    // The file kinds Import Assets accepts: images (.png, .bmp) and sounds (.wav).
    static bool importable(const std::filesystem::path &file);
    // Where an imported file goes when the user gave no folder: assets/textures or assets/audio.
    static std::string defaultImportFolder(const std::filesystem::path &file);
    // Copies files from outside the project into `folder` (project-relative; empty picks a folder
    // per kind). Existing files are never overwritten: a numbered name is used instead. Unsupported
    // files are skipped and named in `skipped`. Returns the new project-relative paths.
    struct ImportResult {
        std::vector<std::string> imported;
        std::vector<std::string> skipped; // "name: reason"
    };
    Result<ImportResult> importFiles(const std::vector<std::filesystem::path> &files,
                                     const std::string &folder);

    // File operations. Paths are project-relative ('/' or '\\' separators are accepted); a file or
    // a folder. Data files (scenes, prefabs, animations, controllers) and the start scene in
    // project.ykproj refer to assets by these paths, so a move rewrites every reference and a
    // delete can say what it would leave dangling.
    struct AssetUsage {
        std::size_t references{0};
        std::vector<std::string> files; // The data files that name it, sorted.
    };
    // Who names `path` (a folder: anything inside it).
    AssetUsage usageOf(const std::string &path) const;
    // Says why `from` cannot become `to` (nothing to move, the destination exists, it would leave
    // the project, ...), or succeeds. Changes nothing.
    Status checkMove(const std::string &from, const std::string &to) const;
    struct MoveResult {
        std::string from, to;               // Normalized.
        std::size_t references{0};          // Rewritten.
        std::vector<std::string> rewritten; // Data files whose text changed (paths after the move).
    };
    // Moves or renames a file or folder inside the project (a texture's import settings go with
    // it) and rewrites every reference to it. All or nothing: when something cannot be written,
    // what was done is undone. Scenes open in an editor must be reloaded by the caller.
    Result<MoveResult> moveAsset(const std::string &from, const std::string &to);
    // Deletes a file (and a texture's import settings) or a folder with everything in it.
    // References to it stay as they are: validation reports them.
    Status deleteAsset(const std::string &path);
    // The number of files a delete of `path` would remove (1 for a file).
    std::size_t filesUnder(const std::string &path) const;

    // A new, empty scene (with a camera) saved at `path`. Fails if the file exists.
    Result<std::unique_ptr<EditorDocument>> newScene(const std::string &path);
    Result<std::unique_ptr<EditorDocument>> openScene(const std::string &path) const;
    // Writes the document to its own path (which must be set) and marks it saved.
    Status saveScene(EditorDocument &document);
    // Writes to a new path, which becomes the document's path.
    Status saveSceneAs(EditorDocument &document, const std::string &path);
    // Saves the entity and everything below it as a reusable prefab.
    Status savePrefab(const EditorDocument &document, EntityId root, const std::string &path);
    Result<Json> loadPrefab(const std::string &path) const;
    // Writes the instance rooted at `root` back to the prefab file it came from (its root's
    // position is not stored, so the prefab has no place of its own).
    Status applyToPrefab(const EditorDocument &document, EntityId root);

    // Prefab instances in the project's scenes that are not open in an editor (`open` lists the
    // scenes that are, by path): how many, and which scenes hold them.
    struct ClosedInstances {
        std::size_t instances{0};
        std::vector<std::string> scenes; // Sorted; only scenes that hold at least one.
    };
    ClosedInstances closedInstancesOf(const std::string &source,
                                      const std::set<std::string> &open) const;
    struct ClosedUpdate {
        std::size_t updated{0};
        std::vector<std::string> scenes;   // The scenes that were written.
        std::vector<std::string> failures; // "scene: why" for the ones that could not be.
    };
    // Puts those instances back to the prefab file's contents (names and placement stay, as in
    // EditorDocument::updatePrefabInstances) and saves each scene that changed. Scenes are written
    // right away, so this cannot be undone; a scene that cannot be loaded or saved is reported and
    // the others still go ahead.
    ClosedUpdate updateClosedInstances(const std::string &source,
                                       const std::set<std::string> &open);

    // Loads every scene and prefab and reports what a game would trip over.
    std::vector<ProjectIssue> validate() const;

    // Packages the project as a game for `options.target` (see yk/assets/Export.hpp): the player
    // program, the project's data, notices and a README. Refuses a project with errors.
    Result<ExportReport> exportGame(const ExportOptions &options) const;

    // "scenes/level 1" or "level.txt" -> a path with the wanted extension, or an error when the
    // result would leave the project.
    Result<std::string> withExtension(const std::string &path, const char *extension) const;

  private:
    EditorProject(Project project, const ComponentRegistry &registry);

    Project project_;
    const ComponentRegistry *registry_;
    ProjectAssets assets_;
    std::vector<AssetEntry> files_;
};

// Remembers recently opened projects between sessions (a small JSON file in the user's data
// directory). Missing or corrupt files read as empty; they are never fatal.
class RecentProjects {
  public:
    static constexpr std::size_t maxEntries = 8;
    explicit RecentProjects(std::filesystem::path file) : file_(std::move(file)) {}

    const std::vector<std::string> &paths() const {
        return paths_;
    }
    void load();
    Status save() const;
    // Most recent first; entries that no longer exist are dropped on load, not here.
    void add(const std::filesystem::path &projectDirectory);

  private:
    std::filesystem::path file_;
    std::vector<std::string> paths_;
};
} // namespace yk::editor
