#include "yk/audio/Tone.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace yk {
std::optional<ToneSpec> parseTone(const std::string &text) {
    constexpr std::string_view prefix = "tone:";
    if (text.rfind(prefix, 0) != 0)
        return std::nullopt;
    std::vector<std::string> parts;
    std::size_t start = prefix.size();
    for (;;) {
        const auto comma = text.find(',', start);
        parts.push_back(
            text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    if (parts.size() < 2 || parts.size() > 3)
        return std::nullopt;
    const auto number = [](const std::string &part) -> std::optional<float> {
        if (part.empty())
            return std::nullopt;
        char *end = nullptr;
        const double value = std::strtod(part.c_str(), &end);
        if (end != part.c_str() + part.size() || !std::isfinite(value))
            return std::nullopt;
        return static_cast<float>(value);
    };
    const auto frequency = number(parts[0]);
    const auto seconds = number(parts[1]);
    if (!frequency || !seconds || *frequency < 20.0F || *frequency > 20000.0F || *seconds <= 0.0F ||
        *seconds > 5.0F)
        return std::nullopt;
    ToneSpec spec{*frequency, *seconds, ToneWave::Sine};
    if (parts.size() == 3) {
        if (parts[2] == "sine")
            spec.wave = ToneWave::Sine;
        else if (parts[2] == "square")
            spec.wave = ToneWave::Square;
        else if (parts[2] == "saw")
            spec.wave = ToneWave::Saw;
        else if (parts[2] == "noise")
            spec.wave = ToneWave::Noise;
        else
            return std::nullopt;
    }
    return spec;
}

std::vector<float> renderTone(const ToneSpec &spec) {
    const auto count =
        static_cast<std::size_t>(std::max(1.0F, spec.seconds * static_cast<float>(toneSampleRate)));
    std::vector<float> samples(count);
    std::uint32_t noise = 0x12345678U; // xorshift32: deterministic across platforms.
    constexpr float twoPi = 6.28318530718F;
    const float attack = 0.004F * static_cast<float>(toneSampleRate);
    for (std::size_t i = 0; i < count; ++i) {
        const float time = static_cast<float>(i) / static_cast<float>(toneSampleRate);
        const float phase = std::fmod(time * spec.frequency, 1.0F);
        float value = 0.0F;
        switch (spec.wave) {
        case ToneWave::Sine:
            value = std::sin(twoPi * phase);
            break;
        case ToneWave::Square:
            value = phase < 0.5F ? 1.0F : -1.0F;
            break;
        case ToneWave::Saw:
            value = 2.0F * phase - 1.0F;
            break;
        case ToneWave::Noise:
            noise ^= noise << 13;
            noise ^= noise >> 17;
            noise ^= noise << 5;
            value = static_cast<float>(noise) / 2147483648.0F - 1.0F;
            break;
        }
        const float fadeIn = std::min(1.0F, static_cast<float>(i) / attack);
        const float decay = std::exp(-4.0F * time / spec.seconds);
        const float fadeOut = std::min(1.0F, static_cast<float>(count - 1 - i) / attack);
        samples[i] = 0.4F * value * fadeIn * decay * fadeOut;
    }
    return samples;
}
} // namespace yk
