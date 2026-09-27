#include "yk/core/Application.hpp"
#include "yk/core/Log.hpp"
#include <array>
#include <charconv>
#include <exception>
#include <string_view>
namespace {
class Sandbox final : public yk::Game {
  public:
    yk::Status initialize(yk::Renderer &renderer) override {
        std::array<yk::Color, 16 * 16> pixels{};
        for (std::size_t y = 0; y < 16; ++y)
            for (std::size_t x = 0; x < 16; ++x)
                pixels[y * 16 + x] = ((x / 4 + y / 4) % 2) == 0 ? yk::Color{255, 194, 74, 255}
                                                                : yk::Color{55, 181, 211, 255};
        auto texture = renderer.createTexture(16, 16, pixels);
        if (!texture)
            return yk::Error{texture.error()};
        sprite_.texture = texture.value();
        sprite_.size = {64, 64};
        yk::log(yk::LogLevel::Info, "sandbox",
                "WASD/arrows move; Q/E move camera; Space resets; Escape exits");
        return yk::success();
    }
    bool update(const yk::FrameContext &frame) override {
        left_.update(frame.keyboard);
        right_.update(frame.keyboard);
        up_.update(frame.keyboard);
        down_.update(frame.keyboard);
        const yk::Vec2 direction{
            static_cast<float>(right_.state().held) - static_cast<float>(left_.state().held),
            static_cast<float>(down_.state().held) - static_cast<float>(up_.state().held)};
        sprite_.transform.position =
            sprite_.transform.position + yk::normalized(direction) * (240.0F * frame.delta.seconds);
        const float pan = static_cast<float>(frame.keyboard.state(yk::Key::E).held) -
                          static_cast<float>(frame.keyboard.state(yk::Key::Q).held);
        camera_.position.x += pan * 180.0F * frame.delta.seconds;
        if (frame.keyboard.state(yk::Key::Space).pressed) {
            sprite_.transform.position = {};
            camera_.position = {};
        }
        return !frame.keyboard.state(yk::Key::Escape).pressed;
    }
    yk::Camera2D camera() const override {
        return camera_;
    }
    yk::Status render(yk::Renderer &renderer) override {
        auto grid = renderer.debugRect({{-320, -180}, {640, 360}}, {72, 88, 108, 255}, -1);
        if (!grid)
            return grid;
        return renderer.submit(sprite_);
    }

  private:
    yk::Sprite sprite_;
    yk::Camera2D camera_;
    yk::ActionBinding left_{yk::Key::A, yk::Key::Left}, right_{yk::Key::D, yk::Key::Right};
    yk::ActionBinding up_{yk::Key::W, yk::Key::Up}, down_{yk::Key::S, yk::Key::Down};
};
} // namespace
int main(int argc, char **argv) {
    unsigned frames = 0;
    if (argc != 1) {
        if ((argc != 3 && argc != 5) || std::string_view(argv[1]) != "--frames") {
            yk::log(yk::LogLevel::Error, "sandbox",
                    "Usage: yk_sandbox [--frames positive-integer [--capture file.bmp]]");
            return 2;
        }
        const std::string_view value(argv[2]);
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), frames);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || frames == 0)
            return 2;
    }
    yk::RunOptions options{frames, std::nullopt};
    if (argc == 5) {
        if (std::string_view(argv[3]) != "--capture")
            return 2;
        options.captureLastFrame = std::filesystem::path(argv[4]);
    }
    try {
        auto app = yk::Application::create(
            {"YK Engine | WASD / arrows | Q/E camera | Space reset | Esc quit"});
        if (!app) {
            yk::log(yk::LogLevel::Error, "sandbox", app.error());
            return 1;
        }
        Sandbox game;
        auto result = app.value()->run(game, options);
        if (!result) {
            yk::log(yk::LogLevel::Error, "sandbox", result.error());
            return 1;
        }
        return 0;
    } catch (const std::exception &error) {
        yk::log(yk::LogLevel::Error, "sandbox", error.what());
        return 1;
    }
}
