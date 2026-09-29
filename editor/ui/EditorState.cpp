#include "ui/EditorState.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <SDL3/SDL.h>
#include <algorithm>

namespace yk::editor {
std::filesystem::path defaultSettingsDirectory() {
    if (char *path = SDL_GetPrefPath("YKEngine", "Editor")) {
        std::filesystem::path result(path);
        SDL_free(path);
        return result;
    }
    return std::filesystem::temp_directory_path() / "yk-editor";
}

void DirectoryBrowser::go(const std::filesystem::path &directory) {
    error.clear();
    folders.clear();
    hasProject = false;
    std::error_code failure;
    std::filesystem::path target = std::filesystem::absolute(directory, failure).lexically_normal();
    if (target.filename().empty() && target != target.root_path())
        target = target.parent_path(); // "dir/" -> "dir"
    if (!std::filesystem::is_directory(target, failure)) {
        error = "'" + target.string() + "' is not a folder";
        return;
    }
    current = target;
    pathText = current.string();
    hasProject = std::filesystem::exists(current / Project::fileName, failure);
    for (std::filesystem::directory_iterator
             it(current, std::filesystem::directory_options::skip_permission_denied, failure),
         end;
         !failure && it != end; it.increment(failure)) {
        std::error_code entryError;
        if (!it->is_directory(entryError))
            continue;
        const std::string name = it->path().filename().string();
        if (name.starts_with("."))
            continue;
        folders.push_back(
            {name, std::filesystem::exists(it->path() / Project::fileName, entryError)});
    }
    std::sort(folders.begin(), folders.end(),
              [](const Entry &a, const Entry &b) { return a.name < b.name; });
}

EditorState::EditorState(const ComponentRegistry &registryRef, EditorOptions optionsValue)
    : registry(registryRef), options(std::move(optionsValue)),
      settingsDirectory(options.settingsDirectory.empty() ? defaultSettingsDirectory()
                                                          : options.settingsDirectory),
      recent(settingsDirectory / "recent.json") {
    std::error_code error;
    std::filesystem::create_directories(settingsDirectory, error);
    recent.load();
    if (options.persistLayout) {
        if (auto text = readTextFile(settingsDirectory / "workbench.json"))
            if (auto json = Json::parse(text.value()))
                layout = WorkbenchLayout::fromJson(json.value());
    }
    savedLayout = layout;
}

Status EditorState::initialize(Renderer &rendererRef) {
    renderer = &rendererRef;
    auto created = SceneRenderer::create(*renderer, nullptr);
    if (!created)
        return Error{created.error()};
    sceneRenderer = std::move(created.value());
    if (!options.project.empty()) {
        // A project that cannot be opened at start-up (moved, damaged) is reported and the welcome
        // screen stays: the editor is a window, and quitting would only make it vanish.
        if (auto opened = openProject(options.project); !opened) {
            message("Cannot open the project", opened.error());
            return success();
        }
        if (!options.scene.empty() && (!document || document->path() != options.scene))
            if (auto scene = openScene(options.scene); !scene)
                message("Cannot open the scene", scene.error());
    }
    return success();
}

const Scene *EditorState::visibleScene() const {
    if (play)
        return &play->runtime().scene();
    return document ? &document->scene() : nullptr;
}

EntityId EditorState::inspected() const {
    if (play)
        return playSelection;
    return document ? document->primary() : EntityId{};
}

void EditorState::inspect(EntityId id, SelectMode mode) {
    if (play)
        playSelection = id;
    else if (document)
        document->select(id, mode);
}

void EditorState::bindDocument() {
    interaction.bind(document.get());
    frameRequested = true;
    renaming = {};
    hierarchyFilter.clear();
}

Status EditorState::attachProject(std::unique_ptr<EditorProject> opened) {
    stopPlay();
    interaction.bind(nullptr);
    document.reset();
    background.clear();
    sceneTabs.clear();
    problems.clear();
    problemsChecked = false;
    selectedAsset.clear();
    project = std::move(opened);
    auto renderers =
        SceneRenderer::create(*renderer, &project->assets(), project->project().textures);
    if (!renderers) {
        project.reset();
        return Error{renderers.error()};
    }
    sceneRenderer = std::move(renderers.value());
    if (options.audio)
        audio = SdlAudio::create(&project->assets());
    recent.add(project->project().root);
    if (auto saved = recent.save(); !saved)
        log(LogLevel::Warning, "editor", "Could not remember this project: " + saved.error());
    log(LogLevel::Info, "editor",
        "Opened project '" + project->project().name + "' at " + project->project().root.string());
    const std::string &start = project->project().startScene;
    if (!start.empty()) {
        std::error_code error;
        if (std::filesystem::exists(project->project().root / start, error))
            openScene(start); // A broken start scene is reported; the project stays open.
        else
            log(LogLevel::Warning, "editor", "Start scene '" + start + "' does not exist");
    }
    refreshProblems();
    return success();
}

Status EditorState::createProject(const std::filesystem::path &directory, const std::string &name) {
    auto created = EditorProject::create(directory, name, registry);
    if (!created) {
        log(LogLevel::Error, "editor", created.error());
        return Error{created.error()};
    }
    return attachProject(std::move(created.value()));
}

Status EditorState::openProject(const std::filesystem::path &fileOrDirectory) {
    auto opened = EditorProject::open(fileOrDirectory, registry);
    if (!opened) {
        log(LogLevel::Error, "editor", opened.error());
        return Error{opened.error()};
    }
    return attachProject(std::move(opened.value()));
}

void EditorState::closeProject() {
    stopPlay();
    interaction.bind(nullptr);
    document.reset();
    background.clear();
    sceneTabs.clear();
    problems.clear();
    problemsChecked = false;
    selectedAsset.clear();
    project.reset();
    audio.reset();
    if (auto created = SceneRenderer::create(*renderer, nullptr))
        sceneRenderer = std::move(created.value());
}

// Moves the edited scene (if any) to the background so another one can take its place.
void EditorState::stashActive() {
    if (!document)
        return;
    const std::string key = document->path(); // Read it before the document is moved away.
    BackgroundScene entry{std::move(document), interaction.camera};
    background[key] = std::move(entry);
}

Status EditorState::openScene(const std::string &path) {
    if (!project)
        return Error{"No project is open"};
    stopPlay();
    if (document && document->path() == path) {
        editorFocus = EditorFocus::Scene;
        return success();
    }
    if (background.contains(path))
        return activateScene(path);
    auto opened = project->openScene(path);
    if (!opened) {
        log(LogLevel::Error, "editor", opened.error());
        return Error{opened.error()};
    }
    stashActive();
    interaction.bind(nullptr);
    document = std::move(opened.value());
    bindDocument();
    if (std::find(sceneTabs.begin(), sceneTabs.end(), path) == sceneTabs.end())
        sceneTabs.push_back(path);
    editorFocus = EditorFocus::Scene;
    log(LogLevel::Info, "editor", "Opened scene " + path);
    return success();
}

Status EditorState::activateScene(const std::string &path) {
    if (document && document->path() == path) {
        editorFocus = EditorFocus::Scene;
        return success();
    }
    const auto found = background.find(path);
    if (found == background.end())
        return Error{"'" + path + "' is not open"};
    stopPlay();
    BackgroundScene next = std::move(found->second);
    background.erase(found);
    stashActive();
    interaction.bind(nullptr);
    document = std::move(next.document);
    bindDocument();
    interaction.camera = next.camera; // bind() asked for a fresh framing; the tab keeps its view.
    frameRequested = false;
    editorFocus = EditorFocus::Scene;
    return success();
}

void EditorState::closeScene(const std::string &path) {
    const auto removeNow = [this, path] {
        const auto tab = std::find(sceneTabs.begin(), sceneTabs.end(), path);
        const std::size_t index =
            tab == sceneTabs.end() ? 0 : static_cast<std::size_t>(tab - sceneTabs.begin());
        if (tab != sceneTabs.end())
            sceneTabs.erase(tab);
        if (document && document->path() == path) {
            stopPlay();
            interaction.bind(nullptr);
            document.reset();
            if (!sceneTabs.empty())
                activateScene(sceneTabs[std::min(index, sceneTabs.size() - 1)]);
        } else {
            background.erase(path);
        }
    };
    const EditorDocument *target = nullptr;
    if (document && document->path() == path)
        target = document.get();
    else if (const auto found = background.find(path); found != background.end())
        target = found->second.document.get();
    if (target && target->dirty() && !(playing() && target == document.get())) {
        dialog = {};
        dialog.kind = DialogKind::Unsaved;
        dialog.title = "Unsaved Changes";
        dialog.message = "'" + target->displayName() + "' has changes that are not saved.";
        dialog.unsavedScenes = {path};
        dialog.continuation = removeNow;
        dialog.needsOpen = true;
        return;
    }
    removeNow();
}

bool EditorState::anyDirty() const {
    return !dirtyScenes().empty();
}

std::vector<std::string> EditorState::dirtyScenes() const {
    std::vector<std::string> names;
    for (const std::string &path : sceneTabs) {
        const EditorDocument *doc = nullptr;
        if (document && document->path() == path)
            doc = document.get();
        else if (const auto found = background.find(path); found != background.end())
            doc = found->second.document.get();
        if (doc && doc->dirty())
            names.push_back(path);
    }
    return names;
}

Status EditorState::newScene(const std::string &path) {
    if (!project)
        return Error{"No project is open"};
    stopPlay();
    auto created = project->newScene(path);
    if (!created) {
        log(LogLevel::Error, "editor", created.error());
        return Error{created.error()};
    }
    stashActive();
    interaction.bind(nullptr);
    document = std::move(created.value());
    bindDocument();
    if (std::find(sceneTabs.begin(), sceneTabs.end(), document->path()) == sceneTabs.end())
        sceneTabs.push_back(document->path());
    editorFocus = EditorFocus::Scene;
    log(LogLevel::Info, "editor", "Created scene " + document->path());
    return success();
}

Status EditorState::saveScene() {
    if (!project || !document)
        return Error{"There is no scene to save"};
    if (document->path().empty()) {
        dialog = {};
        dialog.kind = DialogKind::SaveSceneAs;
        dialog.title = "Save Scene As";
        dialog.text = "scenes/";
        dialog.needsOpen = true;
        return success();
    }
    auto saved = project->saveScene(*document);
    if (!saved) {
        log(LogLevel::Error, "editor", saved.error());
        message("Could not save the scene", saved.error());
    } else {
        refreshProblems();
    }
    return saved;
}

Status EditorState::saveAll() {
    return saveScenes(dirtyScenes());
}

Status EditorState::saveScenes(const std::vector<std::string> &paths) {
    if (!project)
        return Error{"No project is open"};
    for (const std::string &path : paths) {
        EditorDocument *doc = document && document->path() == path ? document.get() : nullptr;
        if (!doc)
            if (const auto found = background.find(path); found != background.end())
                doc = found->second.document.get();
        if (!doc || !doc->dirty())
            continue;
        if (auto saved = project->saveScene(*doc); !saved) {
            log(LogLevel::Error, "editor", saved.error());
            return saved;
        }
    }
    refreshProblems();
    return success();
}

Status EditorState::saveSceneAs(const std::string &path) {
    if (!project || !document)
        return Error{"There is no scene to save"};
    const std::string before = document->path();
    auto saved = project->saveSceneAs(*document, path);
    if (!saved) {
        log(LogLevel::Error, "editor", saved.error());
        return saved;
    }
    const std::string after = document->path();
    for (std::string &tab : sceneTabs)
        if (tab == before)
            tab = after;
    return saved;
}

Status EditorState::savePrefab(EntityId entity, const std::string &path) {
    if (!project || !document)
        return Error{"There is no scene open"};
    auto saved = project->savePrefab(*document, entity, path);
    if (!saved)
        log(LogLevel::Error, "editor", saved.error());
    return saved;
}

Status EditorState::instantiatePrefab(const std::string &path, Vec2 world) {
    if (!project || !document)
        return Error{"There is no scene open"};
    auto prefab = project->loadPrefab(path);
    if (!prefab) {
        log(LogLevel::Error, "editor", prefab.error());
        return Error{prefab.error()};
    }
    auto placed = document->instantiatePrefab(prefab.value(), world, {}, path);
    if (!placed) {
        log(LogLevel::Error, "editor", placed.error());
        return Error{placed.error()};
    }
    return success();
}

void EditorState::refreshProblems() {
    if (!project)
        return;
    problems = project->validate();
    problemsChecked = true;
}

void EditorState::validateProject() {
    if (!project)
        return;
    refreshProblems();
    std::size_t errors = 0;
    for (const ProjectIssue &issue : problems)
        errors += issue.severity == ProjectIssue::Severity::Error ? 1 : 0;
    log(LogLevel::Info, "editor",
        "Validated project: " + std::to_string(errors) + " error(s), " +
            std::to_string(problems.size() - errors) + " warning(s)");
    layout.panelView = PanelView::Problems;
    layout.panelVisible = true;
}

void EditorState::addOutput(LogLevel level, const std::string &text) {
    output.push_back({level, text});
    if (output.size() > 2000)
        output.erase(output.begin(), output.begin() + 500);
}

std::filesystem::path EditorState::executableDirectory() const {
    if (const char *path = SDL_GetBasePath())
        return path;
    return ".";
}

std::optional<std::filesystem::path> EditorState::playerFor(BuildTarget target) const {
    return findPlayer(target, executableDirectory());
}

std::filesystem::path EditorState::playerExecutable() const {
    if (const auto found = playerFor(hostTarget()))
        return *found;
    return executableDirectory() / playerFileName(hostTarget());
}

Result<ExportReport> EditorState::exportGame(ExportOptions request) {
    if (!project)
        return Error{"No project is open"};
    if (request.notices.empty())
        if (const auto notices = findNotices(executableDirectory()))
            request.notices = *notices;
    request.progress = [this](const std::string &line) {
        addOutput(LogLevel::Info, line);
        log(LogLevel::Info, "export", line);
    };
    layout.panelView = PanelView::Output;
    layout.panelVisible = true;
    addOutput(LogLevel::Info,
              std::string("--- Export for ") + displayName(request.target) + " ---");
    auto exported = project->exportGame(request);
    if (!exported) {
        addOutput(LogLevel::Error, exported.error());
        log(LogLevel::Error, "export", exported.error());
        return Error{exported.error()};
    }
    for (const std::string &warning : exported.value().warnings) {
        addOutput(LogLevel::Warning, warning);
        log(LogLevel::Warning, "export", warning);
    }
    addOutput(LogLevel::Info, std::to_string(exported.value().files) + " files, " +
                                  std::to_string(exported.value().bytes / 1024) +
                                  " KB of game data");
    return exported;
}

void EditorState::guarded(std::function<void()> action) {
    if (anyDirty() && !playing()) {
        dialog = {};
        dialog.kind = DialogKind::Unsaved;
        dialog.title = "Unsaved Changes";
        dialog.unsavedScenes = dirtyScenes();
        if (dialog.unsavedScenes.size() == 1) {
            dialog.message =
                "'" + dialog.unsavedScenes.front() + "' has changes that are not saved.";
        } else {
            dialog.message = std::to_string(dialog.unsavedScenes.size()) +
                             " scenes have changes that are not saved:";
            for (const std::string &path : dialog.unsavedScenes)
                dialog.message += "\n   " + path;
        }
        dialog.continuation = std::move(action);
        dialog.needsOpen = true;
        return;
    }
    action();
}

void EditorState::startPlay() {
    if (play || !document || !project || document->inChange())
        return;
    Vec2 viewport = gameView.size * gameView.scale;
    if (viewport.x < 16.0F || viewport.y < 16.0F)
        viewport = {static_cast<float>(project->project().window.width),
                    static_cast<float>(project->project().window.height)};
    auto session = PlaySession::start(*document, *project, audio.get(), viewport);
    if (!session) {
        log(LogLevel::Error, "editor", session.error());
        message("Cannot play the scene", session.error());
        return;
    }
    play = std::move(session.value());
    playSelection = {};
    editorFocus = EditorFocus::Game;
    focusGame = true;
    log(LogLevel::Info, "editor", "Play: " + document->displayName());
}

void EditorState::stopPlay() {
    if (!play)
        return;
    play.reset();
    if (audio)
        audio->stopAll();
    playSelection = {};
    editorFocus = EditorFocus::Scene;
    log(LogLevel::Info, "editor", "Stopped; back to editing");
}

void EditorState::togglePause() {
    if (play)
        play->setPaused(!play->paused());
}

void EditorState::stepPlay(const InputFrame &input) {
    if (play)
        play->step(input);
}

void EditorState::restartPlay() {
    if (!play)
        return;
    if (auto restarted = play->restart(); !restarted)
        log(LogLevel::Error, "editor", restarted.error());
}

void EditorState::message(std::string title, std::string body) {
    dialog = {};
    dialog.kind = DialogKind::Message;
    dialog.title = std::move(title);
    dialog.message = std::move(body);
    dialog.needsOpen = true;
}

void EditorState::requestQuit() {
    guarded([this] { quit = true; });
}

std::vector<std::string> EditorState::scenePaths() const {
    std::vector<std::string> paths;
    if (project)
        for (const AssetEntry &entry : project->files())
            if (entry.kind == AssetKind::Scene)
                paths.push_back(entry.path);
    return paths;
}

std::vector<std::string> EditorState::prefabPaths() const {
    std::vector<std::string> paths;
    if (project)
        for (const AssetEntry &entry : project->files())
            if (entry.kind == AssetKind::Prefab)
                paths.push_back(entry.path);
    return paths;
}

std::string EditorState::windowTitle() const {
    std::string title = "YK Editor";
    if (project)
        title += " - " + project->project().name;
    if (document)
        title += " - " + (document->path().empty() ? std::string("Untitled") : document->path()) +
                 (document->dirty() ? "*" : "");
    if (play)
        title += " [Playing]";
    return title;
}

void EditorState::tick(double seconds, const InputFrame &gameInput) {
    if (play)
        play->update(seconds, gameInput);
    if (audio)
        audio->update();
    std::vector<std::filesystem::path> imports;
    {
        const std::lock_guard<std::mutex> lock(importMutex_);
        imports.swap(importQueue_);
    }
    if (!imports.empty())
        importAssets(imports, explorerFolder);
}

void EditorState::queueImport(std::vector<std::filesystem::path> files) {
    const std::lock_guard<std::mutex> lock(importMutex_);
    importQueue_.insert(importQueue_.end(), files.begin(), files.end());
}

namespace {
// SDL calls this when the file dialog closes, possibly from another thread.
void onFilesChosen(void *userdata, const char *const *files, int) {
    auto *state = static_cast<EditorState *>(userdata);
    if (!files)
        return; // The dialog failed; SDL logs the reason.
    std::vector<std::filesystem::path> chosen;
    for (const char *const *file = files; *file; ++file)
        chosen.emplace_back(*file);
    if (!chosen.empty())
        state->queueImport(std::move(chosen));
}
} // namespace

void EditorState::chooseAssetsToImport(SDL_Window *window) {
    static const SDL_DialogFileFilter filters[] = {
        {"Images and sounds", "png;bmp;wav"}, {"Images", "png;bmp"}, {"Sounds", "wav"}};
    SDL_Window *parent = window ? window : SDL_GetKeyboardFocus();
    SDL_ShowOpenFileDialog(onFilesChosen, this, parent, filters, 3, nullptr, true);
}

Status EditorState::importAssets(const std::vector<std::filesystem::path> &files,
                                 const std::string &folder) {
    if (!project)
        return Error{"No project is open"};
    auto imported = project->importFiles(files, folder);
    if (!imported) {
        log(LogLevel::Error, "editor", imported.error());
        return Error{imported.error()};
    }
    for (const std::string &path : imported.value().imported) {
        log(LogLevel::Info, "editor", "Imported " + path);
        if (sceneRenderer)
            sceneRenderer->reload(*renderer, path);
    }
    for (const std::string &why : imported.value().skipped)
        log(LogLevel::Warning, "editor", "Skipped " + why);
    if (!imported.value().imported.empty())
        showAssetInExplorer(imported.value().imported.front());
    return success();
}

Status EditorState::revertPrefab(EntityId entity) {
    if (!project || !document || playing())
        return Error{"There is no scene to edit"};
    const EntityId root = document->prefabRootOf(entity);
    if (!root)
        return Error{"That entity is not part of a prefab instance"};
    const std::string source = document->scene().find(root)->prefabSource();
    auto prefab = project->loadPrefab(source);
    if (!prefab) {
        log(LogLevel::Error, "editor", prefab.error());
        message("Cannot revert", prefab.error());
        return Error{prefab.error()};
    }
    auto reverted = document->revertToPrefab(root, prefab.value());
    if (!reverted) {
        log(LogLevel::Error, "editor", reverted.error());
        return reverted;
    }
    log(LogLevel::Info, "editor",
        "Reverted '" + document->scene().find(root)->name() + "' to " + source);
    return success();
}

namespace {
// How many other instances of `source` the open scenes hold (`except` is the one being acted on).
std::size_t otherInstances(const EditorState &state, const std::string &source, EntityId except) {
    std::size_t count = 0;
    const auto scan = [&](const EditorDocument &document, EntityId skip) {
        document.scene().forEach([&](const Entity &entity) {
            if (entity.prefabSource() == source && entity.id() != skip)
                ++count;
        });
    };
    if (state.document)
        scan(*state.document, except);
    for (const auto &entry : state.background)
        if (entry.second.document)
            scan(*entry.second.document, {});
    return count;
}
} // namespace

Status EditorState::applyPrefab(EntityId entity) {
    if (!project || !document || playing())
        return Error{"There is no scene to edit"};
    const EntityId root = document->prefabRootOf(entity);
    if (!root)
        return Error{"That entity is not part of a prefab instance"};
    const std::string source = document->scene().find(root)->prefabSource();
    if (auto written = project->applyToPrefab(*document, root); !written) {
        log(LogLevel::Error, "editor", written.error());
        message("Cannot apply to the prefab", written.error());
        return written;
    }
    std::string text = "Applied '" + document->scene().find(root)->name() + "' to " + source;
    if (const std::size_t others = otherInstances(*this, source, root); others > 0)
        text += ". " + std::to_string(others) +
                " other instance(s) in the open scenes still have the old contents (Entity > "
                "Prefab > Update Other Instances brings them in line)";
    log(LogLevel::Info, "editor", text);
    refreshProblems();
    return success();
}

void EditorState::updateOtherInstances(EntityId entity) {
    if (!project || !document || playing())
        return;
    const EntityId root = document->prefabRootOf(entity);
    if (!root)
        return;
    const std::string source = document->scene().find(root)->prefabSource();
    const std::size_t others = otherInstances(*this, source, root);
    if (others == 0) {
        log(LogLevel::Info, "editor",
            "There are no other instances of " + source + " in the open scenes");
        return;
    }
    dialog = {};
    dialog.kind = DialogKind::Confirm;
    dialog.title = "Update Other Instances";
    dialog.message = "This puts " + std::to_string(others) + " other instance(s) of\n" + source +
                     "\nback to the prefab file's contents. Their names and placement stay; "
                     "any other changes made to them are replaced.";
    dialog.confirmLabel = "Update";
    dialog.needsOpen = true;
    dialog.continuation = [this, source, root] {
        auto prefab = project ? project->loadPrefab(source) : Result<Json>(Error{"No project"});
        if (!prefab) {
            log(LogLevel::Error, "editor", prefab.error());
            return;
        }
        std::size_t updated = 0;
        const auto update = [&](EditorDocument &target, EntityId except) {
            if (auto done = target.updatePrefabInstances(source, prefab.value(), except); done)
                updated += done.value();
            else
                log(LogLevel::Error, "editor", done.error());
        };
        if (document)
            update(*document, root);
        for (auto &entry : background)
            if (entry.second.document)
                update(*entry.second.document, {});
        log(LogLevel::Info, "editor",
            "Updated " + std::to_string(updated) + " other instance(s) of " + source);
    };
}

void EditorState::unpackPrefab(EntityId entity) {
    if (!document || playing())
        return;
    if (const EntityId root = document->prefabRootOf(entity)) {
        document->unpackPrefab(root);
        log(LogLevel::Info, "editor", "Unpacked '" + document->scene().find(root)->name() + "'");
    }
}

void EditorState::showAssetInExplorer(const std::string &path) {
    selectedAsset = path;
    explorerReveal = path;
    explorerFolder = std::filesystem::path(path).parent_path().generic_string();
    assetSelectionMark.clear();
    if (document)
        for (const EntityId id : document->selection())
            assetSelectionMark.push_back(id);
    layout.sideView = SideView::Explorer;
    layout.sideBarVisible = true;
}
} // namespace yk::editor
