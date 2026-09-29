#include "yk/animation/AnimationSet.hpp"
#include <cmath>

namespace yk {
Result<AnimationSet> parseAnimationSet(const Json &document) {
    if (document.get("format").asString() != "yk.animation" || document.get("version").asInt() != 1)
        return Error{"Not a yk.animation version 1 document"};
    AnimationSet set;
    set.columns = static_cast<int>(document.get("columns").asInt(1));
    set.rows = static_cast<int>(document.get("rows").asInt(1));
    if (set.columns < 1 || set.rows < 1 || set.columns > 256 || set.rows > 256)
        return Error{"Animation sheet columns/rows must be within 1-256"};
    if (!document.get("clips").isArray() || document.get("clips").size() == 0)
        return Error{"Animation needs a non-empty 'clips' array"};
    for (const Json &item : document.get("clips").items()) {
        AnimationClip clip;
        clip.name = item.get("name").asString();
        clip.firstFrame = static_cast<int>(item.get("first").asInt(0));
        clip.frameCount = static_cast<int>(item.get("count").asInt(1));
        const double fps = item.get("fps").asNumber(10.0);
        clip.loop = item.get("loop").asBool(true);
        if (!(fps > 0.0) || !std::isfinite(fps))
            return Error{"Clip '" + clip.name + "' needs a positive fps"};
        clip.frameSeconds = static_cast<float>(1.0 / fps);
        if (clip.firstFrame + clip.frameCount > set.columns * set.rows)
            return Error{"Clip '" + clip.name + "' runs past the end of the sprite sheet"};
        Animator probe; // Reuse the Animator's clip validation.
        if (auto status = probe.define(clip); !status)
            return Error{"Clip '" + clip.name + "': " + status.error()};
        set.clips.push_back(std::move(clip));
    }
    return set;
}
} // namespace yk
