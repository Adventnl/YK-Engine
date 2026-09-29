#include "yk/core/Color.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
namespace {
int hexDigit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
} // namespace
std::optional<Color> parseColor(std::string_view text) {
    if ((text.size() != 7 && text.size() != 9) || text.front() != '#')
        return std::nullopt;
    std::uint8_t channels[4] = {0, 0, 0, 255};
    for (std::size_t i = 0; i < (text.size() - 1) / 2; ++i) {
        const int high = hexDigit(text[1 + i * 2]), low = hexDigit(text[2 + i * 2]);
        if (high < 0 || low < 0)
            return std::nullopt;
        channels[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return Color{channels[0], channels[1], channels[2], channels[3]};
}
std::string formatColor(Color color) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out = "#";
    for (const std::uint8_t channel : {color.r, color.g, color.b, color.a}) {
        out.push_back(digits[channel >> 4]);
        out.push_back(digits[channel & 15]);
    }
    return out;
}
Color lerp(Color from, Color to, float t) {
    t = std::clamp(t, 0.0F, 1.0F);
    const auto mix = [t](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>(std::lround(
            static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t));
    };
    return {mix(from.r, to.r), mix(from.g, to.g), mix(from.b, to.b), mix(from.a, to.a)};
}
} // namespace yk
