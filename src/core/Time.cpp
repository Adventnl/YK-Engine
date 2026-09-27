#include "yk/core/Time.hpp"
#include <algorithm>
#include <cmath>
namespace yk {
FrameTime boundedDelta(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0)
        return {};
    return {static_cast<float>(std::min(seconds, 0.1))};
}
void FrameClock::reset() {
    previous_ = Clock::now();
    started_ = true;
}
FrameTime FrameClock::tick() {
    const auto now = Clock::now();
    if (!started_) {
        previous_ = now;
        started_ = true;
        return {};
    }
    const auto delta = std::chrono::duration<double>(now - previous_).count();
    previous_ = now;
    return boundedDelta(delta);
}
} // namespace yk
