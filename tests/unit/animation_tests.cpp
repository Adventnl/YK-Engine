// Clip playback, animation assets, controller (state machine) definitions and the player that
// runs them. No window or scene is involved: time advances only through update()/tick().
#include "support/check.hpp"
#include "yk/animation/AnimationController.hpp"

using namespace yk;

namespace {
AnimationClip simpleClip(const char *name, int first, int count, float seconds, bool loop) {
    AnimationClip clip;
    clip.name = name;
    clip.firstFrame = first;
    clip.frameCount = count;
    clip.frameSeconds = seconds;
    clip.loop = loop;
    return clip;
}

void clipPlayback() {
    Animator animator;
    CHECK(!animator.define(simpleClip("", 0, 1, 0.1F, true)));
    CHECK(!animator.define(simpleClip("x", -1, 1, 0.1F, true)));
    CHECK(!animator.define(simpleClip("x", 0, 0, 0.1F, true)));
    CHECK(!animator.define(simpleClip("x", 0, 1, 0.0F, true)));
    CHECK(animator.define(simpleClip("once", 2, 3, 0.1F, false)));
    CHECK(animator.define(simpleClip("loop", 10, 2, 0.1F, true)));
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
    CHECK_NEAR(animator.normalizedTime(), 1.0);

    CHECK(animator.play("loop"));
    animator.tick(0.25F);
    CHECK(animator.frame() == 10); // 2 frames of 0.1s: 0.25s wraps back to the first frame.
    animator.tick(0.1F);
    CHECK(animator.frame() == 11);
    CHECK(animator.play("loop") && animator.frame() == 11);    // Re-playing keeps its place.
    CHECK(animator.restart("loop") && animator.frame() == 10); // restart() does not.
    CHECK(animator.play("once") && animator.frame() == 2);     // Switching restarts.
    animator.tick(-1.0F);
    animator.tick(0.0F);
    CHECK(animator.frame() == 2);
}

void richClips() {
    Animator animator;
    // Explicit cells and per-frame timing: cells 5, 9, 5 lasting 0.1, 0.3, 0.1.
    AnimationClip timed;
    timed.name = "timed";
    timed.frames = {5, 9, 5};
    timed.durations = {0.1F, 0.3F, 0.1F};
    timed.loop = false;
    CHECK(timed.length() == 3 && timed.cell(1) == 9);
    CHECK_NEAR(timed.totalSeconds(), 0.5);
    CHECK(animator.define(timed));
    CHECK(animator.play("timed") && animator.frame() == 5);
    animator.tick(0.05F);
    CHECK(animator.frame() == 5);
    CHECK_NEAR(animator.normalizedTime(), 0.1);
    animator.tick(0.06F);
    CHECK(animator.frame() == 9); // The second frame is the long one.
    animator.tick(0.25F);
    CHECK(animator.frame() == 9);
    animator.tick(0.05F);
    CHECK(animator.frame() == 5);
    animator.tick(0.2F);
    CHECK(animator.takeCompletion());

    // Invalid rich clips.
    AnimationClip bad = timed;
    bad.durations = {0.1F, 0.3F}; // One per frame.
    CHECK(!animator.define(bad));
    bad = timed;
    bad.durations = {0.1F, -0.3F, 0.1F};
    CHECK(!animator.define(bad));
    bad = timed;
    bad.frames = {5, -1, 5};
    CHECK(!animator.define(bad));
    bad = timed;
    bad.events = {{7, "late", ""}}; // Frame outside the clip.
    CHECK(!animator.define(bad));
    bad = timed;
    bad.next = "timed"; // A clip cannot follow itself.
    CHECK(!animator.define(bad));

    // Follow-up clips: a non-looping clip continues with `next`, and still reports completion.
    AnimationClip land = simpleClip("land", 20, 2, 0.1F, false);
    land.next = "idle";
    CHECK(animator.define(land));
    CHECK(animator.define(simpleClip("idle", 0, 2, 0.5F, true)));
    CHECK(animator.play("land"));
    animator.tick(0.25F);
    CHECK(animator.current() == "idle" && animator.frame() == 0);
    CHECK(animator.takeCompletion() && !animator.takeCompletion());

    // Events fire when their frame is entered (including frame 0 when the clip starts).
    AnimationClip step = simpleClip("step", 30, 4, 0.1F, true);
    step.events = {{0, "plant", ""}, {2, "footstep", "tone:200,0.05"}};
    CHECK(animator.define(step));
    CHECK(animator.play("step"));
    auto events = animator.takeEvents();
    CHECK(events.size() == 1 && events[0].name == "plant");
    animator.tick(0.25F); // Enters frames 1 and 2.
    events = animator.takeEvents();
    CHECK(events.size() == 1 && events[0].name == "footstep" && events[0].sound == "tone:200,0.05");
    CHECK(animator.takeEvents().empty());
    animator.tick(0.2F); // Frame 3 then back around to frame 0: "plant" again.
    events = animator.takeEvents();
    CHECK(events.size() == 1 && events[0].name == "plant");
}

Result<AnimationSet> parseSet(const char *text) {
    auto json = Json::parse(text);
    return json ? parseAnimationSet(json.value()) : Result<AnimationSet>(Error{json.error()});
}

void animationAssets() {
    const auto v1 = parseSet(R"({"format":"yk.animation","version":1,"columns":4,"rows":1,
        "clips":[{"name":"run","first":1,"count":3,"fps":10}]})");
    CHECK(v1 && v1.value().columns == 4 && v1.value().clips.size() == 1 &&
          v1.value().texture.empty());
    CHECK(v1 && v1.value().clips[0].loop && v1.value().clips[0].length() == 3);

