#pragma once
#include "yk/animation/Animator.hpp"
#include "yk/core/Json.hpp"
#include <string>
#include <vector>

namespace yk {
// A sprite sheet layout plus its clips, loaded from a ".ykanim" JSON asset:
//
//   {"format":"yk.animation","version":2,
//    "texture":"assets/characters/hero.png","columns":8,"rows":4,
//    "clips":[
//      {"name":"idle","frames":[0,1,2,1],"fps":6},
//      {"name":"run","first":8,"count":8,"fps":14},
//      {"name":"land","first":16,"count":3,"durations":[0.05,0.05,0.1],"loop":false,"next":"idle"},
//      {"name":"step","first":24,"count":4,"fps":10,
//       "events":[{"frame":1,"name":"footstep","sound":"assets/audio/step.wav"}]}]}
//
// Version 1 (no texture, only first/count/fps) still loads. Art can be replaced by editing this
// file and the texture; gameplay only names clips (or drives an AnimationController that does).
struct AnimationSet {
    std::string
        texture; // Project-relative sheet the AnimatedSprite applies; empty keeps the sprite's own.
    int columns{1};
    int rows{1};
    std::vector<AnimationClip> clips;

    const AnimationClip *find(const std::string &name) const {
        for (const AnimationClip &clip : clips)
            if (clip.name == name)
                return &clip;
        return nullptr;
    }
};
Result<AnimationSet> parseAnimationSet(const Json &document);
} // namespace yk
