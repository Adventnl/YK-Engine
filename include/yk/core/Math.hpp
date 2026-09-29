#pragma once
#include <algorithm>
#include <cmath>
#include <numbers>

namespace yk {
inline constexpr float pi = std::numbers::pi_v<float>;
inline constexpr float degreesToRadians(float degrees) {
    return degrees * (pi / 180.0F);
}
inline constexpr float radiansToDegrees(float radians) {
    return radians * (180.0F / pi);
}

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
    Vec2 operator/(float divisor) const {
        return {x / divisor, y / divisor};
    }
    Vec2 operator-() const {
        return {-x, -y};
    }
    Vec2 &operator+=(Vec2 rhs) {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }
    Vec2 &operator-=(Vec2 rhs) {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }
    Vec2 &operator*=(float factor) {
        x *= factor;
        y *= factor;
        return *this;
    }
    friend bool operator==(Vec2, Vec2) = default;
};
inline Vec2 operator*(float factor, Vec2 value) {
    return value * factor;
}
inline bool finite(Vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}
inline float dot(Vec2 a, Vec2 b) {
    return a.x * b.x + a.y * b.y;
}
inline float lengthSquared(Vec2 value) {
    return dot(value, value);
}
inline float length(Vec2 value) {
    return std::hypot(value.x, value.y);
}
inline float distance(Vec2 a, Vec2 b) {
    return length(a - b);
}
inline Vec2 hadamard(Vec2 a, Vec2 b) {
    return {a.x * b.x, a.y * b.y};
}
inline Vec2 lerp(Vec2 a, Vec2 b, float t) {
    return a + (b - a) * t;
}
inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}
// Counter-clockwise in +Y-up math; appears clockwise on screen where +Y points down.
inline Vec2 rotated(Vec2 value, float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    return {value.x * c - value.y * s, value.x * s + value.y * c};
}
inline Vec2 normalized(Vec2 value) {
    const float len = length(value);
    return len > 0.0F && std::isfinite(len) ? value * (1.0F / len) : Vec2{};
}
inline Vec2 clamped(Vec2 value, Vec2 low, Vec2 high) {
    return {std::clamp(value.x, low.x, high.x), std::clamp(value.y, low.y, high.y)};
}

struct Rect {
    Vec2 position;
    Vec2 size;
};
inline Vec2 center(Rect r) {
    return r.position + r.size * 0.5F;
}
inline bool contains(Rect r, Vec2 p) {
    return p.x >= r.position.x && p.y >= r.position.y && p.x <= r.position.x + r.size.x &&
           p.y <= r.position.y + r.size.y;
}
inline bool overlaps(Rect a, Rect b) {
    return a.position.x < b.position.x + b.size.x && a.position.x + a.size.x > b.position.x &&
           a.position.y < b.position.y + b.size.y && a.position.y + a.size.y > b.position.y;
}

struct Transform2D {
    Vec2 position{};
    Vec2 scale{1.0F, 1.0F};
    float rotationDegrees{};
    friend bool operator==(const Transform2D &, const Transform2D &) = default;
};
// Child-in-parent transform -> world. Scale is applied before rotation; shear from non-uniform
// parent scale combined with child rotation is intentionally not represented.
inline Transform2D compose(const Transform2D &parent, const Transform2D &local) {
    return {parent.position + rotated(hadamard(parent.scale, local.position),
                                      degreesToRadians(parent.rotationDegrees)),
            hadamard(parent.scale, local.scale), parent.rotationDegrees + local.rotationDegrees};
}
// Inverse of compose(parent, x) applied to a world transform; zero parent scale yields zero.
inline Transform2D relativeTo(const Transform2D &parent, const Transform2D &world) {
    const Vec2 offset =
        rotated(world.position - parent.position, -degreesToRadians(parent.rotationDegrees));
    const auto divide = [](float a, float b) { return b != 0.0F ? a / b : 0.0F; };
    return {{divide(offset.x, parent.scale.x), divide(offset.y, parent.scale.y)},
            {divide(world.scale.x, parent.scale.x), divide(world.scale.y, parent.scale.y)},
            world.rotationDegrees - parent.rotationDegrees};
}
inline Vec2 transformPoint(const Transform2D &t, Vec2 local) {
    return t.position + rotated(hadamard(t.scale, local), degreesToRadians(t.rotationDegrees));
}
inline Vec2 inverseTransformPoint(const Transform2D &t, Vec2 world) {
    const Vec2 local = rotated(world - t.position, -degreesToRadians(t.rotationDegrees));
    return {t.scale.x != 0.0F ? local.x / t.scale.x : 0.0F,
            t.scale.y != 0.0F ? local.y / t.scale.y : 0.0F};
}
} // namespace yk
