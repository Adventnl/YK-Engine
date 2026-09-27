#pragma once
#include <cmath>
namespace yk {
struct Vec2 {
    float x{}, y{};
    Vec2 operator+(Vec2 rhs) const {
        return {x + rhs.x, y + rhs.y};
    }
    Vec2 operator-(Vec2 rhs) const {
        return {x - rhs.x, y - rhs.y};
    }
    Vec2 operator*(float factor) const {
        return {x * factor, y * factor};
    }
};
inline bool finite(Vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}
inline Vec2 normalized(Vec2 value) {
    const float length = std::hypot(value.x, value.y);
    return length > 0.0F && std::isfinite(length) ? value * (1.0F / length) : Vec2{};
}
struct Rect {
    Vec2 position;
    Vec2 size;
};
struct Transform2D {
    Vec2 position{};
    Vec2 scale{1.0F, 1.0F};
    float rotationDegrees{};
};
} // namespace yk
