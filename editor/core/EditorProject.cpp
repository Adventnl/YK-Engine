#include "core/EditorProject.hpp"
#include "yk/assets/TextureMeta.hpp"
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

// "assets\\a.png " -> "assets/a.png"; refuses what cannot be a name inside the project.
Result<std::string> cleanAssetPath(std::string text) {
    std::replace(text.begin(), text.end(), '\\', '/');
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return Error{"Enter a path inside the project"};
    text = text.substr(first, text.find_last_not_of(" \t") - first + 1);
    while (text.starts_with("./"))
        text.erase(0, 2);
    while (!text.empty() && text.back() == '/')
        text.pop_back();
    if (text.empty())
        return Error{"Enter a path inside the project"};
    if (text.front() == '/')
        return Error{"Use a path inside the project, without a leading slash"};
    std::string clean;
    std::size_t start = 0;
    while (true) {
        const auto slash = text.find('/', start);
        const std::string part =
            text.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part.empty())
            return Error{"'" + text + "' has an empty folder name"};
        if (part == "." || part == "..")
            return Error{"'" + text + "' must stay inside the project"};
        if (part.front() == '.')
            return Error{"Names cannot start with a dot: the project hides those files"};
        if (part.find_first_of(":*?\"<>|") != std::string::npos ||
            std::any_of(part.begin(), part.end(),
                        [](char c) { return static_cast<unsigned char>(c) < 0x20; }))
            return Error{"Names cannot contain : * ? \" < > |"};
        if (part.back() == ' ' || part.back() == '.')
            return Error{"Names cannot end with a space or a dot"};
        if (!clean.empty())
            clean += '/';
        clean += part;
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    return clean;
}

// The files that name other files: everything a scene, prefab or animation can point at.
bool isDataFile(AssetKind kind) {
    return kind == AssetKind::Scene || kind == AssetKind::Prefab || kind == AssetKind::Animation ||
           kind == AssetKind::Controller || kind == AssetKind::Dialogue;
}

// Does `value` name `from` (a file), or something inside it (a folder)?
bool names(const std::string &value, const std::string &from, bool folder) {
    return value == from || (folder && value.size() > from.size() && value.starts_with(from) &&
                             value[from.size()] == '/');
}

// What `value` becomes when `from` moves to `to`.
std::optional<std::string> remapped(const std::string &value, const std::string &from,
                                    const std::string &to, bool folder) {
    if (!names(value, from, folder))
        return std::nullopt;
    return to + value.substr(from.size());
}

std::size_t countReferences(const Json &node, const std::string &from, bool folder) {
    if (node.isString())
        return names(node.asString(), from, folder) ? 1 : 0;
    std::size_t count = 0;
    if (node.isArray()) {
        for (const Json &item : node.items())
            count += countReferences(item, from, folder);
    } else if (node.isObject()) {
        for (std::size_t i = 0; i < node.size(); ++i)
            count += countReferences(node.valueAt(i), from, folder);
    }
    return count;
}

std::size_t rewriteReferences(Json &node, const std::string &from, const std::string &to,
                              bool folder) {
    if (node.isString()) {
        if (const auto replaced = remapped(node.asString(), from, to, folder)) {
            node = Json(*replaced);
            return 1;
        }
        return 0;
    }
    std::size_t count = 0;
    if (node.isArray()) {
        for (std::size_t i = 0; i < node.size(); ++i)
            count += rewriteReferences(node.at(i), from, to, folder);
    } else if (node.isObject()) {
        for (std::size_t i = 0; i < node.size(); ++i) {
            const std::string key = node.keyAt(i);
            if (Json *child = node.find(key))
                count += rewriteReferences(*child, from, to, folder);
        }
    }
    return count;
}

// A parsed data file of the project, or nothing when it cannot be read (validation reports those).
std::optional<Json> readData(const Project &project, const std::string &path) {
    const auto resolved = project.resolve(path);
    if (!resolved)
        return std::nullopt;
    auto text = readTextFile(resolved.value());
    if (!text)
        return std::nullopt;
    auto json = Json::parse(text.value());
    if (!json)
        return std::nullopt;
    return std::move(json.value());
}
} // namespace

