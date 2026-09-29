#pragma once
#include "core/EditorDocument.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Project.hpp"
#include "yk/assets/Validation.hpp"
#include <filesystem>
#include <memory>
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

    // Loads every scene and prefab and reports what a game would trip over.
    std::vector<ProjectIssue> validate() const;

    // Copies the project and the player executable into <destination>/<project name>-game/, the
    // player beside a project/ folder: everything needed to run the game. Refuses a project with
    // errors and never overwrites an existing folder. Returns the folder that was created.
    Result<std::filesystem::path> exportGame(const std::filesystem::path &destination,
                                             const std::filesystem::path &player) const;

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
