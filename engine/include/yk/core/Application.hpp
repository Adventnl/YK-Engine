#pragma once
#include "yk/core/Result.hpp"
#include "yk/core/Time.hpp"
#include "yk/graphics/Renderer.hpp"
#include "yk/input/Input.hpp"
#include <memory>
#include <string>
namespace yk {
class Renderer;
struct ApplicationConfig {
    std::string title{"YK Engine"};
    int width{960}, height{540};
    int logicalWidth{960}, logicalHeight{540};
};
struct RunOptions {
    unsigned frameLimit{}; // Zero runs until layer/window requests exit.
    std::optional<std::filesystem::path> captureLastFrame; // Requires a nonzero frame limit.
};
struct FrameContext {
    FrameTime delta;
    const Keyboard &keyboard;
};
class ApplicationLayer {
  public:
    virtual ~ApplicationLayer() = default;
    virtual Status initialize(Renderer &renderer) = 0;
    virtual bool update(const FrameContext &frame) = 0; // false requests orderly exit
    virtual Camera2D camera() const {
        return {};
    }
    virtual Status
    render(Renderer &renderer) = 0; // Submit only; application owns frame boundaries.
};
class Application {
  public:
    static Result<std::unique_ptr<Application>> create(const ApplicationConfig &config);
    ~Application();
    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;
    // ApplicationLayer is borrowed synchronously; no callbacks or layer references survive run().
    Status run(ApplicationLayer &layer, const RunOptions &options = {});

  private:
    Application();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace yk
