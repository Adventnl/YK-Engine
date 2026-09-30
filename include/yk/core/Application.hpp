#pragma once
#include "yk/core/Result.hpp"
#include "yk/core/Time.hpp"
#include "yk/graphics/Renderer.hpp"
#include "yk/input/Input.hpp"
#include <memory>
#include <string>
union SDL_Event;
namespace yk {
class Renderer;
struct ApplicationConfig {
    std::string title{"YK Engine"};
    int width{960}, height{540};
    // A fixed, letterboxed logical resolution. Both zero selects native resolution instead: drawing
    // coordinates are output pixels and Renderer::viewport() follows the window (used by tools).
    int logicalWidth{960}, logicalHeight{540};
};
struct RunOptions {
    unsigned frameLimit{}; // Zero runs until layer/window requests exit.
    std::optional<std::filesystem::path> captureLastFrame; // Requires a nonzero frame limit.
};
struct FrameContext {
    FrameTime delta;
    const InputFrame &input; // The keyboard and every connected gamepad, as of this frame.
    bool focused{true};      // False while unfocused or minimized (delta is then zero).
};
class ApplicationLayer {
  public:
    virtual ~ApplicationLayer() = default;
    virtual Status initialize(Renderer &renderer) = 0;
    virtual bool update(const FrameContext &frame) = 0; // false requests orderly exit
    // Every platform event, before the application interprets it. Tools with their own input
    // handling (the editor's UI) consume events here.
    virtual void onNativeEvent(const SDL_Event &) {}
    // The window's close button or a quit request. Return false to keep running (a tool asking
    // about unsaved work first, then ending the run itself by returning false from update).
    virtual bool onCloseRequested() {
        return true;
    }
    virtual Camera2D camera() const {
        return {};
    }
    virtual Status
    render(Renderer &renderer) = 0; // Submit only; application owns frame boundaries.
};
// Tells the person that the program cannot start or has to stop. The message is logged and printed
// to stderr and, because a program started by double-click has no terminal, also shown in a message
// box that names the log file. No box appears under SDL's dummy video driver or when YK_NO_DIALOGS
// is set (tests and headless runs).
void showFatalError(const std::string &title, const std::string &message);

class Application {
  public:
    static Result<std::unique_ptr<Application>> create(const ApplicationConfig &config);
    ~Application();
    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;
    // ApplicationLayer is borrowed synchronously; no callbacks or layer references survive run().
    Status run(ApplicationLayer &layer, const RunOptions &options = {});
    // The platform window, for tools that configure it or query display scale. Not for game code.
    SDL_Window *nativeWindow() const;

  private:
    Application();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace yk
