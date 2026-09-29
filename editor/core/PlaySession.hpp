#pragma once
#include "core/EditorProject.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include <memory>
#include <string>

namespace yk::editor {
// The scene being played from the editor. It runs a copy of the open document, including edits that
// were never saved, so pressing Play never changes the scene being edited and stopping simply drops
// the copy: the editor is exactly where the designer left it.
class PlaySession {
  public:
    // `audio` may be null (silent play). `viewport` is the size of the game view in pixels.
    static Result<std::unique_ptr<PlaySession>> start(const EditorDocument &document,
                                                      const EditorProject &project,
                                                      AudioSink *audio, Vec2 viewport);

    GameRuntime &runtime() {
        return *runtime_;
    }
    const GameRuntime &runtime() const {
        return *runtime_;
    }
    // Advances by real elapsed time (unless paused) and follows the game's scene-change requests.
    void update(double seconds, const Keyboard &keyboard);
    // One fixed tick while paused.
    void step(const Keyboard &keyboard);
    void setPaused(bool paused);
    bool paused() const {
        return paused_;
    }
    // Starts the current scene over.
    Status restart();
    void setViewportSize(Vec2 pixels);
    // Project-relative scene being played ("" for the editor's unsaved copy).
    const std::string &scenePath() const {
        return scenePath_;
    }

  private:
    PlaySession() = default;
    Status load(std::unique_ptr<Scene> scene);
    Status followSceneChange();

    const EditorProject *project_{};
    AudioSink *audio_{};
    Vec2 viewport_;
    std::unique_ptr<GameRuntime> runtime_;
    std::string scenePath_;
    bool paused_{};
};
} // namespace yk::editor