    const auto v2 = parseSet(R"({"format":"yk.animation","version":2,"texture":"assets/hero.png",
        "columns":8,"rows":4,"clips":[
        {"name":"idle","frames":[0,1,2,1],"fps":6},
        {"name":"land","first":16,"count":3,"durations":[0.05,0.05,0.1],"loop":false,"next":"idle"},
        {"name":"step","first":24,"count":4,"fps":10,
         "events":[{"frame":1,"name":"footstep","sound":"tone:200,0.05"}]}]})");
    CHECK(v2);
    if (v2) {
        const AnimationSet &set = v2.value();
        CHECK(set.texture == "assets/hero.png" && set.columns == 8 && set.rows == 4);
        CHECK(set.clips.size() == 3);
        CHECK(set.find("idle")->frames.size() == 4 && set.find("idle")->cell(3) == 1);
        CHECK(set.find("land")->next == "idle" && !set.find("land")->loop);
        CHECK_NEAR(set.find("land")->totalSeconds(), 0.2);
        CHECK(set.find("step")->events.size() == 1 && set.find("step")->events[0].frame == 1);
        CHECK(set.find("nothing") == nullptr);
    }

    // Every mistake is reported with the clip it is in.
    const auto reason = [](const char *clips, int columns = 4) {
        const std::string text = std::string(R"({"format":"yk.animation","version":2,"columns":)") +
                                 std::to_string(columns) + R"(,"rows":1,"clips":)" + clips + "}";
        const auto parsed = parseSet(text.c_str());
        return parsed ? std::string{} : parsed.error();
    };
    CHECK(reason(R"([{"name":"a","first":2,"count":5,"fps":10}])").find("'a'") !=
          std::string::npos);
    CHECK(!reason(R"([{"name":"a","first":2,"count":5,"fps":10}])").empty()); // Past the sheet.
    CHECK(!reason(R"([{"name":"a","frames":[0,9]}])").empty());
    CHECK(!reason(R"([{"name":"a","fps":0}])").empty());
    CHECK(!reason(R"([{"name":"a","frames":[]}])").empty());
    CHECK(!reason(R"([{"name":"a","frames":[0.5]}])").empty());
    CHECK(!reason(R"([{"name":"a","durations":"fast"}])").empty());
    CHECK(!reason(R"([{"name":"a","frames":[0,1],"durations":[0.1]}])").empty());
    CHECK(!reason(R"([{"name":"a","events":[{"frame":0}]}])").empty());
    CHECK(!reason(R"([{"name":"a"},{"name":"a"}])").empty());            // Twice.
    CHECK(!reason(R"([{"name":"a","loop":false,"next":"b"}])").empty()); // Unknown follow-up.
    CHECK(!reason(R"([{"first":0}])").empty());                          // No name.
    CHECK(!reason(R"([])").empty());                                     // No clips.
    CHECK(!parseSet(R"({"format":"yk.animation","version":3,"clips":[{"name":"a"}]})"));
    CHECK(!parseSet(R"({"format":"other","version":1})"));
    CHECK(!parseSet(R"({"format":"yk.animation","version":2,"columns":0,"clips":[{"name":"a"}]})"));
}

