#include "yk/core/Application.hpp"
#include "yk/core/Log.hpp"
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <array>
#include <cassert>
#include <optional>
#include <thread>
namespace yk {
namespace {
struct WindowDeleter {
    void operator()(SDL_Window *ptr) const {
        SDL_DestroyWindow(ptr);
    }
};
struct SdlLifetime {
    bool initialized{};
    ~SdlLifetime() {
        if (initialized)
            SDL_Quit();
    }
};
constexpr std::array<SDL_Scancode, static_cast<std::size_t>(Key::Count)> scancodes = {
    SDL_SCANCODE_A,      SDL_SCANCODE_D,     SDL_SCANCODE_W,  SDL_SCANCODE_S,
    SDL_SCANCODE_LEFT,   SDL_SCANCODE_RIGHT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
    SDL_SCANCODE_ESCAPE, SDL_SCANCODE_SPACE, SDL_SCANCODE_Q,  SDL_SCANCODE_E};
std::optional<Key> mapKey(SDL_Scancode scancode) {
    for (std::size_t i = 0; i < scancodes.size(); ++i)
        if (scancodes[i] == scancode)
            return static_cast<Key>(i);
    return std::nullopt;
}
Error sdlError(const std::string &operation) {
    return {operation + ": " + SDL_GetError()};
}
} // namespace
struct Application::Impl {
    const std::thread::id thread = std::this_thread::get_id();
    SdlLifetime sdl;
    std::unique_ptr<SDL_Window, WindowDeleter> window;
    std::unique_ptr<Renderer> renderer;
    Keyboard keyboard;
    bool used{};
};
Application::Application() : impl_(std::make_unique<Impl>()) {}
Application::~Application() {
    assert(std::this_thread::get_id() == impl_->thread);
    impl_.reset();
    log(LogLevel::Info, "application", "Shutdown complete");
}
Result<std::unique_ptr<Application>> Application::create(const ApplicationConfig &config) {
    if (config.width <= 0 || config.height <= 0 || config.logicalWidth <= 0 ||
        config.logicalHeight <= 0)
        return Error{"Window and logical viewport dimensions must be positive"};
    // SDL owns process-wide platform state. One application owns the entire lifecycle.
    if (SDL_WasInit(0) != 0)
        return Error{"SDL already initialized; only one owning application is supported"};
    SDL_SetMainReady();
    auto app = std::unique_ptr<Application>(new Application());
    auto &state = *app->impl_;
    state.sdl.initialized = true; // SDL_Quit also cleans up a partially failed SDL_Init.
    if (!SDL_Init(SDL_INIT_VIDEO))
        return sdlError("Initialize SDL video");
    state.window.reset(SDL_CreateWindow(config.title.c_str(), config.width, config.height,
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY));
    if (!state.window)
        return sdlError("Create window");
    auto renderer = Renderer::create(state.window.get(), config.logicalWidth, config.logicalHeight);
    if (!renderer)
        return Error{renderer.error()};
    state.renderer = std::move(renderer.value());
    log(LogLevel::Info, "application", "Initialized SDL3 window and renderer");
    return app;
}
Status Application::run(ApplicationLayer &layer, const RunOptions &options) {
    assert(std::this_thread::get_id() == impl_->thread);
    if (options.captureLastFrame && options.frameLimit == 0)
        return Error{"Frame capture requires a bounded run"};
    if (impl_->used)
        return Error{"Application run is single-use"};
    impl_->used = true;
    auto initialized = layer.initialize(*impl_->renderer);
    if (!initialized)
        return initialized;
    FrameClock clock;
    const auto windowFlags = SDL_GetWindowFlags(impl_->window.get());
    bool focused = (windowFlags & SDL_WINDOW_INPUT_FOCUS) != 0;
    bool minimized = (windowFlags & SDL_WINDOW_MINIMIZED) != 0;
    unsigned frames = 0;
    bool running = true;
    while (running) {
        const auto frameStart = FrameClock::Clock::now();
        impl_->keyboard.beginFrame();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST &&
                event.window.windowID == SDL_GetWindowID(impl_->window.get())) {
                if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                    running = false;
                if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                    focused = false;
                    impl_->keyboard.releaseAll();
                    clock.reset();
                }
                if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                    focused = true;
                    clock.reset();
                }
                if (event.type == SDL_EVENT_WINDOW_MINIMIZED) {
                    minimized = true;
                    impl_->keyboard.releaseAll();
                    clock.reset();
                }
                if (event.type == SDL_EVENT_WINDOW_RESTORED) {
                    minimized = false;
                    clock.reset();
                }
            }
            if (focused && !minimized &&
                (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                event.key.windowID == SDL_GetWindowID(impl_->window.get())) {
                if (auto key = mapKey(event.key.scancode))
                    impl_->keyboard.set(*key, event.type == SDL_EVENT_KEY_DOWN);
            }
        }
        if (!running)
            break;
        const auto delta = clock.tick();
        const FrameContext context{focused && !minimized ? delta : FrameTime{}, impl_->keyboard};
        if (!layer.update(context))
            break;
        auto begun = impl_->renderer->beginFrame({24, 29, 40, 255}, layer.camera());
        if (!begun)
            return begun;
        auto rendered = layer.render(*impl_->renderer);
        if (!rendered)
            return rendered;
        auto presented = impl_->renderer->present(
            frames + 1 == options.frameLimit ? options.captureLastFrame : std::nullopt);
        if (!presented)
            return presented;
        ++frames;
        if (options.frameLimit != 0 && frames >= options.frameLimit)
            break;
        // VSync may be unsupported or disabled: cap at 120 Hz and avoid a busy loop.
        std::this_thread::sleep_until(frameStart + std::chrono::microseconds(8333));
    }
    impl_->keyboard.releaseAll();
    log(LogLevel::Info, "application", "Loop stopped after " + std::to_string(frames) + " frames");
    return success();
}
} // namespace yk
