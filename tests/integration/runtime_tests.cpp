#include "yk/core/Application.hpp"
#include "yk/graphics/PhysicsDebug.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cstdio>
#include <filesystem>
namespace {
int failures = 0;
void check(bool passed, const char *description) {
    if (!passed) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}
void windowEvent(SDL_WindowID window, Uint32 type) {
    SDL_Event event{};
    event.type = type;
    event.window.windowID = window;
    check(SDL_PushEvent(&event), "push window event");
}
void key(SDL_WindowID window, Uint32 type, SDL_Scancode scancode) {
    SDL_Event event{};
    event.type = type;
    event.key.windowID = window;
    event.key.scancode = scancode;
    check(SDL_PushEvent(&event), "push key event");
}
class TestApplicationLayer final : public yk::ApplicationLayer {
  public:
    yk::TextureHandle retained;
    yk::Status initialize(yk::Renderer &renderer) override {
        auto second = yk::Application::create({});
        check(!second, "second SDL owner rejected");
        const std::array<yk::Color, 4> pixels{
            {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 255}}};
        check(!renderer.createTexture(0, 2, pixels), "zero dimensions rejected");
        check(!renderer.createTexture(2, 3, pixels), "wrong pixel count rejected");
        check(!renderer.loadBmp("/this-path-does-not-exist/yk-missing.bmp"),
              "missing BMP returns contextual error");
        check(!renderer.valid({}) && !renderer.release({}), "default handle invalid");
        check(!renderer.present(), "present requires frame");
        const auto old = renderer.createTexture(2, 2, pixels);
        if (!old)
            return yk::Error{old.error()};
        check(static_cast<bool>(renderer.release(old.value())), "release live texture");
        check(!renderer.valid(old.value()) && !renderer.release(old.value()),
              "released handle cannot be reused");
        const auto live = renderer.createTexture(2, 2, pixels);
        if (!live)
            return yk::Error{live.error()};
        retained = live.value();
        check(!(retained == old.value()), "new resource does not resurrect stale handle");
        sprite_.texture = retained;
        sprite_.size = {32, 32};
        int count = 0;
        SDL_Window **windows = SDL_GetWindows(&count);
        check(count == 1 && windows != nullptr, "one application window");
        if (!windows || count != 1) {
            SDL_free(windows);
            return yk::Error{"Expected one runtime test window"};
        }
        window_ = SDL_GetWindowID(windows[0]);
        SDL_free(windows);
        windowEvent(window_, SDL_EVENT_WINDOW_FOCUS_GAINED);
        key(window_, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_D);
        return yk::success();
    }
    bool update(const yk::FrameContext &frame) override {
        const auto state = frame.input.keyboard.state(yk::Key::D);
        if (frames_ == 0) {
            check(state.held && state.pressed, "platform key mapping and first press");
            check(frame.delta.seconds == 0, "application first frame has zero delta");
            windowEvent(window_, SDL_EVENT_WINDOW_FOCUS_LOST);
        } else if (frames_ == 1) {
            check(!state.held && state.released && frame.delta.seconds == 0,
                  "focus loss releases and pauses simulation");
            key(window_, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_D); // Ignored while unfocused.
        } else if (frames_ == 2) {
            check(!state.held && !state.pressed, "unfocused key ignored");
            windowEvent(window_, SDL_EVENT_WINDOW_FOCUS_GAINED);
            key(window_, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_D);
            key(window_, SDL_EVENT_KEY_UP, SDL_SCANCODE_D);
        } else if (frames_ == 3) {
            check(!state.held && state.pressed && state.released, "platform same-frame tap");
            check(frame.delta.seconds == 0, "focus gain resets frame clock");
            windowEvent(window_, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
        } else if (frames_ == 4) {
            check(!state.held && !state.pressed && !state.released, "edges cleared next frame");
            key(window_, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_D);
        } else if (frames_ == 5) {
            check(state.held && state.pressed, "focused key starts before minimization");
            windowEvent(window_, SDL_EVENT_WINDOW_MINIMIZED);
        } else if (frames_ == 6) {
            check(!state.held && state.released && frame.delta.seconds == 0,
                  "minimization releases keys and pauses simulation");
            key(window_, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_D);
        } else if (frames_ == 7) {
            check(!state.held && !state.pressed && !state.released && frame.delta.seconds == 0,
                  "minimized input is ignored and simulation stays paused");
            windowEvent(window_, SDL_EVENT_WINDOW_RESTORED);
            key(window_, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_D);
        } else if (frames_ == 8) {
            check(state.held && state.pressed && frame.delta.seconds == 0,
                  "restore accepts input and resets frame clock");
            key(window_, SDL_EVENT_KEY_UP, SDL_SCANCODE_D);
        } else if (frames_ == 9) {
            check(!state.held && state.released, "restored key release is observed");
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            check(SDL_PushEvent(&quit), "push quit");
        }
        ++frames_;
        return true;
    }
    yk::Status render(yk::Renderer &renderer) override {
        check(renderer.viewport().x == 960 && renderer.viewport().y == 540,
              "logical viewport stable across resize events");
        check(!renderer.release(retained), "queued frame prevents resource release");
        check(!renderer.beginFrame({}, {}), "nested frame rejected");
        yk::Sprite invalid = sprite_;
        invalid.texture = {};
        check(!renderer.submit(invalid), "invalid sprite handle rejected");
        invalid = sprite_;
        invalid.transform.scale.x = -1;
        check(!renderer.submit(invalid), "unsupported negative scale rejected");
        auto submitted = renderer.submit(sprite_);
        if (!submitted)
            return submitted;
        return renderer.debugRect({{-32, -32}, {64, 64}}, {255, 255, 255, 255});
    }
    unsigned frames() const {
        return frames_;
    }

  private:
    yk::Sprite sprite_;
    SDL_WindowID window_{};
    unsigned frames_{};
};
class CaptureApplicationLayer final : public yk::ApplicationLayer {
  public:
    yk::Status initialize(yk::Renderer &renderer) override {
        yk::physics::WorldConfig config;
        config.gravity = {};
        auto created = yk::physics::World::create(config);
        if (!created)
            return yk::Error{created.error()};
        world_ = std::move(created.value());
        yk::physics::BodyDef body;
        body.pose.position = {0.4F, 0.2F};
        const auto physicsBody = world_->createBody(body);
        if (!physicsBody)
            return yk::Error{physicsBody.error()};
        const auto shape = world_->createShape(physicsBody.value(), yk::physics::Box{{0.1F, 0.1F}});
        if (!shape)
            return yk::Error{shape.error()};
        check(!yk::drawPhysicsDebug(renderer, *world_),
              "physics debug submission requires active frame");
        check(!yk::drawPhysicsDebug(renderer, *world_, 0), "physics debug scale validated");
        std::array<yk::Color, 1> red{{{255, 0, 0, 255}}};
        auto texture = renderer.createTexture(1, 1, red);
        if (!texture)
            return yk::Error{texture.error()};
        red_ = texture.value();
        SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
        if (!surface)
            return yk::Error{SDL_GetError()};
        const bool saved =
            SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGBA(surface, 0, 255, 0, 255)) &&
            SDL_SaveBMP(surface, "runtime-source.bmp");
        SDL_DestroySurface(surface);
        if (!saved)
            return yk::Error{SDL_GetError()};
        auto loaded = renderer.loadBmp(std::filesystem::current_path() / "runtime-source.bmp");
        if (!loaded)
            return yk::Error{loaded.error()};
        green_ = loaded.value();
        const auto cached =
            renderer.loadBmp(std::filesystem::current_path() / "." / "runtime-source.bmp");
        check(cached && cached.value() == green_, "BMP paths canonicalized and cached");
        check(static_cast<bool>(renderer.release(green_)),
              "file texture can be explicitly released");
        loaded = renderer.loadBmp(std::filesystem::current_path() / "runtime-source.bmp");
        if (!loaded)
            return yk::Error{loaded.error()};
        check(!(green_ == loaded.value()), "released cached asset loads as a new resource");
        green_ = loaded.value();
        int count = 0;
        SDL_Window **windows = SDL_GetWindows(&count);
        if (!windows || count != 1) {
            SDL_free(windows);
            return yk::Error{"Capture window missing"};
        }
        check(SDL_SetWindowSize(windows[0], 1200, 800),
              "real window resized to different aspect ratio");
        SDL_free(windows);
        return yk::success();
    }
    bool update(const yk::FrameContext &) override {
        return true;
    }
    yk::Camera2D camera() const override {
        yk::Camera2D camera;
        camera.position = {40, 20};
        camera.setZoom(2);
        return camera;
    }
    yk::Status render(yk::Renderer &renderer) override {
        yk::Sprite green;
        green.texture = green_;
        green.size = {16, 16};
        green.transform.position = {40, 20};
        green.layer = 2;
        auto result = renderer.submit(green); // Submitted first, drawn last by layer.
        if (!result)
            return result;
        yk::Sprite red = green;
        red.texture = red_;
        red.size = {48, 48};
        red.layer = 1;
        result = renderer.submit(red);
        if (!result)
            return result;
        green.layer = 1;
        green.depth = 3;
        green.transform.position = {110, 20};
        result = renderer.submit(green); // Depth order wins over later submission.
        if (!result)
            return result;
        red.depth = -3;
        red.transform.position = {110, 20};
        result = renderer.submit(red);
        if (!result)
            return result;
        green.layer = 3;
        green.depth = 0;
        green.transform.position = {180, 20};
        result = renderer.submit(green);
        if (!result)
            return result;
        red = green;
        red.texture = red_; // Identical keys: last submission wins.
        result = renderer.submit(red);
        if (!result)
            return result;
        return yk::drawPhysicsDebug(renderer, *world_, 100);
    }