// A locomotion graph like the demo game's: what a platformer controller publishes decides the clip.
constexpr const char *locomotionAsset = R"({"format":"yk.animation","version":2,
    "columns":8,"rows":1,"clips":[
    {"name":"idle","first":0,"count":2,"fps":4},
    {"name":"run","first":2,"count":2,"fps":10},
    {"name":"jump","first":4,"count":1,"fps":10,"loop":false},
    {"name":"fall","first":5,"count":1,"fps":10},
    {"name":"land","first":6,"count":1,"fps":20,"loop":false},
    {"name":"death","first":7,"count":1,"fps":10,"loop":false}]})";
constexpr const char *locomotionController = R"({"format":"yk.animator","version":1,
    "parameters":[
      {"name":"speed","type":"float"},{"name":"speedRatio","type":"float"},
      {"name":"velocityY","type":"float"},{"name":"grounded","type":"bool","default":true},
      {"name":"jumped","type":"trigger"},{"name":"landed","type":"trigger"},
      {"name":"dead","type":"bool"}],
    "entry":"Idle",
    "states":[
      {"name":"Idle","clip":"idle"},
      {"name":"Run","clip":"run","speedParameter":"speedRatio"},
      {"name":"Jump","clip":"jump"},
      {"name":"Fall","clip":"fall"},
      {"name":"Land","clip":"land"},
      {"name":"Death","clip":"death"}],
    "transitions":[
      {"from":"*","to":"Death","when":[{"parameter":"dead"}]},
      {"from":"*","to":"Jump","when":[{"parameter":"jumped"},{"parameter":"dead","value":false}]},
      {"from":"Jump","to":"Fall","when":[{"parameter":"velocityY","op":">","value":0.5}]},
      {"from":"Run","to":"Fall","when":[{"parameter":"grounded","value":false}]},
      {"from":"Idle","to":"Fall","when":[{"parameter":"grounded","value":false}]},
      {"from":"Fall","to":"Land","when":[{"parameter":"landed"}]},
      {"from":"Fall","to":"Idle","when":[{"parameter":"grounded"}]},
      {"from":"Land","to":"Idle","exitTime":1.0},
      {"from":"Idle","to":"Run","when":[{"parameter":"speed","op":">","value":0.2}]},
      {"from":"Run","to":"Idle","when":[{"parameter":"speed","op":"<=","value":0.2}]}]})";

std::shared_ptr<const AnimationSet> shared(const char *text) {
    auto set = parseSet(text);
    CHECK(set);
    return set ? std::make_shared<const AnimationSet>(std::move(set.value())) : nullptr;
}
std::shared_ptr<const AnimationController> sharedController(const char *text) {
    const auto json = Json::parse(text);
    CHECK(json);
    auto controller = json ? AnimationController::fromJson(json.value())
                           : Result<AnimationController>(Error{json.error()});
    if (!controller)
        std::fprintf(stderr, "controller: %s\n", controller.error().c_str());
    CHECK(controller);
    return controller ? std::make_shared<const AnimationController>(std::move(controller.value()))
                      : nullptr;
}

Result<AnimationController> parseController(const char *text) {
    const auto json = Json::parse(text);
    return json ? AnimationController::fromJson(json.value())
                : Result<AnimationController>(Error{json.error()});
}

