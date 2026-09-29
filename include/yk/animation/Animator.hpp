#pragma once
#include "yk/core/Result.hpp"
#include <map>
#include <string>

namespace yk {
// A run of consecutive frames in a sprite sheet. Frame numbering is the caller's (an index into an
// atlas grid); the Animator only decides which index is current.
struct AnimationClip {
    std::string name;
    int firstFrame{}, frameCount{1};
    float frameSeconds{0.1F};
    bool loop{true};
};
// Plays one clip at a time. Time advances only through tick(), so a caller that ticks on fixed
// simulation steps gets deterministic animation.
class Animator {
  public:
    Status define(AnimationClip clip); // Replaces a clip with the same name.
    bool has(const std::string &name) const;
    Status play(const std::string &name); // Restarts only when the clip changes.
    void tick(float seconds);
    int frame() const;
    const std::string &current() const;
    // True exactly once after a non-looping clip reaches its last frame.
    bool takeCompletion();

  private:
    std::map<std::string, AnimationClip> clips_;
    std::string active_;
    float elapsed_{};
    int index_{};
    bool completed_{}, emitted_{};
};
} // namespace yk
