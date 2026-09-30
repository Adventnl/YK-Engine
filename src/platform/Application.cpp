#include "yk/core/Application.hpp"
#include "stb_image.h"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <array>
#include <cassert>
#include <cstdio>
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
constexpr std::array<SDL_Scancode, keyCount> scancodes = {
#define YK_KEY_SCANCODE(name, display, sdl) SDL_SCANCODE_##sdl,
    YK_KEY_LIST(YK_KEY_SCANCODE)
#undef YK_KEY_SCANCODE
};
std::optional<Key> mapKey(SDL_Scancode scancode) {
    for (std::size_t i = 0; i < scancodes.size(); ++i)
        if (scancodes[i] == scancode)
            return static_cast<Key>(i);
    return std::nullopt;
}
std::optional<GamepadButton> mapButton(Uint8 button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return GamepadButton::South;
    case SDL_GAMEPAD_BUTTON_EAST:
        return GamepadButton::East;
    case SDL_GAMEPAD_BUTTON_WEST:
        return GamepadButton::West;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return GamepadButton::North;
    case SDL_GAMEPAD_BUTTON_BACK:
        return GamepadButton::Back;
    case SDL_GAMEPAD_BUTTON_START:
        return GamepadButton::Start;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return GamepadButton::LeftStick;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return GamepadButton::RightStick;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return GamepadButton::LeftShoulder;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return GamepadButton::RightShoulder;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return GamepadButton::DPadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return GamepadButton::DPadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return GamepadButton::DPadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return GamepadButton::DPadRight;
    default:
        return std::nullopt;
    }
}
std::optional<GamepadAxis> mapAxis(Uint8 axis) {
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX:
        return GamepadAxis::LeftX;
    case SDL_GAMEPAD_AXIS_LEFTY:
        return GamepadAxis::LeftY;
    case SDL_GAMEPAD_AXIS_RIGHTX:
        return GamepadAxis::RightX;
    case SDL_GAMEPAD_AXIS_RIGHTY:
        return GamepadAxis::RightY;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        return GamepadAxis::LeftTrigger;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
        return GamepadAxis::RightTrigger;
    default:
        return std::nullopt;
    }
}
Error sdlError(const std::string &operation) {
    return {operation + ": " + SDL_GetError()};
}
struct GamepadDeleter {
    void operator()(SDL_Gamepad *ptr) const {
        SDL_CloseGamepad(ptr);
    }
};
} // namespace
struct Application::Impl {
    const std::thread::id thread = std::this_thread::get_id();
    SdlLifetime sdl;
    std::unique_ptr<SDL_Window, WindowDeleter> window;
    std::unique_ptr<Renderer> renderer;
    InputFrame input;
    // Connected controllers occupy the first free of maxGamepads slots (the index an ActionSet
    // names); a controller that returns may get a different slot.
    struct Pad {
        std::unique_ptr<SDL_Gamepad, GamepadDeleter> handle;
        SDL_JoystickID id{};
    };
    std::array<Pad, maxGamepads> pads;
    bool used{};

    Gamepad *padFor(SDL_JoystickID id) {
        for (std::size_t i = 0; i < pads.size(); ++i)
            if (pads[i].handle && pads[i].id == id)
                return &input.gamepads[i];
        return nullptr;
    }
    void addPad(SDL_JoystickID id) {
        for (std::size_t i = 0; i < pads.size(); ++i) {
            if (pads[i].handle)
                continue;
            SDL_Gamepad *opened = SDL_OpenGamepad(id);
            if (!opened) {
                log(LogLevel::Warning, "input",
                    std::string("Cannot open the gamepad: ") + SDL_GetError());
                return;
            }
            pads[i].handle.reset(opened);
            pads[i].id = id;
            input.gamepads[i].setConnected(true);
            log(LogLevel::Info, "input",
                "Gamepad " + std::to_string(i) + " connected: " +
                    (SDL_GetGamepadName(opened) ? SDL_GetGamepadName(opened) : "unnamed"));
            return;
        }
        log(LogLevel::Warning, "input", "More than 4 gamepads; extra controllers are ignored");
    }
    void removePad(SDL_JoystickID id) {
        for (std::size_t i = 0; i < pads.size(); ++i) {
            if (!pads[i].handle || pads[i].id != id)
                continue;
            pads[i].handle.reset();
            input.gamepads[i].setConnected(false);
            log(LogLevel::Info, "input", "Gamepad " + std::to_string(i) + " disconnected");
        }
    }
    void releaseAll() {
        input.keyboard.releaseAll();
        for (auto &pad : input.gamepads) {
            const bool connected = pad.connected();
            pad.setConnected(false); // Clears every button and axis.
            pad.setConnected(connected);
        }
    }
};
void showFatalError(const std::string &title, const std::string &message) {
    log(LogLevel::Error, "fatal", title + ": " + message);
    std::fprintf(stderr, "%s: %s\n", title.c_str(), message.c_str());
    const auto driver = environmentVariable("SDL_VIDEODRIVER");
    if (environmentVariable("YK_NO_DIALOGS") || (driver && *driver == "dummy"))
        return;
    std::string body = message;
    if (const auto file = logFilePath(); !file.empty())
        body += "\n\nMore detail is in the log:\n" + file.string();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), body.c_str(), nullptr);
}

