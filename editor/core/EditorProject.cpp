#include "core/EditorProject.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <algorithm>
#include <cctype>

namespace yk::editor {
namespace {
// An empty scene a designer can start in: just the camera the game will look through.
std::unique_ptr<Scene> startingScene(const ComponentRegistry &registry, const std::string &name) {
    auto scene = std::make_unique<Scene>(registry);
    scene->settings.name = name;
    Entity &camera = scene->createEntity("Main Camera");
    camera.addComponent("Camera");
    return scene;
}

std::string sceneNameFor(const std::string &path) {
    return std::filesystem::path(path).stem().string();
}
} // namespace

EditorProject::EditorProject(Project project, const ComponentRegistry &registry)
    : project_(std::move(project)), registry_(&registry), assets_(project_) {}

Result<std::unique_ptr<EditorProject>> EditorProject::create(const std::filesystem::path &directory,
                                                             std::string name,
                                                             const ComponentRegistry &registry) {
    if (name.empty())
        return Error{"A project needs a name"};
    Project project = Project::create(directory, std::move(name));
    if (std::filesystem::exists(project.file()))
        return Error{"'" + project.root.string() + "' already contains a project"};
    std::error_code error;
    for (const char *folder : {"scenes", "prefabs", "assets"}) {
        std::filesystem::create_directories(project.root / folder, error);
        if (error)
            return Error{"Cannot create '" + (project.root / folder).string() +
                         "': " + error.message()};
    }
    project.layers = layers::standard();
    project.startScene = "scenes/main.ykscene";
    const auto scene = startingScene(registry, "Main");
    if (auto status = yk::saveScene(*scene, project.root / project.startScene); !status)
        return Error{status.error()};
    if (auto status = project.save(); !status)
        return Error{status.error()};
    std::unique_ptr<EditorProject> created(new EditorProject(std::move(project), registry));
    created->refresh();
    return created;
}

Result<std::unique_ptr<EditorProject>>
EditorProject::open(const std::filesystem::path &fileOrDirectory,
                    const ComponentRegistry &registry) {
    auto project = Project::load(fileOrDirectory);
    if (!project)
        return Error{project.error()};
    std::unique_ptr<EditorProject> opened(new EditorProject(std::move(project.value()), registry));
    opened->refresh();
    return opened;
}

Status EditorProject::save() const {
    return project_.save();
}

void EditorProject::refresh() {
    files_ = scanAssets(project_);
}

Result<std::string> EditorProject::withExtension(const std::string &path,
                                                 const char *extension) const {
    std::string text = path;
    std::replace(text.begin(), text.end(), '\\', '/');
    const auto first = text.find_first_not_of(" \t");
    const auto last = text.find_last_not_of(" \t");
    if (first == std::string::npos)
        return Error{"Enter a file name"};
    text = text.substr(first, last - first + 1);
    if (!text.ends_with(extension))
        text += extension;
    if (text == extension || text.ends_with(std::string("/") + extension))
        return Error{"Enter a file name"};
    auto resolved = project_.resolve(text);
    if (!resolved)
        return Error{resolved.error()};
    return text;
}

Result<std::unique_ptr<EditorDocument>> EditorProject::newScene(const std::string &path) {
    auto relative = withExtension(path, sceneExtension);
    if (!relative)
        return Error{relative.error()};
    const auto absolute = project_.resolve(relative.value());
    if (std::filesystem::exists(absolute.value()))
        return Error{"'" + relative.value() + "' already exists"};
    auto document = std::make_unique<EditorDocument>(
        *registry_, startingScene(*registry_, sceneNameFor(relative.value())), relative.value());
    if (auto status = saveScene(*document); !status)
        return Error{status.error()};
    return document;
}

Result<std::unique_ptr<EditorDocument>> EditorProject::openScene(const std::string &path) const {
    auto absolute = project_.resolve(path);
    if (!absolute)
        return Error{absolute.error()};
    auto scene = loadScene(absolute.value(), *registry_);
    if (!scene)
        return Error{scene.error()};
    return std::make_unique<EditorDocument>(*registry_, std::move(scene.value()), path);
}

Status EditorProject::saveScene(EditorDocument &document) {
    if (document.path().empty())
        return Error{"This scene has no file yet; use Save As"};
    if (document.inChange())
        return Error{"Finish the current edit before saving"};
    auto absolute = project_.resolve(document.path());
    if (!absolute)
        return Error{absolute.error()};
    if (auto status = yk::saveScene(document.scene(), absolute.value()); !status)
        return status;
    document.markSaved();
    refresh();
    log(LogLevel::Info, "editor", "Saved " + document.path());
    return success();
}

Status EditorProject::saveSceneAs(EditorDocument &document, const std::string &path) {
    auto relative = withExtension(path, sceneExtension);
    if (!relative)
        return Error{relative.error()};
    const std::string previous = document.path();
    document.setPath(relative.value());
    if (auto status = saveScene(document); !status) {
        document.setPath(previous);
        return status;
    }
    return success();
}

Status EditorProject::savePrefab(const EditorDocument &document, EntityId root,
                                 const std::string &path) {
    auto relative = withExtension(path, prefabExtension);
    if (!relative)
        return Error{relative.error()};
    auto absolute = project_.resolve(relative.value());
    if (auto status = yk::savePrefab(document.scene(), root, absolute.value()); !status)
        return status;
    refresh();
    log(LogLevel::Info, "editor", "Saved prefab " + relative.value());
    return success();
}

Result<Json> EditorProject::loadPrefab(const std::string &path) const {
    auto absolute = project_.resolve(path);
    if (!absolute)
        return Error{absolute.error()};
    return loadPrefabDocument(absolute.value());
}

std::vector<ProjectIssue> EditorProject::validate() const {
    return validateProject(project_, *registry_);
}

Result<std::filesystem::path> EditorProject::exportGame(const std::filesystem::path &destination,
                                                        const std::filesystem::path &player) const {
    std::error_code error;
    if (!std::filesystem::is_regular_file(player, error))
        return Error{"The player program was not found (" + player.string() +
                     "). Build the yk_player target next to the editor."};
    const auto issues = validate();
    if (hasErrors(issues)) {
        std::size_t errors = 0;
        for (const ProjectIssue &issue : issues)
            errors += issue.severity == ProjectIssue::Severity::Error ? 1 : 0;
        return Error{"The project has " + std::to_string(errors) +
                     " error(s); fix them first (File > Validate Project). First: " +
                     issues.front().path + ": " + issues.front().message};
    }
    std::string folderName;
    for (const char c : project_.name)
        folderName +=
            (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ') ? c
                                                                                              : '_';
    if (folderName.empty())
        folderName = "game";
    folderName +=
        "-game"; // Never collides with the project's own folder, which usually shares its name.
    const std::filesystem::path folder = destination / folderName;
    if (std::filesystem::exists(folder, error))
        return Error{"'" + folder.string() +
                     "' already exists; choose another folder or remove it"};
    std::filesystem::create_directories(folder / "project", error);
    if (error)
        return Error{"Cannot create '" + folder.string() + "': " + error.message()};
    const auto copy = [&](const std::filesystem::path &from,
                          const std::filesystem::path &to) -> Status {
        std::filesystem::create_directories(to.parent_path(), error);
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing,
                                   error);
        if (error)
            return Error{"Cannot copy '" + from.string() + "': " + error.message()};
        return success();
    };
    const std::filesystem::path playerCopy = folder / player.filename();
    if (auto status = copy(player, playerCopy); !status)
        return Error{status.error()};
    std::filesystem::permissions(playerCopy,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::group_exec |
                                     std::filesystem::perms::others_exec,
                                 std::filesystem::perm_options::add, error);
    for (const AssetEntry &entry : scanAssets(project_))
        if (auto status = copy(project_.root / entry.path, folder / "project" / entry.path);
            !status)
            return Error{status.error()};
    const std::string readme =
        project_.name + "\n\nRun " + player.filename().string() +
        " from this folder to play. The game data is in the project folder;\n" +
        "the player finds it next to itself, or run: " + player.filename().string() +
        " <path to project>\n";
    if (auto status = writeTextFileAtomic(folder / "README.txt", readme); !status)
        return Error{status.error()};
    log(LogLevel::Info, "editor", "Exported the game to " + folder.string());
    return folder;
}

void RecentProjects::load() {
    paths_.clear();
    auto text = readTextFile(file_);
    if (!text)
        return;
    auto document = Json::parse(text.value());
    if (!document)
        return;
    for (const Json &item : document.value().get("recent").items()) {
        std::error_code error;
        if (item.isString() && std::filesystem::exists(item.asString(), error) &&
            paths_.size() < maxEntries)
            paths_.push_back(item.asString());
    }
}

Status RecentProjects::save() const {
    Json document = Json::object();
    Json list = Json::array();
    for (const std::string &path : paths_)
        list.push(path);
    document.set("recent", list);
    return writeTextFileAtomic(file_, document.dump(2) + "\n");
}

void RecentProjects::add(const std::filesystem::path &projectDirectory) {
    const std::string path =
        std::filesystem::absolute(projectDirectory).lexically_normal().string();
    std::erase(paths_, path);
    paths_.insert(paths_.begin(), path);
    if (paths_.size() > maxEntries)
        paths_.resize(maxEntries);
}
} // namespace yk::editor