EditorProject::EditorProject(Project project, const ComponentRegistry &registry)
    : project_(std::move(project)), registry_(&registry), assets_(project_) {}

Result<std::unique_ptr<EditorProject>> EditorProject::create(const std::filesystem::path &directory,
                                                             std::string name,
                                                             const ComponentRegistry &registry) {
    auto project = yk::createProject(directory, name, registry);
    if (!project)
        return Error{project.error()};
    std::unique_ptr<EditorProject> created(new EditorProject(std::move(project.value()), registry));
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

EditorProject::AssetUsage EditorProject::usageOf(const std::string &pathInput) const {
    AssetUsage usage;
    const auto cleaned = cleanAssetPath(pathInput);
    if (!cleaned)
        return usage;
    const std::string &path = cleaned.value();
    std::error_code error;
    const auto absolute = project_.resolve(path);
    const bool folder = absolute && std::filesystem::is_directory(absolute.value(), error);
    for (const AssetEntry &entry : scanAssets(project_)) {
        if (!isDataFile(entry.kind))
            continue;
        if (const auto json = readData(project_, entry.path))
            if (const std::size_t count = countReferences(*json, path, folder); count > 0) {
                usage.references += count;
                usage.files.push_back(entry.path);
            }
    }
    if (names(project_.startScene, path, folder)) {
        ++usage.references;
        usage.files.push_back(Project::fileName);
    }
    std::sort(usage.files.begin(), usage.files.end());
    return usage;
}

Status EditorProject::checkMove(const std::string &fromInput, const std::string &toInput) const {
    const auto cleanFrom = cleanAssetPath(fromInput);
    if (!cleanFrom)
        return Error{cleanFrom.error()};
    const auto cleanTo = cleanAssetPath(toInput);
    if (!cleanTo)
        return Error{cleanTo.error()};
    const std::string &from = cleanFrom.value();
    const std::string &to = cleanTo.value();
    if (from == to)
        return Error{"That is where it already is"};
    if (from == Project::fileName || to == Project::fileName)
        return Error{std::string(Project::fileName) +
                     " is the project itself: it cannot be renamed, moved or replaced"};
    const auto fromAbsolute = project_.resolve(from);
    const auto toAbsolute = project_.resolve(to);
    if (!fromAbsolute)
        return Error{fromAbsolute.error()};
    if (!toAbsolute)
        return Error{toAbsolute.error()};
    std::error_code error;
    if (!std::filesystem::exists(fromAbsolute.value(), error))
        return Error{"'" + from + "' does not exist"};
    const bool folder = std::filesystem::is_directory(fromAbsolute.value(), error);
    if (!folder && classifyAsset(from) == AssetKind::TextureMeta)
        return Error{"Import settings belong to their picture: rename the picture and they follow"};
    if (folder && (to + "/").starts_with(from + "/"))
        return Error{"A folder cannot be moved into itself"};
    // Every folder on the way must be a folder (or not exist yet).
    for (std::filesystem::path parent = toAbsolute.value().parent_path();
         parent != project_.root && parent.has_relative_path(); parent = parent.parent_path())
        if (std::filesystem::is_regular_file(parent, error))
            return Error{"'" + project_.relativize(parent).value_or(parent.string()) +
                         "' is a file, not a folder"};
    // A picture's import settings travel with it and must not land on other settings.
    const bool exists = std::filesystem::exists(toAbsolute.value(), error);
    const bool sameFile =
        exists && std::filesystem::equivalent(fromAbsolute.value(), toAbsolute.value(), error);
    if (exists && !sameFile)
        return Error{"'" + to + "' already exists"};
    if (!folder && classifyAsset(from) == AssetKind::Texture) {
        const auto sidecarFrom = project_.resolve(TextureMeta::sidecarPath(from));
        const auto sidecarTo = project_.resolve(TextureMeta::sidecarPath(to));
        if (sidecarFrom && sidecarTo && std::filesystem::exists(sidecarFrom.value(), error) &&
            std::filesystem::exists(sidecarTo.value(), error) &&
            !std::filesystem::equivalent(sidecarFrom.value(), sidecarTo.value(), error))
            return Error{"'" + TextureMeta::sidecarPath(to) + "' already exists"};
    }
    return success();
}

Result<EditorProject::MoveResult> EditorProject::moveAsset(const std::string &fromInput,
                                                           const std::string &toInput) {
    if (auto checked = checkMove(fromInput, toInput); !checked)
        return Error{checked.error()};
    MoveResult result;
    result.from = cleanAssetPath(fromInput).value();
    result.to = cleanAssetPath(toInput).value();
    const std::string &from = result.from;
    const std::string &to = result.to;
    const std::filesystem::path fromAbsolute = project_.resolve(from).value();
    const std::filesystem::path toAbsolute = project_.resolve(to).value();
    std::error_code error;
    const bool folder = std::filesystem::is_directory(fromAbsolute, error);
    std::filesystem::path sidecarFrom, sidecarTo;
    if (!folder && classifyAsset(from) == AssetKind::Texture) {
        sidecarFrom = project_.resolve(TextureMeta::sidecarPath(from)).value();
        sidecarTo = project_.resolve(TextureMeta::sidecarPath(to)).value();
        if (!std::filesystem::exists(sidecarFrom, error))
            sidecarFrom.clear();
    }

    // First work out every rewrite, so that nothing is touched unless all of it can be done.
    struct Rewrite {
        std::string path; // Where the data file is once the move is done.
        std::string text; // What it becomes.
        std::string original;
    };
    std::vector<Rewrite> rewrites;
    for (const AssetEntry &entry : scanAssets(project_)) {
        if (!isDataFile(entry.kind))
            continue;
        const auto resolved = project_.resolve(entry.path);
        auto text = resolved ? readTextFile(resolved.value()) : Result<std::string>(Error{"path"});
        if (!text)
            continue;
        auto json = Json::parse(text.value());
        if (!json)
            continue;
        const std::size_t changed = rewriteReferences(json.value(), from, to, folder);
        if (changed == 0)
            continue;
        result.references += changed;
        rewrites.push_back({remapped(entry.path, from, to, folder).value_or(entry.path),
                            json.value().dump(2) + "\n", std::move(text.value())});
    }

    // Then move. A name that differs only by case is the same file on some systems: go through a
    // temporary name there.
    std::filesystem::create_directories(toAbsolute.parent_path(), error);
    if (error)
        return Error{"Cannot create the folder for '" + to + "': " + error.message()};
    const auto rename = [](const std::filesystem::path &a, const std::filesystem::path &b) {
        std::error_code failure;
        std::filesystem::rename(a, b, failure);
        return failure;
    };
    const bool sameFile = std::filesystem::exists(toAbsolute, error) &&
                          std::filesystem::equivalent(fromAbsolute, toAbsolute, error);
    const auto moveFile = [&](const std::filesystem::path &a, const std::filesystem::path &b,
                              bool caseOnly) {
        if (!caseOnly)
            return rename(a, b);
        std::filesystem::path temporary = a;
        temporary += ".yk-moving";
        if (const auto failure = rename(a, temporary))
            return failure;
        return rename(temporary, b);
    };
    if (const auto failure = moveFile(fromAbsolute, toAbsolute, sameFile))
        return Error{"Cannot move '" + from + "': " + failure.message()};
    if (!sidecarFrom.empty())
        if (const auto failure = moveFile(sidecarFrom, sidecarTo, sameFile)) {
            rename(toAbsolute, fromAbsolute);
            return Error{"Cannot move the import settings of '" + from + "': " + failure.message()};
        }
    std::vector<const Rewrite *> written;
    const std::string previousStart = project_.startScene;
    const auto undo = [&] {
        for (const Rewrite *rewrite : written)
            if (const auto path = project_.resolve(rewrite->path))
                (void)writeTextFileAtomic(path.value(), rewrite->original);
        project_.startScene = previousStart;
        if (!sidecarFrom.empty())
            rename(sidecarTo, sidecarFrom);
        rename(toAbsolute, fromAbsolute);
    };

    // Finally rewrite the references, each file replaced whole.
    for (const Rewrite &rewrite : rewrites) {
        const auto path = project_.resolve(rewrite.path).value();
        if (auto status = writeTextFileAtomic(path, rewrite.text); !status) {
            undo();
            return Error{"Cannot update '" + rewrite.path + "': " + status.error() +
                         ". Nothing was moved."};
        }
        written.push_back(&rewrite);
        result.rewritten.push_back(rewrite.path);
    }
    if (const auto start = remapped(previousStart, from, to, folder)) {
        project_.startScene = *start;
        if (auto saved = project_.save(); !saved) {
            undo();
            return Error{"Cannot update the start scene: " + saved.error() +
                         ". Nothing was moved."};
        }
        ++result.references;
        result.rewritten.push_back(Project::fileName);
    }
    refresh();
    return result;
}

std::size_t EditorProject::filesUnder(const std::string &pathInput) const {
    const auto path = cleanAssetPath(pathInput);
    const auto absolute =
        path ? project_.resolve(path.value()) : Result<std::filesystem::path>(Error{""});
    std::error_code error;
    if (!absolute || !std::filesystem::exists(absolute.value(), error))
        return 0;
    if (!std::filesystem::is_directory(absolute.value(), error))
        return 1;
    std::size_t count = 0;
    for (std::filesystem::recursive_directory_iterator it(absolute.value(), error), end;
         !error && it != end; it.increment(error))
        count += it->is_regular_file(error) ? 1 : 0;
    return count;
}

Status EditorProject::deleteAsset(const std::string &pathInput) {
    const auto cleaned = cleanAssetPath(pathInput);
    if (!cleaned)
        return Error{cleaned.error()};
    const std::string &path = cleaned.value();
    if (path == Project::fileName)
        return Error{std::string(Project::fileName) +
                     " is the project itself and cannot be deleted"};
    const auto absolute = project_.resolve(path);
    if (!absolute)
        return Error{absolute.error()};
    std::error_code error;
    if (!std::filesystem::exists(absolute.value(), error))
        return Error{"'" + path + "' does not exist"};
    if (std::filesystem::is_directory(absolute.value(), error)) {
        std::filesystem::remove_all(absolute.value(), error);
    } else {
        std::filesystem::remove(absolute.value(), error);
        if (!error && classifyAsset(path) == AssetKind::Texture)
            if (const auto sidecar = project_.resolve(TextureMeta::sidecarPath(path)))
                std::filesystem::remove(sidecar.value(), error);
    }
    if (error)
        return Error{"Cannot delete '" + path + "': " + error.message()};
    refresh();
    return success();
}

EditorProject::ClosedInstances
EditorProject::closedInstancesOf(const std::string &source,
                                 const std::set<std::string> &open) const {
    ClosedInstances found;
    for (const AssetEntry &entry : scanAssets(project_)) {
        if (entry.kind != AssetKind::Scene || open.contains(entry.path))
            continue;
        const auto document = openScene(entry.path);
        if (!document)
            continue;
        std::size_t count = 0;
        document.value()->scene().forEach(
            [&](const Entity &entity) { count += entity.prefabSource() == source ? 1 : 0; });
        if (count > 0) {
            found.instances += count;
            found.scenes.push_back(entry.path);
        }
    }
    return found;
}

EditorProject::ClosedUpdate
EditorProject::updateClosedInstances(const std::string &source, const std::set<std::string> &open) {
    ClosedUpdate result;
    const auto prefab = loadPrefab(source);
    if (!prefab) {
        result.failures.push_back(source + ": " + prefab.error());
        return result;
    }
    for (const std::string &path : closedInstancesOf(source, open).scenes) {
        auto document = openScene(path);
        if (!document) {
            result.failures.push_back(path + ": " + document.error());
            continue;
        }
        const auto updated = document.value()->updatePrefabInstances(source, prefab.value(), {});
        if (!updated) {
            result.failures.push_back(path + ": " + updated.error());
            continue;
        }
        if (auto saved = saveScene(*document.value()); !saved) {
            result.failures.push_back(path + ": " + saved.error());
            continue;
        }
        result.updated += updated.value();
        result.scenes.push_back(path);
    }
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
