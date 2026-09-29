#pragma once
#include "yk/core/Result.hpp"
#include <map>
#include <string>
#include <vector>

namespace yk {
// Something that happens when a clip reaches a frame: a footstep sound, a spark. `frame` is the
// index within the clip (0 is its first frame).
struct ClipEvent {
    int frame{};
    std::string name;  // Raised on the event bus with the animated entity as its source.
    std::string sound; // Optional sound to play (project-relative path or a tone: placeholder).
};

// A run of sprite-sheet cells with timing. The simple form is `firstFrame`, `frameCount` and
// `frameSeconds` (consecutive cells at one rate); `frames` lists arbitrary cells and `durations`
// gives each frame its own time, for hand-timed animation. Frame numbering is the caller's (an
// index into the sheet grid); the Animator only decides which index is current.
struct AnimationClip {
    std::string name;
    int firstFrame{}, frameCount{1};
    float frameSeconds{0.1F};
    bool loop{true};
    std::vector<int>
        frames; // Non-empty: these cells, in this order (overrides firstFrame/frameCount).
    std::vector<float>
        durations;    // Non-empty: seconds per frame (one per frame; overrides frameSeconds).
    std::string next; // A non-looping clip continues with this one when it ends.
    std::vector<ClipEvent> events;

    int length() const {
        return frames.empty() ? frameCount : static_cast<int>(frames.size());
    }
    int cell(int index) const {
        return frames.empty() ? firstFrame + index : frames[static_cast<std::size_t>(index)];
    }
    float duration(int index) const {
        return durations.empty() ? frameSeconds : durations[static_cast<std::size_t>(index)];
    }
    float totalSeconds() const;
};

// Plays one clip at a time. Time advances only through tick(), so a caller that ticks on fixed
// simulation steps gets deterministic animation.
class Animator {
  public:
    Status define(AnimationClip clip); // Replaces a clip with the same name.
    bool has(const std::string &name) const;
    const AnimationClip *clip(const std::string &name) const;
    Status play(const std::string &name); // Restarts only when the clip changes.
    // Starts the clip from its first frame even when it is already playing.
    Status restart(const std::string &name);
    void tick(float seconds);
    int frame() const; // Sheet cell of the current frame.
    const std::string &current() const;
    // How far through the clip we are: whole cycles plus the fraction of the current one (a
    // finished non-looping clip is 1).
    float normalizedTime() const;
    // True exactly once after a non-looping clip reaches its last frame.
    bool takeCompletion();
    // Events of the frames entered since the last call, in order.
    std::vector<ClipEvent> takeEvents();

  private:
    void enter(const AnimationClip &clip, int index);
    Status begin(const std::string &name);

    std::map<std::string, AnimationClip> clips_;
    std::string active_;
    float elapsed_{};
    int index_{};
    int cycles_{};
    int completions_{};
    bool completed_{};
    std::vector<ClipEvent> events_;
};
} // namespace yk
