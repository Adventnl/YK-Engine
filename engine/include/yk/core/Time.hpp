#pragma once
#include <chrono>
namespace yk {
struct FrameTime {
    float seconds{};
};
// Pure conversion shared by the clock and tests. Invalid/backward time is zero.
FrameTime boundedDelta(double seconds);
class FrameClock {
  public:
    using Clock = std::chrono::steady_clock;
    void reset();
    FrameTime tick();

  private:
    Clock::time_point previous_{};
    bool started_{};
};
} // namespace yk
