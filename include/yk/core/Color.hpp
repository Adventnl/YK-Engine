#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace yk {
struct Color {
    std::uint8_t r{}, g{}, b{}, a{255};
    friend bool operator==(Color, Color) = default;
};
// "#RRGGBB" or "#RRGGBBAA" (case-insensitive) -> Color. Anything else yields nullopt.
std::optional<Color> parseColor(std::string_view text);
// Always "#rrggbbaa" so a round trip is lossless.
std::string formatColor(Color color);
Color lerp(Color from, Color to, float t);
} // namespace yk
