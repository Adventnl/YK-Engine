#pragma once
#include "core/EditorProject.hpp"
#include "yk/runtime/GameSession.hpp"
#include <memory>
#include <string>

namespace yk::editor {
// The scene being played from the editor. It runs a copy of the open document, including edits that
// were never saved, so pressing Play never changes the scene being edited and stopping simply drops
// the copy: the editor is exactly where the designer left it.
class PlaySession {
  public:
    // `audio` may be null (silent play). `viewport` is the size of the game view in pixels.
    // `transitionSeconds` is how long a restart or a change of scene fades out (and back in), as in
    // the player; zero switches at once.
    static Result<std::unique_ptr<PlaySession>> start(const EditorDocument &document,
                                                      const EditorProject &project,
                                                      AudioSink *audio, Vec2 viewport,
                                                      float transitionSeconds = 0.35F);

    // The scene being played (a new runtime replaces it when the game moves to another scene).
    GameRuntime &runtime() {
        return session_->runtime();
    }
    const GameRuntime &runtime() const {
        return session_->runtime();
    }
    // Advances by real elapsed time (unless paused) and follows the game's scene-change requests.
    void update(double seconds, const InputFrame &input);
    void update(double seconds, const Keyboard &keyboard);
    // One fixed tick while paused.
    void step(const InputFrame &input);
    void step(const Keyboard &keyboard);
    void setPaused(bool paused);
    bool paused() const {
        return session_->paused();
    }
    // Starts the current scene over.
    Status restart();
    void setViewportSize(Vec2 pixels);
    // Project-relative scene being played ("" for the editor's unsaved copy).
    const std::string &scenePath() const {
        return session_->scenePath();
    }

  private:
    PlaySession() = default;

    std::unique_ptr<GameSession> session_;
};
} // namespace yk::editor
