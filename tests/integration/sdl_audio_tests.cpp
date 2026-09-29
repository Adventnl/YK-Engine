// Exercises SdlAudio against SDL's dummy audio driver: no sound hardware is needed.
#include "support/check.hpp"
#include "yk/audio/SdlAudio.hpp"
#include "yk/core/Log.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <thread>

using namespace yk;

int main() {
    setLogStderrEnabled(false);
    {
        // Without an initialized audio subsystem the sink degrades to silence.
        auto silent = SdlAudio::create(nullptr);
        CHECK(!silent->available());
        silent->play("tone:440,0.1");
        silent->update();
        CHECK(silent->activeVoices() == 0);
    }
    CHECK(SDL_Init(SDL_INIT_AUDIO));
    {
        MemoryAssets unused;
        auto audio = SdlAudio::create(&unused);
        CHECK(audio->available());
        audio->play("tone:440,0.05");
        audio->play("tone:660,0.05,square", 0.5F);
        CHECK(audio->activeVoices() == 2);
        audio->play("missing.wav"); // Unreadable: reported, not played.
        audio->play("tone:garbage");
        audio->play("tone:440,0.05", 0.0F); // Silent requests are skipped.
        CHECK(audio->activeVoices() == 2);
        for (int i = 0; i < 100 && audio->activeVoices() > 0;
             ++i) { // The dummy device drains in real time.
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            audio->update();
        }
        CHECK(audio->activeVoices() == 0);
        audio->play("tone:220,0.05", 1.0F, true);
        audio->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        audio->update();
        CHECK(audio->activeVoices() == 1); // A looping sound keeps playing.
        audio->stopAll();
        CHECK(audio->activeVoices() == 0);
        for (int i = 0; i < 40; ++i)
            audio->play("tone:440,1.0");
        CHECK(audio->activeVoices() <= 16); // Voice stealing bounds the mixer.
    }
    SDL_Quit();
    return yk::test::finish("sdl_audio");
}
