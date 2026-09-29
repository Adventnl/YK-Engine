#include "yk/audio/SdlAudio.hpp"
#include "yk/audio/Tone.hpp"
#include "yk/core/Log.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace yk {
namespace {
constexpr std::size_t maxVoices = 16;

struct StreamDeleter {
    void operator()(SDL_AudioStream *stream) const {
        SDL_DestroyAudioStream(stream);
    }
};
struct Sound {
    SDL_AudioSpec spec{};
    std::vector<Uint8> data;
};
struct Voice {
    std::unique_ptr<SDL_AudioStream, StreamDeleter> stream;
    const Sound *sound{};
    bool loop{};
};
} // namespace

struct SdlAudio::Impl {
    const AssetSource *assets{};
    SDL_AudioDeviceID device{};
    SDL_AudioSpec deviceSpec{};
    std::map<std::string, Sound> sounds;
    std::set<std::string> reportedMissing;
    std::vector<Voice> voices;

    const Sound *load(const std::string &path) {
        if (const auto cached = sounds.find(path); cached != sounds.end())
            return cached->second.data.empty() ? nullptr : &cached->second;
        Sound sound;
        std::string failure;
        if (const auto tone = parseTone(path)) {
            const auto samples = renderTone(*tone);
            sound.spec = {SDL_AUDIO_F32, 1, toneSampleRate};
            sound.data.resize(samples.size() * sizeof(float));
            SDL_memcpy(sound.data.data(), samples.data(), sound.data.size());
        } else if (path.rfind("tone:", 0) == 0) {
            failure = "malformed tone";
        } else if (!assets) {
            failure = "no asset source";
        } else {
            const auto file = assets->filePath(path);
            Uint8 *buffer = nullptr;
            Uint32 length = 0;
            if (file.empty()) {
                failure = "not a file-backed asset";
            } else if (!SDL_LoadWAV(file.string().c_str(), &sound.spec, &buffer, &length)) {
                failure = SDL_GetError();
            } else {
                sound.data.assign(buffer, buffer + length);
                SDL_free(buffer);
            }
        }
        if (sound.data.empty() && reportedMissing.insert(path).second)
            log(LogLevel::Warning, "audio", "Cannot play '" + path + "': " + failure);
        Sound &stored = sounds[path]; // Failures are cached too, so a bad path is reported once.
        stored = std::move(sound);
        return stored.data.empty() ? nullptr : &stored;
    }
};

SdlAudio::SdlAudio() : impl_(std::make_unique<Impl>()) {}
SdlAudio::~SdlAudio() {
    impl_->voices.clear(); // Streams before the device.
    if (impl_->device != 0)
        SDL_CloseAudioDevice(impl_->device);
}
std::unique_ptr<SdlAudio> SdlAudio::create(const AssetSource *assets) {
    auto audio = std::unique_ptr<SdlAudio>(new SdlAudio());
    audio->impl_->assets = assets;
    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        log(LogLevel::Warning, "audio", "Audio subsystem unavailable; sound is disabled");
        return audio;
    }
    audio->impl_->device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (audio->impl_->device == 0) {
        log(LogLevel::Warning, "audio", std::string("No audio output device: ") + SDL_GetError());
        return audio;
    }
    int frames = 0;
    SDL_GetAudioDeviceFormat(audio->impl_->device, &audio->impl_->deviceSpec, &frames);
    return audio;
}
bool SdlAudio::available() const {
    return impl_->device != 0;
}
std::size_t SdlAudio::activeVoices() const {
    return impl_->voices.size();
}
void SdlAudio::playSound(const std::string &path, float volume, bool loop) {
    if (impl_->device == 0 || !(volume > 0.0F))
        return;
    const Sound *sound = impl_->load(path);
    if (!sound)
        return;
    if (impl_->voices.size() >= maxVoices) // Steal the oldest voice.
        impl_->voices.erase(impl_->voices.begin());
    std::unique_ptr<SDL_AudioStream, StreamDeleter> stream(
        SDL_CreateAudioStream(&sound->spec, &impl_->deviceSpec));
    if (!stream || !SDL_BindAudioStream(impl_->device, stream.get()))
        return;
    SDL_SetAudioStreamGain(stream.get(), std::min(volume, 4.0F));
    SDL_PutAudioStreamData(stream.get(), sound->data.data(), static_cast<int>(sound->data.size()));
    if (!loop)
        SDL_FlushAudioStream(stream.get());
    impl_->voices.push_back({std::move(stream), sound, loop});
}
void SdlAudio::stopAll() {
    impl_->voices.clear();
}
void SdlAudio::update() {
    for (Voice &voice : impl_->voices) {
        if (voice.loop && SDL_GetAudioStreamQueued(voice.stream.get()) <
                              static_cast<int>(voice.sound->data.size()))
            SDL_PutAudioStreamData(voice.stream.get(), voice.sound->data.data(),
                                   static_cast<int>(voice.sound->data.size()));
    }
    std::erase_if(impl_->voices, [](const Voice &voice) {
        return !voice.loop && SDL_GetAudioStreamAvailable(voice.stream.get()) == 0 &&
               SDL_GetAudioStreamQueued(voice.stream.get()) == 0;
    });
}
} // namespace yk
