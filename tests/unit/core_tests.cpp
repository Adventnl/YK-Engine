#include "yk/core/Time.hpp"
#include "yk/graphics/Camera2D.hpp"
#include "yk/input/Input.hpp"
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
namespace {
int failures = 0;
void check(bool passed, const char *description) {
    if (!passed) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}
bool near(float a, float b) {
    return std::abs(a - b) < 0.0001F;
}
} // namespace
int main() {
    yk::Keyboard keyboard;
    keyboard.beginFrame();
    keyboard.set(yk::Key::A, true);
    check(keyboard.state(yk::Key::A).held && keyboard.state(yk::Key::A).pressed,
          "key down transition");
    keyboard.beginFrame();
    keyboard.set(yk::Key::A, true);
    check(keyboard.state(yk::Key::A).held && !keyboard.state(yk::Key::A).pressed,
          "repeat does not retrigger");
    keyboard.releaseAll();
    check(!keyboard.state(yk::Key::A).held && keyboard.state(yk::Key::A).released,
          "focus loss releases held keys");
    keyboard.beginFrame();
    keyboard.set(yk::Key::A, true);
    keyboard.set(yk::Key::A, false);
    check(keyboard.state(yk::Key::A).pressed && keyboard.state(yk::Key::A).released &&
              !keyboard.state(yk::Key::A).held,
          "tap in a single frame preserves both edges");
    check(!keyboard.state(yk::Key::Count).held, "invalid key is safe");
    check(near(yk::boundedDelta(0.016).seconds, 0.016F), "normal seconds preserved");
    check(near(yk::boundedDelta(9).seconds, 0.1F), "long pause clamped");
    check(yk::boundedDelta(-1).seconds == 0 &&
              yk::boundedDelta(std::numeric_limits<double>::infinity()).seconds == 0 &&
              yk::boundedDelta(std::numeric_limits<double>::quiet_NaN()).seconds == 0,
          "invalid time cannot reach update");
    yk::FrameClock clock;
    check(clock.tick().seconds == 0, "first frame zero delta");
    yk::Camera2D camera;
    camera.position = {40, -20};
    check(camera.setZoom(2), "positive zoom accepted");
    auto center = camera.worldToScreen(camera.position, {960, 540});
    check(near(center.x, 480) && near(center.y, 270), "camera center maps to viewport center");
    auto screen = camera.worldToScreen({52, 15}, {960, 540});
    check(near(screen.x, 504) && near(screen.y, 340), "translated and zoomed world coordinate");
    auto world = camera.screenToWorld(screen, {960, 540});
    check(near(world.x, 52) && near(world.y, 15), "camera transform round trip");
    check(!camera.setZoom(0) && !camera.setZoom(-1) &&
              !camera.setZoom(std::numeric_limits<float>::quiet_NaN()) && camera.zoom() == 2,
          "invalid zoom leaves valid camera intact");
    const auto diagonal = yk::normalized({1, 1});
    check(near(std::hypot(diagonal.x, diagonal.y), 1), "diagonal input does not increase speed");
    check(yk::normalized({}).x == 0 && yk::normalized({}).y == 0, "zero direction stays zero");
    const auto moved =
        yk::Vec2{10, 20} + yk::normalized({1, 0}) * (240 * yk::boundedDelta(0.05).seconds);
    check(near(moved.x, 22) && near(moved.y, 20), "movement uses seconds and units per second");
    return failures == 0 ? 0 : 1;
}