void controllerDefinitions() {
    const auto controller = parseController(locomotionController);
    CHECK(controller);
    if (controller) {
        const AnimationController &c = controller.value();
        CHECK(c.states.size() == 6 && c.transitions.size() == 10 && c.parameters.size() == 7);
        CHECK(c.entry == "Idle" && c.state("Run")->speedParameter == "speedRatio");
        CHECK(c.parameter("grounded")->initial == 1.0 &&
              c.parameter("jumped")->type == ParameterType::Trigger);
        // "is true" conditions have no operator; "value":false compares to zero.
        CHECK(c.transitions[0].conditions[0].comparison == Comparison::Equal &&
              c.transitions[0].conditions[0].value == 1.0);
        CHECK(c.transitions[3].conditions[0].value == 0.0);
        CHECK(c.transitions[7].hasExitTime && c.transitions[7].exitTime == 1.0F);
        CHECK(c.validateAgainst(*shared(locomotionAsset)));
        // Saved text parses back to the same controller (and to the same text).
        const std::string saved = c.toJson().dump(2);
        const auto again = parseController(saved.c_str());
        CHECK(again && again.value().toJson().dump(2) == saved);
        // A controller whose states use a clip the animation lacks is rejected against it.
        AnimationController missing = c;
        missing.states[0].clip = "sleep";
        CHECK(!missing.validateAgainst(*shared(locomotionAsset)));
    }

    const auto reason = [](const char *body) {
        const std::string text =
            std::string(R"({"format":"yk.animator","version":1,)") + body + "}";
        const auto parsed = parseController(text.c_str());
        return parsed ? std::string{} : parsed.error();
    };
    CHECK(!reason(R"("states":[])").empty()); // No states.
    CHECK(!reason(R"("states":[{"name":"A","clip":"a"},{"name":"A","clip":"b"}])")
               .empty());                                            // Duplicate.
    CHECK(!reason(R"("states":[{"name":"*","clip":"a"}])").empty()); // Reserved.
    CHECK(!reason(R"("states":[{"name":"A"}])").empty());            // No clip.
    CHECK(!reason(R"("entry":"Nope","states":[{"name":"A","clip":"a"}])").empty());
    CHECK(
        !reason(R"("parameters":[{"name":"p","type":"vector"}],"states":[{"name":"A","clip":"a"}])")
             .empty());
    CHECK(!reason(R"("parameters":[{"name":"p"},{"name":"p"}],"states":[{"name":"A","clip":"a"}])")
               .empty());
    CHECK(
        !reason(
             R"("states":[{"name":"A","clip":"a"}],"transitions":[{"from":"A","to":"Z","exitTime":1}])")
             .empty());
    CHECK(
        !reason(
             R"("states":[{"name":"A","clip":"a"}],"transitions":[{"from":"Z","to":"A","exitTime":1}])")
             .empty());
    CHECK(!reason(R"("states":[{"name":"A","clip":"a"}],"transitions":[{"from":"A","to":"A"}])")
               .empty()); // Fires at once.
    CHECK(
        !reason(
             R"("states":[{"name":"A","clip":"a"}],"transitions":[{"from":"A","to":"A","exitTime":2}])")
             .empty());
    CHECK(
        !reason(
             R"("states":[{"name":"A","clip":"a"}],"transitions":[{"from":"A","to":"A","when":[{"parameter":"ghost"}]}])")
             .empty());
    CHECK(
        !reason(
             R"("parameters":[{"name":"p"}],"states":[{"name":"A","clip":"a"}],"transitions":[{"from":"A","to":"A","when":[{"parameter":"p","op":"~","value":1}]}])")
             .empty());
    CHECK(!reason(R"("states":[{"name":"A","clip":"a","speedParameter":"ghost"}])").empty());
    CHECK(
        !reason(
             R"("parameters":[{"name":"flag","type":"bool"}],"states":[{"name":"A","clip":"a","speedParameter":"flag"}])")
             .empty());
    CHECK(!parseController(R"({"format":"yk.animator","version":2})"));
}

// Advances the player in 1/60 s ticks, the way a fixed-step game does.
void run(AnimationPlayer &player, int ticks) {
    for (int i = 0; i < ticks; ++i)
        player.update(1.0F / 60.0F);
}

