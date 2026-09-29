#pragma once
#include "yk/assets/AssetSource.hpp"
#include "yk/audio/Audio.hpp"
#include <memory>

namespace yk {
// AudioSink backed by SDL3: mixes up to 16 simultaneous sounds (project .wav files or procedural
// "tone:" placeholders) on the default output device. Without a usable device it still works as a
// sink that plays nothing, so a machine with no audio never breaks the game.
class SdlAudio final : public AudioSink {
  public:
    // SDL's audio subsystem must already be initialized (Application does this when it can).
    // `assets` resolves file-backed sounds and may be null.
    static std::unique_ptr<SdlAudio> create(const AssetSource *assets);
    ~SdlAudio() override;
    SdlAudio(const SdlAudio &) = delete;
    SdlAudio &operator=(const SdlAudio &) = delete;

    void stopAll() override;
    // Retires finished sounds and refills looping ones; call once per frame.
    void update();
    bool available() const;
    std::size_t activeVoices() const;

  protected:
    void playSound(const std::string &path, float volume, bool loop) override;

  private:
    SdlAudio();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace yk
