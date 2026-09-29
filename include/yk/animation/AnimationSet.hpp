#pragma once
#include "yk/animation/Animator.hpp"
#include "yk/core/Json.hpp"
#include <vector>

namespace yk {
// A sprite sheet layout plus its clips, loaded from a ".ykanim" JSON asset:
//   {"format":"yk.animation","version":1,"columns":4,"rows":2,
//    "clips":[{"name":"run","first":1,"count":3,"fps":10,"loop":true}]}
// Art can be replaced by editing this file and the texture; gameplay only names clips.
struct AnimationSet {
    int columns{1};
    int rows{1};
    std::vector<AnimationClip> clips;
};
Result<AnimationSet> parseAnimationSet(const Json &document);
} // namespace yk
