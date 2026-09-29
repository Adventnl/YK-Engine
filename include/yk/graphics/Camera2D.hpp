#pragma once
#include "yk/core/Math.hpp"
namespace yk {
// World axes: +x right, +y down. Position is the center of the logical viewport.
class Camera2D {
  public:
    Vec2 position{};
    bool setZoom(float zoom) {
        if (!std::isfinite(zoom) || zoom <= 0.0F)
            return false;
        zoom_ = zoom;
        return true;
    }
    float zoom() const {
        return zoom_;
    }
    Vec2 worldToScreen(Vec2 world, Vec2 viewport) const {
        return (world - position) * zoom_ + viewport * 0.5F;
    }
    Vec2 screenToWorld(Vec2 screen, Vec2 viewport) const {
        return (screen - viewport * 0.5F) * (1.0F / zoom_) + position;
    }
    // The world rectangle a `viewport`-pixel view of this camera shows.
    Rect visibleWorld(Vec2 viewport) const {
        const Vec2 half = viewport * (0.5F / zoom_);
        return {position - half, half * 2.0F};
    }

  private:
    float zoom_{1.0F};
};
} // namespace yk
