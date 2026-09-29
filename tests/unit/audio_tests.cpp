#include "support/check.hpp"
#include "yk/audio/Audio.hpp"
#include "yk/audio/Tone.hpp"
#include <algorithm>
#include <cmath>

using namespace yk;

int main() {
    // Parsing.
    const auto plain = parseTone("tone:440,0.25");
    CHECK(plain && plain->frequency == 440.0F && plain->seconds == 0.25F &&
          plain->wave == ToneWave::Sine);
    CHECK(parseTone("tone:880,0.1,square")->wave == ToneWave::Square);
    CHECK(parseTone("tone:100,1,saw")->wave == ToneWave::Saw &&
          parseTone("tone:100,1,noise")->wave == ToneWave::Noise);
    for (const char *bad :
         {"", "tone", "tone:", "tone:440", "tone:440,", "tone:abc,1", "tone:440,abc", "tone:10,1",
          "tone:30000,1", "tone:440,0", "tone:440,-1", "tone:440,6", "tone:440,1,triangle",
          "tone:440,1,sine,extra", "sound.wav", "tone:nan,1", "tone:440,inf"})
        CHECK(!parseTone(bad));

    // Synthesis.
    const auto samples = renderTone({440.0F, 0.2F, ToneWave::Sine});
    CHECK(samples.size() == static_cast<std::size_t>(0.2F * toneSampleRate));
    float peak = 0.0F;
    for (const float sample : samples) {
        CHECK(std::isfinite(sample));
        peak = std::max(peak, std::fabs(sample));
    }
    CHECK(peak > 0.2F && peak <= 0.4F + 1e-4F); // Audible but never clipping.
    CHECK(std::fabs(samples.front()) < 0.02F &&
          std::fabs(samples.back()) < 0.02F);                     // No clicks at the ends.
    CHECK(renderTone({440.0F, 0.2F, ToneWave::Sine}) == samples); // Deterministic.
    CHECK(renderTone({300.0F, 0.1F, ToneWave::Noise}) ==
          renderTone({300.0F, 0.1F, ToneWave::Noise}));
    const auto noise = renderTone({300.0F, 0.1F, ToneWave::Noise});
    CHECK(std::any_of(noise.begin(), noise.end(), [](float s) { return s > 0.05F; }) &&
          std::any_of(noise.begin(), noise.end(), [](float s) { return s < -0.05F; }));
    // A 1 kHz sine has 20 zero crossings per 10 ms.
    const auto sine = renderTone({1000.0F, 0.5F, ToneWave::Sine});
    int crossings = 0;
    for (std::size_t i = 1; i < 480 + 1; ++i)
        crossings += (sine[i - 1] < 0) != (sine[i] < 0) ? 1 : 0;
    CHECK(crossings >= 19 && crossings <= 21);
    CHECK(renderTone({440.0F, 0.0001F, ToneWave::Square}).size() >= 1);

    // Sinks.
    NullAudio quiet;
    quiet.play("anything");
    quiet.stopAll();
    RecordingAudio recorder;
    recorder.play("a", 0.5F, true);
    recorder.play("a");
    recorder.play("b");
    recorder.stopAll();
    CHECK(recorder.count("a") == 2 && recorder.count("b") == 1 && recorder.stops == 1 &&
          recorder.requests[0].volume == 0.5F && recorder.requests[0].loop);
    return yk::test::finish("audio");
}
