#pragma once
#include "yk/runtime/GameRuntime.hpp"
#include <functional>
#include <memory>
#include <string>

namespace yk {
// Runs a game across its scenes. A GameRuntime executes one scene; what happens when the game asks
// for another (a level is complete, a door leads on) belongs here: load the scene, carry over the
// variables the old scene kept, start the new one covered so that it fades in, and go on. The
// standalone player and the editor's Play mode both use it, so a game moves from level to level in
// exactly the same way in both, and a host has nothing to do by hand but draw and pass input.
class GameSession {
  public:
    // Fetches a scene by its project-relative path: the host knows where the project is.
    using SceneLoader = std::function<Result<std::unique_ptr<Scene>>(const std::string &path)>;

    // `scene` is the first scene and `path` its project-relative name ("" when it is not a file,
    // as with the editor's unsaved copy). `options` serve every scene the session runs.
    static Result<std::unique_ptr<GameSession>> create(std::unique_ptr<Scene> scene,
                                                       std::string path, RuntimeOptions options,
                                                       SceneLoader loader);

    // The runtime of the scene being played. A new one replaces it when the scene changes, so do
    // not keep the reference between frames.
    GameRuntime &runtime() {
        return *runtime_;
    }
    const GameRuntime &runtime() const {
        return *runtime_;
    }
    const std::string &scenePath() const {
        return path_;
    }

    // Advances by `seconds` of real time (unless paused) and follows the game's request to change
    // scene. A scene that cannot be loaded is logged and the current one goes on playing. Returns
    // true when a new scene started during this call. The pause action of the options toggles the
    // pause, and is heard while paused, which the runtime itself could not.
    bool update(double seconds, const InputFrame &input);
    // One fixed tick, for stepping while paused; scene changes are followed too.
    bool step(const InputFrame &input);
    void setPaused(bool paused);
    bool paused() const {
        return paused_;
    }
    // Starts the current scene over, at once (a designer's button, not a game rule).
    Status restart();
    void setViewportSize(Vec2 pixels);

  private:
    GameSession() = default;
    bool followSceneChange();

    RuntimeOptions options_;
    ActionInput pauseInput_; // Reads only the pause action, so it keeps working while paused.
    SceneLoader loader_;
    std::unique_ptr<GameRuntime> runtime_;
    std::string path_;
    bool paused_{};
};
} // namespace yk
