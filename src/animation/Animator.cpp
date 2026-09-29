#include "yk/animation/Animator.hpp"
#include <cmath>

namespace yk {
Status Animator::define(AnimationClip clip) {
    if (clip.name.empty() || clip.firstFrame < 0 || clip.frameCount <= 0 ||
        !std::isfinite(clip.frameSeconds) || clip.frameSeconds <= 0)
        return Error{"Invalid animation clip"};
    const auto name = clip.name;
    clips_.insert_or_assign(name, std::move(clip));
    return success();
}
bool Animator::has(const std::string &name) const {
    return clips_.contains(name);
}
Status Animator::play(const std::string &name) {
    if (!clips_.contains(name))
        return Error{"Unknown animation '" + name + "'"};
    if (active_ != name) {
        active_ = name;
        elapsed_ = 0;
        index_ = 0;
        completed_ = emitted_ = false;
    }
    return success();
}
void Animator::tick(float seconds) {
    const auto found = clips_.find(active_);
    if (found == clips_.end() || !(seconds > 0) || completed_)
        return;
    const AnimationClip &clip = found->second;
    elapsed_ += seconds;
    while (elapsed_ >= clip.frameSeconds) {
        elapsed_ -= clip.frameSeconds;
        if (index_ + 1 < clip.frameCount) {
            ++index_;
        } else if (clip.loop) {
            index_ = 0;
        } else {
            completed_ = true;
            index_ = clip.frameCount - 1;
            break;
        }
    }
}
int Animator::frame() const {
    const auto found = clips_.find(active_);
    return found == clips_.end() ? 0 : found->second.firstFrame + index_;
}
const std::string &Animator::current() const {
    return active_;
}
bool Animator::takeCompletion() {
    if (completed_ && !emitted_) {
        emitted_ = true;
        return true;
    }
    return false;
}
} // namespace yk
