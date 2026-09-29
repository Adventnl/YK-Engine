#include "core/PlaySession.hpp"
#include "yk/core/Log.hpp"
#include "yk/scene/SceneSerializer.hpp"

namespace yk::editor {
Result<std::unique_ptr<PlaySession>> PlaySession::start(const EditorDocument &document,
                                                        const EditorProject &project,
                                                        AudioSink *audio, Vec2 viewport) {
    if (document.inChange())
        return Error{"Finish the current edit before playing"};
    // The copy goes through the same save format a real game load uses, so Play shows exactly what
    // the saved scene would do.
    auto copy = sceneFromJson(sceneToJson(document.scene()), project.registry());
    if (!copy)
        return Error{"Cannot copy the scene for play mode: " + copy.error()};
    std::unique_ptr<PlaySession> session(new PlaySession());
    session->project_ = &project;
    session->audio_ = audio;
    session->viewport_ = viewport;
    session->scenePath_ = document.path();
    if (auto status = session->load(std::move(copy.value())); !status)
        return Error{status.error()};
    return session;
}

Status PlaySession::load(std::unique_ptr<Scene> scene) {
    RuntimeOptions options;
    options.layers = project_->project().layers;
    options.viewportSize = viewport_;
    options.audio = audio_;
    options.assets = &project_->assets();
    auto runtime = GameRuntime::create(std::move(scene), options);
    if (!runtime)
        return Error{runtime.error()};
    runtime_ = std::move(runtime.value());
    runtime_->setPaused(paused_);
    return success();
}

Status PlaySession::followSceneChange() {
    const std::string next = runtime_->sceneChangeRequested();
    if (next.empty())
        return success();
    runtime_->clearSceneChangeRequest();
    auto absolute = project_->project().resolve(next);
    if (!absolute)
        return Error{absolute.error()};
    auto scene = loadScene(absolute.value(), project_->registry());
    if (!scene)
        return Error{scene.error()};
    if (auto status = load(std::move(scene.value())); !status)
        return status;
    scenePath_ = next;
    log(LogLevel::Info, "play", "Loaded scene " + next);
    return success();
}

void PlaySession::update(double seconds, const Keyboard &keyboard) {
    if (!paused_)
        runtime_->update(seconds, keyboard);
    if (auto status = followSceneChange(); !status)
        log(LogLevel::Error, "play", status.error()); // Keep playing the current scene.
}

void PlaySession::step(const Keyboard &keyboard) {
    if (!paused_)
        return;
    runtime_->setPaused(false);
    runtime_->stepOnce(keyboard);
    runtime_->setPaused(true);
}

void PlaySession::setPaused(bool paused) {
    paused_ = paused;
    runtime_->setPaused(paused);
}

Status PlaySession::restart() {
    return runtime_->restart();
}

void PlaySession::setViewportSize(Vec2 pixels) {
    viewport_ = pixels;
    runtime_->setViewportSize(pixels);
}
} // namespace yk::editor
