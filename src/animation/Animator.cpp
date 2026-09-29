#include "yk/animation/Animator.hpp"
#include <cmath>

namespace yk {
float AnimationClip::totalSeconds() const {
    float total = 0.0F;
    for (int i = 0; i < length(); ++i)
        total += duration(i);
    return total;
}

Status Animator::define(AnimationClip clip) {
    if (clip.name.empty())
        return Error{"Invalid animation clip: it needs a name"};
    if (!std::isfinite(clip.frameSeconds) || clip.frameSeconds <= 0)
        return Error{"Invalid animation clip: frame time must be positive"};
    if (clip.frames.empty()) {
        if (clip.firstFrame < 0 || clip.frameCount <= 0)
            return Error{"Invalid animation clip: first frame and count must be positive"};
    } else {
        for (const int cell : clip.frames)
            if (cell < 0)
                return Error{"Invalid animation clip: frame numbers cannot be negative"};
    }
    if (!clip.durations.empty()) {
        if (static_cast<int>(clip.durations.size()) != clip.length())
            return Error{"Invalid animation clip: needs one duration per frame"};
        for (const float seconds : clip.durations)
            if (!std::isfinite(seconds) || seconds <= 0)
                return Error{"Invalid animation clip: frame durations must be positive"};
    }
    for (const ClipEvent &event : clip.events)
        if (event.frame < 0 || event.frame >= clip.length() || event.name.empty())
            return Error{
                "Invalid animation clip: an event needs a name and a frame inside the clip"};
    if (clip.next == clip.name)
        return Error{"Invalid animation clip: 'next' cannot be the clip itself (use loop)"};
    const auto name = clip.name;
    clips_.insert_or_assign(name, std::move(clip));
    return success();
}
bool Animator::has(const std::string &name) const {
    return clips_.contains(name);
}
const AnimationClip *Animator::clip(const std::string &name) const {
    const auto found = clips_.find(name);
    return found == clips_.end() ? nullptr : &found->second;
}

void Animator::enter(const AnimationClip &clip, int index) {
    for (const ClipEvent &event : clip.events)
        if (event.frame == index)
            events_.push_back(event);
}

Status Animator::begin(const std::string &name) {
    const auto found = clips_.find(name);
    if (found == clips_.end())
        return Error{"Unknown animation '" + name + "'"};
    active_ = name;
    elapsed_ = 0;
    index_ = 0;
    cycles_ = 0;
    completed_ = false;
    enter(found->second, 0);
    return success();
}
Status Animator::play(const std::string &name) {
    if (!clips_.contains(name))
        return Error{"Unknown animation '" + name + "'"};
    if (active_ == name)
        return success();
    completions_ = 0;
    return begin(name);
}
Status Animator::restart(const std::string &name) {
    if (!clips_.contains(name))
        return Error{"Unknown animation '" + name + "'"};
    completions_ = 0;
    return begin(name);
}

void Animator::tick(float seconds) {
    const auto found = clips_.find(active_);
    if (found == clips_.end() || !(seconds > 0) || !std::isfinite(seconds) || completed_)
        return;
    const AnimationClip &clip = found->second;
    elapsed_ += seconds;
    // Bounded, so a pathological clip can never hang a frame.
    for (int guard = 0; guard < 100000; ++guard) {
        const float frameTime = clip.duration(index_);
        if (elapsed_ < frameTime)
            return;
        elapsed_ -= frameTime;
        if (index_ + 1 < clip.length()) {
            ++index_;
            enter(clip, index_);
        } else if (clip.loop) {
            index_ = 0;
            ++cycles_;
            enter(clip, 0);
        } else {
            completed_ = true;
            ++completions_;
            index_ = clip.length() - 1;
            elapsed_ = 0;
            if (!clip.next.empty() && clips_.contains(clip.next))
                begin(clip.next); // Keeps the completion count: the finished clip still counts.
            return;
        }
    }
}

int Animator::frame() const {
    const auto found = clips_.find(active_);
    return found == clips_.end() ? 0 : found->second.cell(index_);
}
const std::string &Animator::current() const {
    return active_;
}
float Animator::normalizedTime() const {
    const auto found = clips_.find(active_);
    if (found == clips_.end())
        return 0.0F;
    const AnimationClip &clip = found->second;
    if (completed_)
        return 1.0F;
    float before = 0.0F;
    for (int i = 0; i < index_; ++i)
        before += clip.duration(i);
    const float total = clip.totalSeconds();
    return static_cast<float>(cycles_) + (total > 0.0F ? (before + elapsed_) / total : 0.0F);
}
bool Animator::takeCompletion() {
    if (completions_ > 0) {
        --completions_;
        return true;
    }
    return false;
}
std::vector<ClipEvent> Animator::takeEvents() {
    std::vector<ClipEvent> taken;
    taken.swap(events_);
    return taken;
}
} // namespace yk
