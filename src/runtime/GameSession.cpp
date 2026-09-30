#include "yk/runtime/GameSession.hpp"
#include "yk/core/Log.hpp"

namespace yk {
Result<std::unique_ptr<GameSession>> GameSession::create(std::unique_ptr<Scene> scene,
                                                         std::string path, RuntimeOptions options,
                                                         SceneLoader loader) {
    std::unique_ptr<GameSession> session(new GameSession());
    session->options_ = std::move(options);
    session->loader_ = std::move(loader);
    session->path_ = std::move(path);
    auto runtime = GameRuntime::create(std::move(scene), session->options_);
    if (!runtime)
        return Error{runtime.error()};
    session->runtime_ = std::move(runtime.value());
    return session;
}

bool GameSession::followSceneChange() {
    const std::string next = runtime_->sceneChangeRequested();
    if (next.empty())
        return false;
    // Load first: if it cannot be had, the game that is running is left as it was.
    auto scene =
        loader_ ? loader_(next) : Result<std::unique_ptr<Scene>>(Error{"no scene loader is set"});
    RuntimeOptions options = options_;
    options.viewportSize = runtime_->viewportSize();
    options.startCovered = true; // The new scene fades in from black (when transitions are on).
    options.variables = runtime_->blackboard().kept();
    auto runtime = scene ? GameRuntime::create(std::move(scene.value()), std::move(options))
                         : Result<std::unique_ptr<GameRuntime>>(Error{scene.error()});
    if (!runtime) {
        log(LogLevel::Error, "runtime", "Cannot go to scene '" + next + "': " + runtime.error());
        runtime_->clearSceneChangeRequest(); // Uncovers the screen: keep playing this scene.
        return false;
    }
    runtime_ = std::move(runtime.value());
    runtime_->setPaused(paused_);
    path_ = next;
    log(LogLevel::Info, "runtime", "Loaded scene " + next);
    return true;
}

bool GameSession::update(double seconds, const InputFrame &input) {
    if (!paused_)
        runtime_->update(seconds, input);
    return followSceneChange();
}

bool GameSession::step(const InputFrame &input) {
    if (!paused_)
        return false;
    runtime_->setPaused(false);
    runtime_->stepOnce(input);
    runtime_->setPaused(true);
    return followSceneChange();
}

void GameSession::setPaused(bool paused) {
    paused_ = paused;
    runtime_->setPaused(paused);
}

Status GameSession::restart() {
    return runtime_->restart();
}

void GameSession::setViewportSize(Vec2 pixels) {
    options_.viewportSize = pixels;
    runtime_->setViewportSize(pixels);
}
} // namespace yk
