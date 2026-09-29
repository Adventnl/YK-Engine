#include "support/check.hpp"
#include "yk/animation/Animator.hpp"

using yk::Animator;

int main() {
    Animator animator;
    CHECK(!animator.define({"", 0, 1, 0.1F, true}) && !animator.define({"x", -1, 1, 0.1F, true}) &&
          !animator.define({"x", 0, 0, 0.1F, true}) && !animator.define({"x", 0, 1, 0.0F, true}));
    CHECK(animator.define({"once", 2, 3, 0.1F, false}));
    CHECK(animator.define({"loop", 10, 2, 0.1F, true}));
    CHECK(!animator.play("missing") && animator.has("once") && !animator.has("missing"));
    CHECK(animator.frame() == 0 && animator.current().empty()); // Nothing selected yet.
    animator.tick(1.0F);                                        // Harmless without a clip.

    CHECK(animator.play("once"));
    animator.tick(0.05F);
    CHECK(animator.frame() == 2);
    animator.tick(0.06F);
    CHECK(animator.frame() == 3);
    animator.tick(0.35F);
    CHECK(animator.frame() == 4 && animator.takeCompletion() && !animator.takeCompletion());
    animator.tick(5.0F);
    CHECK(animator.frame() == 4); // Non-looping clip holds its last frame.

    CHECK(animator.play("loop"));
    animator.tick(0.25F);
    CHECK(animator.frame() == 10); // 2 frames of 0.1s: 0.25s wraps back to the first frame.
    animator.tick(0.1F);
    CHECK(animator.frame() == 11);
    CHECK(animator.play("loop") &&
          animator.frame() == 11); // Re-playing the same clip keeps its place.
    CHECK(animator.play("once") && animator.frame() == 2); // Switching restarts.
    animator.tick(-1.0F);
    animator.tick(0.0F);
    CHECK(animator.frame() == 2);
    return yk::test::finish("animation");
}
