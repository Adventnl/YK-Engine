#pragma once
#include <string>
#include <vector>

namespace yk {
// Where gameplay sends sound requests. `path` is a project-relative sound file or a procedural
// "tone:<hz>,<seconds>[,sine|square|saw|noise]" placeholder that needs no asset at all. Requests
// never fail the game: an unknown or unplayable sound is reported once and skipped.
class AudioSink {
  public:
    virtual ~AudioSink() = default;
    virtual void play(const std::string &path, float volume = 1.0F, bool loop = false) = 0;
    virtual void stopAll() = 0;
};

class NullAudio final : public AudioSink {
  public:
    void play(const std::string &, float, bool) override {}
    void stopAll() override {}
};

// Remembers requests so tests can assert on them.
class RecordingAudio final : public AudioSink {
  public:
    struct Request {
        std::string path;
        float volume;
        bool loop;
    };
    std::vector<Request> requests;
    int stops{};
    void play(const std::string &path, float volume, bool loop) override {
        requests.push_back({path, volume, loop});
    }
    void stopAll() override {
        ++stops;
    }
    int count(const std::string &path) const {
        int total = 0;
        for (const Request &request : requests)
            total += request.path == path ? 1 : 0;
        return total;
    }
};
} // namespace yk
