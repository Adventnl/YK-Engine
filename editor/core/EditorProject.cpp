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

Status EditorProject::applyToPrefab(const EditorDocument &document, EntityId root) {
    const Entity *entity = document.scene().find(root);
    if (!entity || entity->prefabSource().empty())
        return Error{"That entity is not a prefab instance"};
    const std::string source = entity->prefabSource();
    auto absolute = project_.resolve(source);
    if (!absolute)
        return Error{absolute.error()};
    Json prefab = subtreeToJson(document.scene(), root);
    if (Json *entities = prefab.find("entities"))
        for (std::size_t i = 0; i < entities->size(); ++i)
            if (entities->at(i).get("id").asString() == toString(root)) {
                Json &record = entities->at(i);
                record.erase("prefab"); // The prefab is not an instance of anything.
                if (Json *transform = record.find("transform")) {
                    Json position = Json::array();
                    position.push(0.0);
                    position.push(0.0);
                    transform->set("position", position);
                }
            }
    if (auto written = writeTextFileAtomic(absolute.value(), prefab.dump(2) + "\n"); !written)
        return written;
    refresh();
    log(LogLevel::Info, "editor", "Applied the instance to " + source);
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

bool EditorProject::importable(const std::filesystem::path &file) {
    const AssetKind kind = classifyAsset(file.generic_string());
    return kind == AssetKind::Texture || kind == AssetKind::Sound;
}

std::string EditorProject::defaultImportFolder(const std::filesystem::path &file) {
    return classifyAsset(file.generic_string()) == AssetKind::Sound ? "assets/audio"
                                                                    : "assets/textures";
}

Result<EditorProject::ImportResult>
EditorProject::importFiles(const std::vector<std::filesystem::path> &files,
                           const std::string &folder) {
    ImportResult result;
    std::error_code error;
    for (const std::filesystem::path &source : files) {
        const std::string name = source.filename().string();
        if (!std::filesystem::is_regular_file(source, error)) {
            result.skipped.push_back(name + ": not a file");
            continue;
        }
        if (!importable(source)) {
            result.skipped.push_back(name + ": only .png, .bmp and .wav files can be imported");
            continue;
        }
        // Never copy a file onto itself (it is already in the project).
        if (project_.relativize(source))
            if (std::filesystem::equivalent(source.parent_path(),
                                            project_.root / std::filesystem::path(folder), error)) {
                result.skipped.push_back(name + ": already in that folder");
                continue;
            }
        const std::string target = folder.empty() ? defaultImportFolder(source) : folder;
        auto directory = project_.resolve(target);
        if (!directory)
            return Error{directory.error()};
        std::filesystem::create_directories(directory.value(), error);
        if (error)
            return Error{"Cannot create '" + target + "': " + error.message()};
        // A free name: "hero.png", then "hero (1).png", "hero (2).png", ...
        std::filesystem::path destination = directory.value() / source.filename();
        for (int copy = 1; std::filesystem::exists(destination, error); ++copy)
            destination =
                directory.value() / (source.stem().string() + " (" + std::to_string(copy) + ")" +
                                     source.extension().string());
        std::filesystem::copy_file(source, destination, error);
        if (error)
            return Error{"Cannot copy '" + source.string() + "': " + error.message()};
        if (const auto relative = project_.relativize(destination))
            result.imported.push_back(*relative);
    }
    refresh();
    return result;
}

Result<ExportReport> EditorProject::exportGame(const ExportOptions &options) const {
    auto report = yk::exportGame(project_, *registry_, options);
    if (report)
        log(LogLevel::Info, "editor", "Exported the game to " + report.value().output.string());
    return report;
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