  private:
    yk::TextureHandle red_, green_;
    std::unique_ptr<yk::physics::World> world_;
};
void checkPixel(SDL_Surface *surface, int x, int y, yk::Color expected, const char *description) {
    Uint8 r{}, g{}, b{}, a{};
    check(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a) && r == expected.r &&
              g == expected.g && b == expected.b,
          description);
}
class FailureApplicationLayer final : public yk::ApplicationLayer {
  public:
    yk::Status initialize(yk::Renderer &renderer) override {
        check(!renderer.valid(stale), "handle from destroyed renderer is foreign");
        return yk::Error{"expected initialization failure"};
    }
    bool update(const yk::FrameContext &) override {
        check(false, "failed layer must not update");
        return false;
    }
    yk::Status render(yk::Renderer &) override {
        check(false, "failed layer must not render");
        return yk::success();
    }
    yk::TextureHandle stale;
};
} // namespace
int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--native-smoke") {
        auto app = yk::Application::create({});
        if (!app) {
            std::fprintf(stderr, "%s\n", app.error().c_str());
            return 1;
        }
        CaptureApplicationLayer capture;
        const auto result =
            app.value()->run(capture, {12, std::filesystem::path("native-frame.bmp")});
        std::filesystem::remove("runtime-source.bmp");
        if (!result) {
            std::fprintf(stderr, "%s\n", result.error().c_str());
            return 1;
        }
        return failures == 0 ? 0 : 1;
    }
    check(!yk::Application::create({"bad", 0}), "invalid application config rejected");
    check(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "yk-nonexistent", SDL_HINT_OVERRIDE),
          "set failing video driver");
    {
        const auto failed = yk::Application::create({});
        check(!failed, "SDL initialization failure propagated");
        if (!failed)
            check(failed.error().find("Initialize SDL video") != std::string::npos,
                  "initialization error has operation context");
        check(SDL_WasInit(0) == 0, "failed SDL initialization cleaned up");
    }
    SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
    check(SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "yk-nonexistent", SDL_HINT_OVERRIDE),
          "set failing renderer");
    {
        const auto failed = yk::Application::create({});
        check(!failed, "renderer initialization failure propagated");
        check(SDL_WasInit(0) == 0, "renderer failure destroys window and SDL");
    }
    SDL_ResetHint(SDL_HINT_RENDER_DRIVER);
    TestApplicationLayer layer;
    {
        auto app = yk::Application::create({});
        if (!app) {
            std::fprintf(stderr, "%s\n", app.error().c_str());
            return 1;
        }
        const auto result = app.value()->run(layer, {12, std::nullopt});
        if (!result)
            std::fprintf(stderr, "%s\n", result.error().c_str());
        check(static_cast<bool>(result), "real loop renders and exits");
        check(layer.frames() == 10, "quit is handled before another update");
        check(!app.value()->run(layer, {1, std::nullopt}), "single-use application contract");
    }
    check(SDL_WasInit(0) == 0, "shutdown releases SDL after resources");
    {
        auto app = yk::Application::create({});
        if (!app)
            return 1;
        FailureApplicationLayer failure;
        failure.stale = layer.retained;
        check(!app.value()->run(failure, {1, std::nullopt}),
              "layer initialization failure propagated");
    }
    check(SDL_WasInit(0) == 0, "failed layer shuts down cleanly");
    {
        auto app = yk::Application::create({});
        if (!app)
            return 1;
        CaptureApplicationLayer capture;
        const auto result =
            app.value()->run(capture, {2, std::filesystem::path("runtime-frame.bmp")});
        if (!result)
            std::fprintf(stderr, "%s\n", result.error().c_str());
        check(static_cast<bool>(result), "capture real renderer before presentation");
    }
    SDL_Surface *image = SDL_LoadBMP("runtime-frame.bmp");
    check(image != nullptr, "captured frame can be read");
    if (image) {
        // SDL readback clips to the content viewport: 1200x675 inside a 1200x800 window.
        check(image->w == 1200 && image->h == 675,
              "capture preserves logical aspect at resized physical resolution");
        checkPixel(image, 600, 337, {0, 255, 0, 255},
                   "camera-centered green BMP sprite wins by layer");
        checkPixel(image, 650, 337, {255, 0, 0, 255},
                   "zoom and sprite size project red outer area");
        checkPixel(image, 775, 337, {0, 255, 0, 255},
                   "explicit depth sorting overrides submission order");
        checkPixel(image, 950, 337, {255, 0, 0, 255}, "equal sort keys retain submission order");
        checkPixel(image, 30, 337, {24, 29, 40, 255}, "clear color outside sprites");
        checkPixel(image, 575, 337, {90, 220, 110, 255},
                   "physics debug outline agrees with meter conversion and camera projection");
        SDL_DestroySurface(image);
    }
    std::filesystem::remove("runtime-source.bmp");
    std::filesystem::remove("runtime-frame.bmp");

    return failures == 0 ? 0 : 1;
}
