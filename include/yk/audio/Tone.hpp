#pragma once
#include <optional>
#include <string>
#include <vector>

namespace yk {
enum class ToneWave { Sine, Square, Saw, Noise };
struct ToneSpec {
    float frequency{440.0F}; // Hz
    float seconds{0.15F};
    ToneWave wave{ToneWave::Sine};
};
inline constexpr int toneSampleRate = 48000;
// Parses a procedural placeholder sound: "tone:<hz>,<seconds>[,sine|square|saw|noise]".
// Frequency must be 20-20000 Hz and duration up to 5 s; anything else yields nullopt.
std::optional<ToneSpec> parseTone(const std::string &text);
// Mono samples in [-1, 1] at toneSampleRate with a short attack and exponential decay, so tones
// never click. Deterministic (noise uses a fixed-seed generator).
std::vector<float> renderTone(const ToneSpec &spec);
} // namespace yk
