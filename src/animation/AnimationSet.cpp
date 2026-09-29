#include "yk/animation/AnimationSet.hpp"
#include <cmath>

namespace yk {
namespace {
Result<AnimationClip> parseClip(const Json &item, int cells) {
    if (!item.isObject())
        return Error{"Each clip must be an object"};
    AnimationClip clip;
    clip.name = item.get("name").asString();
    const std::string where = "Clip '" + clip.name + "': ";
    if (clip.name.empty())
        return Error{"Every clip needs a 'name'"};
    clip.loop = item.get("loop").asBool(true);
    clip.next = item.get("next").asString();

    if (const Json *frames = item.find("frames")) {
        if (!frames->isArray() || frames->size() == 0)
            return Error{where + "'frames' must be a non-empty array of sheet cells"};
        for (const Json &cell : frames->items()) {
            if (!cell.isNumber() || cell.asNumber() != std::floor(cell.asNumber()))
                return Error{where + "'frames' entries must be whole numbers"};
            clip.frames.push_back(static_cast<int>(cell.asInt()));
        }
    } else {
        clip.firstFrame = static_cast<int>(item.get("first").asInt(0));
        clip.frameCount = static_cast<int>(item.get("count").asInt(1));
    }
    const double fps = item.get("fps").asNumber(10.0);
    if (!(fps > 0.0) || !std::isfinite(fps))
        return Error{where + "needs a positive fps"};
    clip.frameSeconds = static_cast<float>(1.0 / fps);
    if (const Json *durations = item.find("durations")) {
        if (!durations->isArray())
            return Error{where + "'durations' must be an array of seconds"};
        for (const Json &seconds : durations->items()) {
            if (!seconds.isNumber())
                return Error{where + "'durations' entries must be numbers"};
            clip.durations.push_back(static_cast<float>(seconds.asNumber()));
        }
    }
    if (const Json *events = item.find("events")) {
        if (!events->isArray())
            return Error{where + "'events' must be an array"};
        for (const Json &event : events->items()) {
            if (!event.isObject() || !event.get("name").isString() || !event.get("frame").isNumber())
                return Error{where + "each event needs a 'frame' number and a 'name'"};
            clip.events.push_back({static_cast<int>(event.get("frame").asInt()),
                                   event.get("name").asString(), event.get("sound").asString()});
        }
    }
    // Reuse the Animator's own validation so a clip that loads is a clip that plays.
    Animator probe;
    if (auto status = probe.define(clip); !status)
        return Error{where + status.error()};
    for (int i = 0; i < clip.length(); ++i)
        if (clip.cell(i) >= cells)
            return Error{where + "runs past the end of the sprite sheet (cell " +
                         std::to_string(clip.cell(i)) + " of " + std::to_string(cells) + ")"};
    return clip;
}
} // namespace

Result<AnimationSet> parseAnimationSet(const Json &document) {
    const auto version = document.get("version").asInt();
    if (document.get("format").asString() != "yk.animation" || (version != 1 && version != 2))
        return Error{"Not a yk.animation version 1 or 2 document"};
    AnimationSet set;
    set.texture = document.get("texture").asString();
    set.columns = static_cast<int>(document.get("columns").asInt(1));
    set.rows = static_cast<int>(document.get("rows").asInt(1));
    if (set.columns < 1 || set.rows < 1 || set.columns > 256 || set.rows > 256)
        return Error{"Animation sheet columns/rows must be within 1-256"};
    if (!document.get("clips").isArray() || document.get("clips").size() == 0)
        return Error{"Animation needs a non-empty 'clips' array"};
    for (const Json &item : document.get("clips").items()) {
        auto clip = parseClip(item, set.columns * set.rows);
        if (!clip)
            return Error{clip.error()};
        if (set.find(clip.value().name))
            return Error{"Clip '" + clip.value().name + "' is defined twice"};
        set.clips.push_back(std::move(clip.value()));
    }
    for (const AnimationClip &clip : set.clips)
        if (!clip.next.empty() && !set.find(clip.next))
            return Error{"Clip '" + clip.name + "' continues with '" + clip.next +
                         "', which does not exist"};
    return set;
}
} // namespace yk