void stateMachine() {
    AnimationPlayer player;
    CHECK(!player.ready());
    player.update(1.0F); // Harmless before start.
    CHECK(!player.start(nullptr));

    // Parameters may be published before the player starts; declared defaults do not overwrite
    // them.
    player.setFloat("speed", 3.0);
    CHECK(player.start(shared(locomotionAsset), sharedController(locomotionController)));
    CHECK(player.ready() && player.state() == "Idle" && player.clip() == "idle");
    CHECK_NEAR(player.value("speed"), 3.0);
    CHECK(player.value("grounded") == 1.0); // The declared default.
    player.setFloat("speed", 0.0);

    // Standing: idle plays and loops.
    run(player, 30);
    CHECK(player.state() == "Idle" && (player.frame() == 0 || player.frame() == 1));

    // Start running: speed above the threshold switches to Run at once, on its first frame.
    player.setFloat("speed", 5.0);
    player.setFloat("speedRatio", 1.0);
    player.update(1.0F / 60.0F);
    CHECK(player.state() == "Run" && player.clip() == "run" && player.frame() == 2);
    // The run clip is 10 fps; at speedRatio 1 it takes 0.1 s to reach its second frame, at 0.5
    // twice as long.
    run(player, 5);
    CHECK(player.frame() == 2);
    run(player, 3);
    CHECK(player.frame() == 3);
    player.setFloat("speedRatio", 0.5);
    for (int i = 0; i < 12; ++i)
        player.update(1.0F / 60.0F);
    CHECK(player.frame() == 2); // 12 ticks at half rate = 0.1 s of clip time: one more frame flip.

    // Stop: back to Idle.
    player.setFloat("speed", 0.0);
    player.update(1.0F / 60.0F);
    CHECK(player.state() == "Idle");

    // A jump trigger works from any state and holds for exactly one update.
    player.trigger("jumped");
    CHECK(player.value("jumped") == 1.0);
    player.setBool("grounded", false);
    player.setFloat("velocityY", -6.0);
    player.update(1.0F / 60.0F);
    CHECK(player.state() == "Jump" && player.frame() == 4 && player.value("jumped") == 0.0);
    run(player, 10);
    CHECK(player.state() == "Jump"); // Still rising: no Fall yet.
    player.setFloat("velocityY", 2.0);
    player.update(1.0F / 60.0F);
    CHECK(player.state() == "Fall" && player.frame() == 5);

    // Landing hard raises the trigger while grounded flips: Fall -> Land (trigger consumed), which
    // returns to Idle by itself when its clip has finished.
    player.setBool("grounded", true);
    player.trigger("landed");
    player.update(1.0F / 60.0F);
    CHECK(player.state() == "Land" && player.frame() == 6 && player.value("landed") == 0.0);
    run(player, 2);
    CHECK(player.state() == "Land"); // The 20 fps clip has not finished yet (0.05 s).
    run(player, 3);
    CHECK(player.state() == "Idle");

    // A soft landing has no trigger: Fall goes straight to Idle when grounded.
    player.setBool("grounded", false);
    player.update(1.0F / 60.0F);
    CHECK(player.state() == "Fall");
    player.setBool("grounded", true);
    player.update(1.0F / 60.0F);
    CHECK(player.state() == "Idle");

    // An unused trigger does not linger: set while in Idle it is gone after the update.
    player.trigger("landed");
    player.update(1.0F / 60.0F);
    CHECK(player.value("landed") == 0.0 && player.state() == "Idle");
    player.setBool("grounded", false);
    run(player, 2);
    CHECK(player.state() == "Fall"); // The stale "landed" did not fire Land.

    // Death wins over everything (any-state transitions are checked first) and holds its frame.
    player.setBool("dead", true);
    player.update(1.0F / 60.0F);
    CHECK(player.state() == "Death" && player.frame() == 7);
    // Any-state transitions can leave Death too, so the graph guards them (Jump requires dead ==
    // false); the trigger is then unused and dropped, and the character stays dead.
    player.trigger("jumped");
    player.update(1.0F / 60.0F);
    CHECK(player.state() == "Death" && player.value("jumped") == 0.0);
    // Unknown parameters are harmless.
    player.setFloat("mystery", 1.0);
    player.trigger("nothing");
    player.update(1.0F / 60.0F);
}

void directClipPlayback() {
    // Without a controller the player plays clips by name.
    AnimationPlayer player;
    CHECK(player.start(shared(locomotionAsset)));
    CHECK(player.state() == "idle" && player.frame() == 0);
    player.playClip("run");
    CHECK(player.state() == "run" && player.frame() == 2);
    player.playClip("missing");
    CHECK(player.state() == "run");
    run(player, 6);
    CHECK(player.frame() == 3);

    // A controller that needs a clip the set lacks refuses to start.
    AnimationController broken = *sharedController(locomotionController);
    broken.states[2].clip = "hover";
    AnimationPlayer other;
    CHECK(
        !other.start(shared(locomotionAsset), std::make_shared<const AnimationController>(broken)));
    CHECK(!other.ready());

    // Frozen (speed 0 state) and exit-time-with-loop behaviours.
    const char *frozenController = R"({"format":"yk.animator","version":1,
        "parameters":[{"name":"go","type":"trigger"}],
        "states":[{"name":"A","clip":"run","speed":0},{"name":"B","clip":"idle"},{"name":"C","clip":"run"}],
        "transitions":[{"from":"A","to":"B","when":[{"parameter":"go"}]},
                       {"from":"B","to":"C","exitTime":1.0}]})";
    AnimationPlayer frozen;
    CHECK(frozen.start(shared(locomotionAsset), sharedController(frozenController)));
    run(frozen, 60);
    CHECK(frozen.frame() == 2); // Speed 0: never advances.
    frozen.trigger("go");
    frozen.update(1.0F / 60.0F);
    CHECK(frozen.state() == "B");
    run(frozen, 20);
    CHECK(frozen.state() == "B"); // idle: 2 frames at 4 fps = 0.5 s = 30 ticks for one full cycle.
    run(frozen, 12);
    CHECK(frozen.state() == "C"); // A looping clip's exit time waits for one whole cycle.
}
} // namespace

int main() {
    clipPlayback();
    richClips();
    animationAssets();
    controllerDefinitions();
    stateMachine();
    directClipPlayback();
    return yk::test::finish("animation");
}
