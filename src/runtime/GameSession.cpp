#include "yk/runtime/GameSession.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"

namespace yk {
Result<std::unique_ptr<GameSession>> GameSession::create(std::unique_ptr<Scene> scene,
                                                         std::string path, RuntimeOptions options,
                                                         SceneLoader loader) {
    std::unique_ptr<GameSession> session(new GameSession());
    session->options_ = std::move(options);
    session->pauseInput_ = ActionInput(session->options_.inputMap);
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
    PauseMenu *menu = nullptr;
    for (const auto id : runtime_->scene().orderedIds()) {
        auto *entity = runtime_->scene().find(id);
        if (entity && entity->activeInHierarchy())
            if (auto *candidate = entity->get<PauseMenu>(); candidate && candidate->enabled) {
                menu = candidate;
                break;
            }
    }
    pauseInput_.update(input);
    const auto pressed = [&](const char *action) {
        return pauseInput_.state(options_.pauseSet, action).pressed;
    };
    bool toggled = false;
    if (!options_.pauseAction.empty() && (!menu || menu->allowPause)) {
        if (pressed(options_.pauseAction.c_str()) ||
            (menu && pressed("Cancel") && (paused_ || !runtime_->inputLocked()))) {
            setPaused(!paused_);
            toggled = true;
            if (menu) {
                menu->selected = 0;
                menu->armed = -1;
                menu->pointerPosition = {-1, -1};
            }
        }
    }
    if (toggled && menu && !paused_) {
        runtime_->update(seconds, InputFrame{});
        return followSceneChange();
    }
    if (paused_ && menu && !toggled) {
        if (pressed("PauseUp"))
            menu->selected = (menu->selected + 3) % 4;
        if (pressed("PauseDown"))
            menu->selected = (menu->selected + 1) % 4;
        int hovered = -1;
        for (int row = 0; row < 4; ++row) {
            const auto box = PauseMenu::buttonRect(runtime_->viewportSize(), row);
            const auto point = input.pointer.position;
            if (input.pointer.inside && point.x >= box.position.x && point.y >= box.position.y &&
                point.x < box.position.x + box.size.x && point.y < box.position.y + box.size.y)
                hovered = row;
        }
        if (hovered >= 0 &&
            (input.pointer.position != menu->pointerPosition || input.pointer.primary.pressed))
            menu->selected = hovered;
        menu->pointerPosition = input.pointer.position;
        if (input.pointer.primary.pressed)
            menu->armed = hovered;
        int chosen = pressed("Continue") ? menu->selected : -1;
        if (input.pointer.primary.released) {
            if (menu->armed >= 0 && menu->armed == hovered)
                chosen = hovered;
            menu->armed = -1;
        }
        if (pressed("Restart"))
            chosen = 1;
        if (pressed("LevelSelect"))
            chosen = 2;
        if (pressed("Menu"))
            chosen = 3;
        if (chosen == 0)
            setPaused(false);
        if (chosen == 1) {
            setPaused(false);
            if (auto status = restart(); !status)
                log(LogLevel::Error, "runtime", status.error());
            return false;
        }
        if (chosen == 2 || chosen == 3) {
            const auto destination = chosen == 2 ? menu->levelSelectScene : menu->menuScene;
            if (!destination.empty()) {
                setPaused(false);
                runtime_->requestSceneChange(destination);
            }
        }
        // Consume pause-menu controls so resuming never also interacts with the world.
        if (!paused_) {
            runtime_->update(seconds, InputFrame{});
            return followSceneChange();
        }
    }
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
