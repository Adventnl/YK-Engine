#include "core/PlaySession.hpp"
#include "yk/core/Log.hpp"
#include "yk/scene/SceneSerializer.hpp"

namespace yk::editor {
Result<std::unique_ptr<PlaySession>> PlaySession::start(const EditorDocument &document,
                                                        const EditorProject &project,
                                                        AudioSink *audio, Vec2 viewport,
                                                        float transitionSeconds) {
    if (document.inChange())
        return Error{"Finish the current edit before playing"};
    // The copy goes through the same save format a real game load uses, so Play shows exactly what
    // the saved scene would do.
    auto copy = sceneFromJson(sceneToJson(document.scene()), project.registry());
    if (!copy)
        return Error{"Cannot copy the scene for play mode: " + copy.error()};
    RuntimeOptions options;
    options.layers = project.project().layers;
    options.inputMap = project.project().input;
    options.viewportSize = viewport;
    options.audio = audio;
    options.assets = &project.assets();
    options.transitionSeconds = transitionSeconds;
    // Scenes the game asks for are read from the project, as the standalone player does.
    const auto loader = [&project](const std::string &path) -> Result<std::unique_ptr<Scene>> {
        auto absolute = project.project().resolve(path);
        if (!absolute)
            return Error{absolute.error()};
        return loadScene(absolute.value(), project.registry());
    };
    auto session =
        GameSession::create(std::move(copy.value()), document.path(), std::move(options), loader);
    if (!session)
        return Error{session.error()};
    std::unique_ptr<PlaySession> play(new PlaySession());
    play->session_ = std::move(session.value());
    return play;
}

void PlaySession::update(double seconds, const Keyboard &keyboard) {
    InputFrame frame;
    frame.keyboard = keyboard;
    update(seconds, frame);
}

void PlaySession::update(double seconds, const InputFrame &input) {
    session_->update(seconds, input);
}

void PlaySession::step(const Keyboard &keyboard) {
    InputFrame frame;
    frame.keyboard = keyboard;
    step(frame);
}

void PlaySession::step(const InputFrame &input) {
    session_->step(input);
}

void PlaySession::setPaused(bool paused) {
    session_->setPaused(paused);
}

Status PlaySession::restart() {
    return session_->restart();
}

void PlaySession::setViewportSize(Vec2 pixels) {
    session_->setViewportSize(pixels);
}
} // namespace yk::editor