Application::Application() : impl_(std::make_unique<Impl>()) {}
Application::~Application() {
    assert(std::this_thread::get_id() == impl_->thread);
    impl_.reset();
    log(LogLevel::Info, "application", "Shutdown complete");
}
Result<std::unique_ptr<Application>> Application::create(const ApplicationConfig &config) {
    const bool nativeResolution = config.logicalWidth == 0 && config.logicalHeight == 0;
    if (config.width <= 0 || config.height <= 0 ||
        (!nativeResolution && (config.logicalWidth <= 0 || config.logicalHeight <= 0)))
        return Error{"Window dimensions must be positive; the logical viewport must be positive or "
                     "both zero for native resolution"};
    // SDL owns process-wide platform state. One application owns the entire lifecycle.
    if (SDL_WasInit(0) != 0)
        return Error{"SDL already initialized; only one owning application is supported"};
    SDL_SetMainReady();
    auto app = std::unique_ptr<Application>(new Application());
    auto &state = *app->impl_;
    state.sdl.initialized = true; // SDL_Quit also cleans up a partially failed SDL_Init.
    // Gamepads are optional too; a machine without controller support still runs.
    const bool gamepads = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD);
    if (!gamepads && !SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        // Audio is optional: retain a playable application on machines without a device.
        if (!SDL_Init(SDL_INIT_VIDEO))
            return sdlError("Initialize SDL video");
        log(LogLevel::Warning, "audio", "Audio unavailable; continuing silently");
    } else if (!gamepads) {
        log(LogLevel::Warning, "input", "Gamepad support unavailable; keyboard only");
    }
    state.window.reset(SDL_CreateWindow(config.title.c_str(), config.width, config.height,
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY));
    if (!state.window)
        return sdlError("Create window");
    if (!config.iconFile.empty() || !config.iconPng.empty()) {
        int width = 0, height = 0, channels = 0;
        unsigned char *pixels = nullptr;
        std::string source = "the window icon";
        if (!config.iconFile.empty()) {
            source = config.iconFile.string();
            pixels = stbi_load(source.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        } else {
            pixels = stbi_load_from_memory(
                reinterpret_cast<const unsigned char *>(config.iconPng.data()),
                static_cast<int>(config.iconPng.size()), &width, &height, &channels,
                STBI_rgb_alpha);
        }
        if (pixels != nullptr) {
            SDL_Surface *icon =
                SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels, width * 4);
            if (icon != nullptr) {
                SDL_SetWindowIcon(state.window.get(), icon);
                SDL_DestroySurface(icon);
            }
            stbi_image_free(pixels);
        } else {
            log(LogLevel::Warning, "application", "Cannot read " + source);
        }
    }
    auto renderer = Renderer::create(state.window.get(), config.logicalWidth, config.logicalHeight);
    if (!renderer)
        return Error{renderer.error()};
    state.renderer = std::move(renderer.value());
    log(LogLevel::Info, "application", "Initialized SDL3 window and renderer");
    return app;
}
SDL_Window *Application::nativeWindow() const {
    return impl_->window.get();
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
        impl_->input.beginFrame();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            layer.onNativeEvent(event);
            if (event.type == SDL_EVENT_QUIT && layer.onCloseRequested())
                running = false;
            if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST &&
                event.window.windowID == SDL_GetWindowID(impl_->window.get())) {
                if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && layer.onCloseRequested())
                    running = false;
                if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                    focused = false;
                    impl_->releaseAll();
                    clock.reset();
                }
                if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                    focused = true;
                    clock.reset();
                }
                if (event.type == SDL_EVENT_WINDOW_MINIMIZED) {
                    minimized = true;
                    impl_->releaseAll();
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
                    impl_->input.keyboard.set(*key, event.type == SDL_EVENT_KEY_DOWN);
            }
            if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                impl_->addPad(event.gdevice.which);
            } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                impl_->removePad(event.gdevice.which);
            } else if (focused && !minimized &&
                       (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                        event.type == SDL_EVENT_GAMEPAD_BUTTON_UP)) {
                Gamepad *pad = impl_->padFor(event.gbutton.which);
                if (const auto button = mapButton(event.gbutton.button); pad && button)
                    pad->setButton(*button, event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
            } else if (focused && !minimized && event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                Gamepad *pad = impl_->padFor(event.gaxis.which);
                if (const auto axis = mapAxis(event.gaxis.axis); pad && axis)
                    pad->setAxis(*axis, static_cast<float>(event.gaxis.value) / 32767.0F);
            }
        }
        if (!running)
            break;
        const auto delta = clock.tick();
        const FrameContext context{focused && !minimized ? delta : FrameTime{}, impl_->input,
                                   focused && !minimized};
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
    impl_->releaseAll();
    log(LogLevel::Info, "application", "Loop stopped after " + std::to_string(frames) + " frames");
    return success();
}
} // namespace yk
