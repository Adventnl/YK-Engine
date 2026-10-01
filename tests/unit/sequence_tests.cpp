// Cutscene sequences: the file (cues in time order, what is refused and what is only warned about),
// a SequencePlayer playing one in a running game (fades, camera moves, walks, waits that hold the
// clock, conversations, skipping, stopping), the rule actions around it, and project validation.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Project.hpp"
#include "yk/assets/Validation.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/Definitions.hpp"
#include "yk/data/GameData.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include "yk/sim/Sequence.hpp"
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace yk;

namespace {
Json J(const char *text) {
    auto parsed = Json::parse(text);
    CHECK(parsed);
    return parsed ? parsed.value() : Json();
}
bool has(const std::string &text, const char *part) {
    return text.find(part) != std::string::npos;
}

struct Rig {
    ComponentRegistry registry;
    MemoryAssets assets;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<GameRuntime> runtime;
    EntityId director, hero, guard, walker, console;
    Keyboard keyboard;
    std::vector<GameEvent> heard;

    explicit Rig(const char *sequence, bool playOnStart = false, const char *rulesText = nullptr,
                 bool withCamera = true) {
        registerEngineComponents(registry);
        assets.files["cut/scene.ykseq"] = sequence;
        scene = std::make_unique<Scene>(registry, 3);
        Entity &heroEntity = scene->createEntity("Hero");
        heroEntity.setWorldPosition({2.0F, 1.0F});
        hero = heroEntity.id();
        if (withCamera) {
            auto &camera = scene->createEntity("Camera").add<Camera>();
            camera.mode = CameraMode::Follow;
            camera.smoothTime = 0.0F; // Where it looks is where it is told to look, each tick.
            camera.targets = {hero};
        }
        Entity &guardEntity = scene->createEntity("Guard");
        auto &talk = guardEntity.add<Dialogue>();
        talk.pages = {"Halt.", "Move along."};
        talk.charactersPerSecond = 0.0F;
        guard = guardEntity.id();
        Entity &body = scene->createEntity("Walker");
        body.setWorldPosition({1.0F, 5.0F});
        body.add<RigidBody>().gravityScale = 0.0F;
        body.add<Collider>();
        body.add<AnimatedSprite>();
        walker = body.id();
        Entity &conductor = scene->createEntity("Director");
        auto &player = conductor.add<SequencePlayer>();
        player.sequence.path = "cut/scene.ykseq";
        player.skipSet = "Global";
        player.playOnStart = playOnStart;
        director = conductor.id();
        if (rulesText) {
            Entity &panel = scene->createEntity("Console");
            CHECK(panel.add<RuleSet>().setRulesJson(J(rulesText)));
            console = panel.id();
        }
    }
    void start() {
        RuntimeOptions options;
        options.assets = &assets;
        auto created = GameRuntime::create(std::move(scene), options);
        CHECK(created);
        if (!created)
            return;
        runtime = std::move(created.value());
        runtime->events().subscribe("*",
                                    [this](const GameEvent &event) { heard.push_back(event); });
        step();
    }
    void step(int ticks = 1) {
        for (int i = 0; i < ticks; ++i) {
            InputFrame frame;
            frame.keyboard = keyboard;
            runtime->stepOnce(frame);
            keyboard.beginFrame();
        }
    }
    void press(Key key) {
        keyboard.set(key, true);
        step();
        keyboard.set(key, false);
        step();
    }
    bool play(EntityId actor = {}) {
        return player().play(*runtime, actor);
    }
    SequencePlayer &player() {
        return *runtime->scene().find(director)->get<SequencePlayer>();
    }
    Entity &entity(EntityId id) {
        return *runtime->scene().find(id);
    }
    Vec2 where(EntityId id) {
        return entity(id).worldPosition();
    }
    Camera &camera() {
        return *runtime->scene().findByName("Camera")->get<Camera>();
    }
    bool flag(const char *name) {
        return runtime->blackboard().flag(name);
    }
    int count(const char *name) const {
        return static_cast<int>(
            std::count_if(heard.begin(), heard.end(),
                          [&](const GameEvent &event) { return event.name == name; }));
    }
    const GameEvent *last(const char *name) const {
        for (auto it = heard.rbegin(); it != heard.rend(); ++it)
            if (it->name == name)
                return &*it;
        return nullptr;
    }
};

// ---- The file
// --------------------------------------------------------------------------------------
const char *fullText = R"({
  "lockInput": false, "skippable": false,
  "cues": [
    {"time": 3, "type": "SetVariable", "name": "late", "value": true},
    {"time": 0, "type": "Fade", "to": 1, "seconds": 0},
    {"time": 0, "type": "CameraMove", "to": [12, 5], "seconds": 2, "height": 9, "ease": "smooth", "wait": true},
    {"type": "MoveEntity", "entity": "name:Guard", "to": [14, 5], "speed": 2},
    {"time": 1, "type": "MoveEntity", "entity": "name:Guard", "toEntity": "name:Door", "seconds": 1},
    {"time": 1, "type": "Wait", "seconds": 0.5},
    {"time": 2, "type": "WaitForEvent", "event": "door_opened", "source": "name:Hero",
     "data": {"locked": false}, "timeout": 4},
    {"time": 2, "type": "CameraRelease"},
    {"time": 2.5, "type": "StartDialogue", "entity": "name:Guard", "wait": true},
    {"time": 2.5, "type": "TriggerAnimation", "entity": "name:Guard", "trigger": "wave"}
  ]})";

void parsing() {
    std::vector<std::string> warnings;
    auto full = SequenceDefinition::fromJson(J(fullText), warnings);
    CHECK(full && warnings.empty());
    if (!full)
        return;
    const SequenceDefinition &sequence = full.value();
    using Kind = SequenceCue::Kind;
    CHECK(!sequence.lockInput && !sequence.skippable && sequence.cues.size() == 10);
    // In order of time; cues at the same time keep their order in the file.
    const std::vector<std::string> order = {
        "Fade",         "CameraMove",    "MoveEntity",    "MoveEntity",       "Wait",
        "WaitForEvent", "CameraRelease", "StartDialogue", "TriggerAnimation", "SetVariable"};
    for (std::size_t i = 0; i < order.size(); ++i)
        CHECK(sequence.cues[i].type == order[i]);
    CHECK(sequence.cues[2].time == 0.0 && sequence.cues[2].args.get("speed").asNumber() == 2.0 &&
          sequence.cues[3].args.get("toEntity").asString() == "name:Door");
    CHECK(sequence.length() == 3.0 && sequence.cues.front().number == 2 &&
          sequence.cues.back().number == 1);
    CHECK(sequence.cues[0].kind == Kind::Fade && sequence.cues[1].kind == Kind::CameraMove &&
          sequence.cues[1].wait && !sequence.cues[0].wait);
    CHECK(sequence.cues[4].kind == Kind::Wait && sequence.cues[4].wait); // Always holds.
    CHECK(sequence.cues[5].kind == Kind::WaitForEvent && sequence.cues[5].wait &&
          sequence.cues[5].trigger.pattern == "door_opened" &&
          sequence.cues[5].trigger.source == "name:Hero" &&
          sequence.cues[5].trigger.dataFilter.get("locked").asBool(true) == false);
    CHECK(sequence.cues[7].kind == Kind::Action && sequence.cues[7].wait &&
          sequence.cues[7].action.size() == 1 &&
          sequence.cues[7].action[0].type == "StartDialogue");
    CHECK(sequence.cues[6].kind == Kind::CameraRelease);
    // Writing it back and reading again gives the same sequence.
    auto again = SequenceDefinition::fromJson(sequence.toJson(), warnings);
    CHECK(again && again.value().toJson() == sequence.toJson() && warnings.empty());
    // Defaults.
    auto bare = SequenceDefinition::fromJson(
        J(R"({"cues":[{"type":"PlaySound","sound":"a.wav"}]})"), warnings);
    CHECK(bare && bare.value().lockInput && bare.value().skippable &&
          bare.value().cues[0].time == 0.0 && !bare.value().cues[0].wait);
    CHECK(sequenceCueTypes().size() == 6 && sequenceEaseNames().size() == 4);

    const auto error = [&](const char *text) {
        auto parsed = SequenceDefinition::fromJson(J(text), warnings);
        return parsed ? std::string() : parsed.error();
    };
    CHECK(has(error("[]"), "object with a list of cues"));
    CHECK(has(error("{}"), "needs a 'cues' list"));
    CHECK(has(error(R"({"cues":3})"), "needs a 'cues' list"));
    CHECK(has(error(R"({"cues":[3]})"), "cue 1 must be an object"));
    CHECK(has(error(R"({"cues":[{}]})"), "cue 1 needs a 'type'"));
    CHECK(has(error(R"({"cues":[{"type":"Fade"}]})"), "cue 1 ('Fade'): needs 'to'"));
    CHECK(has(error(R"({"cues":[{"type":"Fade","to":"dark"}]})"), "needs 'to'"));
    CHECK(has(error(R"({"cues":[{"type":"Fade","to":1,"seconds":"x"}]})"),
              "'seconds' must be a number"));
    CHECK(has(error(R"({"cues":[{"type":"Fade","to":1,"ease":"bouncy"}]})"),
              "'ease' must be one of"));
    CHECK(has(error(R"({"cues":[{"type":"Wait"}]})"), "needs 'seconds'"));
    CHECK(has(error(R"({"cues":[{"type":"WaitForEvent"}]})"), "needs the event it waits for"));
    CHECK(has(error(R"({"cues":[{"type":"WaitForEvent","event":"x","data":3}]})"),
              "must be an object"));
    CHECK(has(error(R"({"cues":[{"type":"CameraMove"}]})"), "needs 'to' ([x, y]) or an 'entity'"));
    CHECK(has(error(R"({"cues":[{"type":"CameraMove","to":[1]}]})"), "needs 'to' ([x, y])"));
    CHECK(
        has(error(R"({"cues":[{"type":"MoveEntity","to":[1,2],"seconds":1}]})"), "needs 'entity'"));
    CHECK(has(error(R"({"cues":[{"type":"MoveEntity","entity":"self","seconds":1}]})"),
              "needs 'to' ([x, y]) or 'toEntity'"));
    CHECK(has(error(R"({"cues":[{"type":"MoveEntity","entity":"self","to":[1,2]}]})"),
              "needs 'seconds' or 'speed'"));
    CHECK(has(error(R"({"cues":[{"type":"MoveEntity","entity":"self","to":[1,2],"speed":0}]})"),
              "'speed' must be above 0"));
    CHECK(has(error(R"({"cues":[{"time":"soon","type":"Fade","to":1}]})"),
              "'time' must be a number"));
    CHECK(
        has(error(R"({"cues":[{"type":"PlaySound","wait":3}]})"), "'wait' must be true or false"));
    CHECK(has(error(R"({"cues":[{"type":"Fade","to":1},{"type":"Wait"}]})"), "cue 2 ('Wait')"));

    // What is only odd is warned about and still works.
    warnings.clear();
    auto odd =
        SequenceDefinition::fromJson(J(R"({"cues":[{"time":-1,"type":"Fade","to":2,"seconds":-3},
                      {"type":"SetVariable","name":"a","value":1,"wait":true},
                      {"type":"CameraRelease","wait":true}]})"),
                                     warnings);
    CHECK(odd && odd.value().cues[0].time == 0.0);
    CHECK(warnings.size() == 5 && has(warnings[0], "'time' is below 0; it counts as 0") &&
          has(warnings[1], "'to' is outside 0..1") && has(warnings[2], "'seconds' is below 0") &&
          has(warnings[3], "cue 2 ('SetVariable'): 'wait' has no effect here") &&
          has(warnings[4], "cue 3 ('CameraRelease'): 'wait' has no effect here"));
    warnings.clear();
    CHECK(SequenceDefinition::fromJson(J(R"({"cues":[]})"), warnings));
    CHECK(warnings.size() == 1 && has(warnings[0], "has no cues"));

    // The rule actions are handed to the validator, with where they are in the file.
    std::vector<std::string> places;
    sequence.visitRules("cut.ykseq", [&](const RuleSource &source) {
        CHECK(source.file == "cut.ykseq" && source.actions && source.actions->size() == 1 &&
              !source.condition);
        places.push_back(source.label);
    });
    CHECK(places.size() == 3 && has(places[0], "cue 9 'StartDialogue' at 2.5 s") &&
          has(places[1], "cue 10 'TriggerAnimation' at 2.5 s") &&
          has(places[2], "cue 1 'SetVariable' at 3 s"));
}

// ---- Fades and the clock
// ---------------------------------------------------------------------------
void fades() {
    Rig rig(R"({"cues":[{"time":0,"type":"Fade","to":1,"seconds":0},
                        {"time":0.25,"type":"Fade","to":0.25,"seconds":0.5,"ease":"linear"}]})");
    rig.start();
    CHECK(rig.runtime->screenFade() == 0.0F && !rig.runtime->inputLocked() &&
          !rig.player().playing());
    CHECK(rig.play(rig.hero) && rig.player().playing());
    CHECK(rig.runtime->screenFade() == 1.0F && rig.runtime->inputLocked()); // Time zero: at once.
    CHECK(!rig.play());                                                     // One at a time.
    rig.step(10);
    CHECK(rig.runtime->screenFade() == 1.0F);
    CHECK_NEAR(rig.player().clock(), 10.0 / 60.0, 0.001);
    rig.step(20); // Thirty ticks: the second fade began at 15 and is halfway.
    CHECK_NEAR(rig.runtime->screenFade(), 0.625, 0.01);
    rig.step(14);
    CHECK(rig.player().playing() && rig.runtime->screenFade() > 0.25F);
    rig.step(1);
    CHECK(!rig.player().playing() && rig.runtime->screenFade() == 0.25F &&
          !rig.runtime->inputLocked()); // It ends where the last fade left the screen.
    CHECK(rig.count("sequence.started") == 1 && rig.count("sequence.finished") == 1);
    const GameEvent *finished = rig.last("sequence.finished");
    CHECK(finished && finished->source == rig.director && finished->other == rig.hero &&
          finished->data.get("sequence").asString() == "cut/scene.ykseq" &&
          !finished->data.get("skipped").asBool(true) && !finished->data.contains("stopped"));
    CHECK(rig.last("sequence.started")->other == rig.hero);
    // It can be played again.
    CHECK(rig.play() && rig.runtime->screenFade() == 1.0F);

    // The shapes of a fade: halfway through, linear is 0.5, "in" 0.25, "out" 0.75.
    for (const auto &[ease, expected] : std::vector<std::pair<std::string, double>>{
             {"linear", 0.5}, {"in", 0.25}, {"out", 0.75}, {"smooth", 0.5}}) {
        const std::string text =
            R"({"cues":[{"type":"Fade","to":1,"seconds":1,"ease":")" + ease + R"("}]})";
        Rig shaped(text.c_str());
        shaped.start();
        CHECK(shaped.play());
        shaped.step(30);
        CHECK_NEAR(shaped.runtime->screenFade(), expected, 0.01);
    }
    // A fade does not hide the one a scene change makes, and the other way round: the screen is as
    // dark as the darker of the two.
    Rig dark(R"({"cues":[{"type":"Fade","to":0.5,"seconds":0}]})");
    dark.start();
    dark.play();
    CHECK(dark.runtime->cinematicFade() == 0.5F && dark.runtime->screenFade() == 0.5F);
    dark.runtime->setCinematicFade(7.0F);
    CHECK(dark.runtime->cinematicFade() == 1.0F);
    dark.runtime->setCinematicFade(-1.0F);
    CHECK(dark.runtime->cinematicFade() == 0.0F);
}

void waiting() {
    // A wait holds what comes after it, and the clock with it.
    Rig rig(R"({"cues":[{"time":0,"type":"SetVariable","name":"before","value":true},
                        {"time":0,"type":"Wait","seconds":0.5},
                        {"time":0,"type":"SetVariable","name":"after","value":true},
                        {"time":0.5,"type":"SetVariable","name":"later","value":true}]})");
    rig.start();
    CHECK(rig.play());
    CHECK(rig.flag("before") && !rig.flag("after"));
    rig.step(29);
    CHECK(!rig.flag("after") && rig.player().clock() == 0.0);
    rig.step(1);
    CHECK(rig.flag("after") && !rig.flag("later"));
    rig.step(20);
    CHECK(!rig.flag("later"));
    rig.step(15);
    CHECK(rig.flag("later") && !rig.player().playing());

    // Waiting for an event: only the right one, only after the wait began.
    Rig events(R"({"cues":[{"type":"WaitForEvent","event":"door_opened","source":"name:Hero",
                            "data":{"locked":false}},
                           {"type":"SetVariable","name":"through","value":true}]})");
    events.start();
    GameContext &game = *events.runtime;
    game.events().emit(GameEvent("door_opened", events.hero, {}, J(R"({"locked":false})")));
    events.step();
    CHECK(events.play());
    events.step(5);
    CHECK(!events.flag("through") && events.player().playing());
    game.events().emit(GameEvent("door_closed", events.hero, {}, J(R"({"locked":false})")));
    game.events().emit(GameEvent("door_opened", events.guard, {}, J(R"({"locked":false})")));
    game.events().emit(GameEvent("door_opened", events.hero, {}, J(R"({"locked":true})")));
    game.events().emit(GameEvent("door_opened", events.hero)); // No payload at all.
    events.step(5);
    CHECK(!events.flag("through"));
    game.events().emit(GameEvent("door_opened", events.hero, {}, J(R"({"locked":false})")));
    events.step(2);
    CHECK(events.flag("through") && !events.player().playing());

    // A timeout ends the wait on its own.
    Rig timeout(R"({"cues":[{"type":"WaitForEvent","event":"never","timeout":0.5},
                            {"type":"SetVariable","name":"gave_up","value":true}]})");
    timeout.start();
    CHECK(timeout.play());
    timeout.step(29);
    CHECK(!timeout.flag("gave_up"));
    timeout.step(1);
    CHECK(timeout.flag("gave_up") && !timeout.player().playing());
}

// ---- Walks
// -------------------------------------------------------------------------------------------
void moves() {
    {
        // A wait: what comes after a walk happens after it, and the clock stands still meanwhile.
        Rig rig(R"({"cues":[{"time":0,"type":"MoveEntity","entity":"name:Guard","to":[6,0],
                             "seconds":1,"wait":true},
                            {"time":0,"type":"SetVariable","name":"early","value":true},
                            {"time":0.5,"type":"SetVariable","name":"late","value":true}]})");
        rig.start();
        CHECK(rig.play());
        CHECK(!rig.flag("early"));
        rig.step(30);
        CHECK_NEAR(rig.where(rig.guard).x, 3.0, 0.01);
        CHECK_NEAR(rig.where(rig.guard).y, 0.0, 0.001);
        CHECK(!rig.flag("early") && rig.player().clock() == 0.0);
        // Drawn between the last two ticks, not jumped: the walk interpolates like any movement.
        CHECK_NEAR(rig.entity(rig.guard).renderPosition(0.5F).x, rig.where(rig.guard).x - 0.05,
                   0.01);
        rig.step(29);
        CHECK(!rig.flag("early"));
        rig.step(1);
        CHECK(rig.where(rig.guard) == (Vec2{6.0F, 0.0F}) && rig.flag("early") && !rig.flag("late"));
        rig.step(20);
        CHECK(!rig.flag("late") && rig.player().playing());
        rig.step(15);
        CHECK(rig.flag("late") && !rig.player().playing());
    }
    {
        // Without it the walk goes on alongside.
        Rig rig(R"({"cues":[{"type":"MoveEntity","entity":"name:Guard","to":[6,0],"seconds":1},
                            {"time":0.5,"type":"SetVariable","name":"late","value":true}]})");
        rig.start();
        CHECK(rig.play());
        rig.step(25);
        CHECK(!rig.flag("late"));
        rig.step(10);
        CHECK(rig.flag("late") && rig.player().playing());
        rig.step(25);
        CHECK(!rig.player().playing() && rig.where(rig.guard) == (Vec2{6.0F, 0.0F}));
    }
    {
        // By speed, then to another entity with an ease.
        Rig rig(
            R"({"cues":[{"type":"MoveEntity","entity":"name:Guard","to":[3,4],"speed":5,"wait":true},
                            {"type":"MoveEntity","entity":"name:Guard","toEntity":"name:Hero",
                             "seconds":0.5,"ease":"in"}]})");
        rig.start();
        CHECK(rig.play());
        rig.step(59);
        CHECK(rig.where(rig.guard).x < 3.0F && rig.where(rig.guard).y < 4.0F);
        rig.step(1);
        CHECK(rig.where(rig.guard) == (Vec2{3.0F, 4.0F}) && rig.player().playing());
        rig.step(15); // Halfway in time, a quarter of the way ("in" starts slowly).
        CHECK_NEAR(rig.where(rig.guard).x, 2.75, 0.01);
        CHECK_NEAR(rig.where(rig.guard).y, 3.25, 0.01);
        rig.step(15);
        CHECK(rig.where(rig.guard) == rig.where(rig.hero) && !rig.player().playing());
    }
    {
        // An entity with a body is moved as a body: its pose follows and it keeps no velocity.
        Rig rig(
            R"({"cues":[{"type":"MoveEntity","entity":"name:Walker","to":[5,5],"seconds":1}]})");
        rig.start();
        CHECK(rig.play());
        rig.step(30);
        const auto body = rig.runtime->bodyOf(rig.walker);
        CHECK(body.has_value());
        if (body) {
            const auto state = rig.runtime->physics().state(*body);
            CHECK(state);
            if (state) {
                CHECK_NEAR(state.value().pose.position.x, 3.0, 0.02);
                CHECK_NEAR(state.value().pose.position.y, 5.0, 0.02);
                CHECK_NEAR(state.value().linearVelocity.x, 0.0, 0.001);
            }
        }
        CHECK_NEAR(rig.where(rig.walker).x, 3.0, 0.02);
        rig.step(30);
        CHECK_NEAR(rig.where(rig.walker).x, 5.0, 0.001);
        CHECK(!rig.player().playing());
    }
    {
        // Cues that name what is not there are logged and passed over, not waited for.
        Rig rig(
            R"({"cues":[{"type":"MoveEntity","entity":"name:Nobody","to":[1,1],"seconds":1,"wait":true},
                            {"type":"MoveEntity","entity":"name:Guard","toEntity":"name:Nobody","seconds":1,"wait":true},
                            {"type":"CameraMove","entity":"name:Nobody","seconds":1,"wait":true},
                            {"type":"SetVariable","name":"carried_on","value":true}]})");
        rig.start();
        CHECK(rig.play() && rig.flag("carried_on") && !rig.player().playing() &&
              !rig.runtime->inputLocked());
        // Walking an entity that is destroyed on the way ends the walk.
        Rig gone(
            R"({"cues":[{"type":"MoveEntity","entity":"name:Guard","to":[6,0],"seconds":1,"wait":true},
                             {"type":"SetVariable","name":"after","value":true}]})");
        gone.start();
        CHECK(gone.play());
        gone.step(10);
        gone.runtime->destroyLater(gone.guard);
        gone.step(2);
        CHECK(gone.flag("after") && !gone.player().playing());
    }
}

// ---- The camera
// -----------------------------------------------------------------------------------
void cameraWork() {
    {
        // A cut, then handing the camera back.
        Rig rig(R"({"cues":[{"time":0,"type":"CameraMove","to":[10,4],"seconds":0},
                            {"time":0.5,"type":"CameraRelease"},
                            {"time":1,"type":"SetVariable","name":"end","value":true}]})");
        rig.start();
        CHECK(!rig.camera().held());
        CHECK_NEAR(rig.camera().view().position.x, 2.0, 0.001); // Following the hero.
        CHECK(rig.play());
        rig.step();
        CHECK(rig.camera().held() && rig.camera().view().position == (Vec2{10.0F, 4.0F}));
        rig.step(28);
        CHECK(rig.camera().held() && rig.camera().view().position == (Vec2{10.0F, 4.0F}));
        rig.step(1); // 0.5 s: released, back to the hero.
        CHECK(!rig.camera().held() && rig.camera().view().position == (Vec2{2.0F, 1.0F}));
        rig.step(40);
        CHECK(!rig.player().playing() && !rig.camera().held());
    }
    {
        // A move over time; the camera stays where it arrives until the sequence is over.
        Rig rig(R"({"cues":[{"time":0,"type":"CameraMove","to":[10,4],"seconds":1,"ease":"linear"},
                            {"time":1.5,"type":"SetVariable","name":"end","value":true}]})");
        rig.start();
        CHECK(rig.play());
        rig.step(30);
        CHECK_NEAR(rig.camera().view().position.x, 6.0, 0.05);
        CHECK_NEAR(rig.camera().view().position.y, 2.5, 0.05);
        rig.step(29);
        CHECK_NEAR(rig.camera().view().position.x, 2.0 + 8.0 * 59.0 / 60.0, 0.05);
        rig.step(11);
        CHECK(rig.camera().held() && rig.camera().view().position == (Vec2{10.0F, 4.0F}));
        rig.step(40);
        CHECK(!rig.player().playing() && !rig.camera().held() &&
              rig.camera().view().position == (Vec2{2.0F, 1.0F}));
    }
    {
        // Going to an entity, and staying with it as it walks off.
        Rig rig(R"({"cues":[{"time":0,"type":"CameraMove","entity":"name:Guard","seconds":0.5},
                            {"time":0.5,"type":"MoveEntity","entity":"name:Guard","to":[20,0],"seconds":1},
                            {"time":2,"type":"SetVariable","name":"end","value":true}]})");
        rig.start();
        CHECK(rig.play());
        rig.step(30);
        CHECK_NEAR(rig.camera().view().position.x, 0.0, 0.01);
        CHECK_NEAR(rig.camera().view().position.y, 0.0, 0.01);
        rig.step(30);
        CHECK(rig.where(rig.guard).x > 5.0F);
        CHECK_NEAR(rig.camera().view().position.x, rig.where(rig.guard).x, 0.01);
        rig.step(30);
        CHECK_NEAR(rig.camera().view().position.x, 20.0, 0.01);
        rig.step(40);
        CHECK(!rig.player().playing() && !rig.camera().held());
    }
    {
        // How much of the world is seen.
        Rig rig(R"({"cues":[{"time":0,"type":"CameraMove","to":[0,0],"height":9},
                            {"time":1,"type":"SetVariable","name":"end","value":true}]})");
        rig.start();
        CHECK_NEAR(rig.camera().view().visibleHeight, 18.0, 0.001);
        CHECK(rig.play());
        rig.step();
        CHECK_NEAR(rig.camera().view().visibleHeight, 9.0, 0.001);
        rig.step(70);
        CHECK(!rig.player().playing());
        CHECK_NEAR(rig.camera().view().visibleHeight, 18.0, 0.001);
    }
    {
        // Without a camera the cue is passed over and the rest goes on.
        Rig rig(R"({"cues":[{"type":"CameraMove","to":[1,1],"seconds":1,"wait":true},
                            {"type":"CameraRelease"},
                            {"type":"SetVariable","name":"end","value":true}]})",
                false, nullptr, false);
        rig.start();
        CHECK(rig.play() && rig.flag("end") && !rig.player().playing());
    }
}

// ---- Conversations, skipping and stopping
// -------------------------------------------------------------------
void conversation() {
    Rig rig(R"({"cues":[{"type":"StartDialogue","entity":"name:Guard","wait":true},
                        {"type":"SetVariable","name":"after_talk","value":true}]})");
    rig.start();
    CHECK(rig.play(rig.hero));
    Dialogue &talk = *rig.entity(rig.guard).get<Dialogue>();
    CHECK(talk.active() && !rig.flag("after_talk") && rig.runtime->inputLocked());
    rig.step(30);
    CHECK(talk.active() && !rig.flag("after_talk")); // It waits as long as the conversation does.
    rig.press(Key::E);
    CHECK(talk.active() && !rig.flag("after_talk")); // The second page.
    rig.press(Key::E);
    CHECK(!talk.active() && rig.flag("after_talk") && !rig.player().playing() &&
          !rig.runtime->inputLocked());
    CHECK(rig.count("conversation_started") == 1 &&
          rig.last("conversation_started")->other == rig.hero); // The sequence's actor talks.

    // Without "wait" the conversation is only started.
    Rig brisk(R"({"cues":[{"type":"StartDialogue","entity":"name:Guard"},
                          {"type":"SetVariable","name":"after_talk","value":true}]})");
    brisk.start();
    CHECK(brisk.play() && brisk.flag("after_talk") && !brisk.player().playing() &&
          brisk.entity(brisk.guard).get<Dialogue>()->active());
}

void skipping() {
    const char *text = R"({"cues":[{"time":0,"type":"Fade","to":1,"seconds":0},
        {"time":1,"type":"MoveEntity","entity":"name:Guard","to":[6,0],"seconds":2,"wait":true},
        {"time":1,"type":"StartDialogue","entity":"name:Guard","wait":true},
        {"time":2,"type":"SetVariable","name":"deep","value":5},
        {"time":2,"type":"CameraMove","to":[9,9],"seconds":1},
        {"time":3,"type":"Fade","to":0,"seconds":1},
        {"time":3,"type":"EmitEvent","name":"cutscene_end"}]})";
    {
        Rig rig(text);
        rig.start();
        rig.player().skipAction = "Continue";
        CHECK(rig.play(rig.hero));
        rig.step(10);
        CHECK(rig.runtime->screenFade() == 1.0F && !rig.flag("deep") && rig.player().playing());
        rig.press(Key::A); // Not the skip key.
        CHECK(rig.player().playing());
        rig.press(Key::Enter);
        CHECK(!rig.player().playing() && rig.runtime->blackboard().number("deep") == 5.0);
        CHECK(rig.where(rig.guard) == (Vec2{6.0F, 0.0F}));       // The walk, finished.
        CHECK(!rig.entity(rig.guard).get<Dialogue>()->active()); // Never started.
        CHECK(rig.runtime->screenFade() == 0.0F && !rig.runtime->inputLocked()); // The last fade.
        CHECK(!rig.camera().held());
        CHECK(rig.count("cutscene_end") == 1 && rig.count("sequence.finished") == 1 &&
              rig.last("sequence.finished")->data.get("skipped").asBool(false) &&
              rig.count("conversation_started") == 0);
    }
    {
        // Skipping while a conversation is open closes it.
        Rig rig(R"({"cues":[{"type":"StartDialogue","entity":"name:Guard","wait":true},
                            {"type":"SetVariable","name":"after_talk","value":true}]})");
        rig.start();
        rig.player().skipAction = "Continue";
        CHECK(rig.play());
        Dialogue &talk = *rig.entity(rig.guard).get<Dialogue>();
        rig.step(5);
        CHECK(talk.active());
        rig.press(Key::Enter);
        CHECK(!talk.active() && !rig.player().playing() && rig.flag("after_talk") &&
              !rig.runtime->inputLocked());
        CHECK(rig.count("conversation_finished") == 1);
    }
    {
        // A sequence can say that it cannot be skipped; so can a player that names no action.
        Rig rig(R"({"skippable":false,"cues":[{"type":"Wait","seconds":1}]})");
        rig.start();
        rig.player().skipAction = "Continue";
        CHECK(rig.play());
        rig.press(Key::Enter);
        CHECK(rig.player().playing());
        Rig quiet(R"({"cues":[{"type":"Wait","seconds":1}]})");
        quiet.start();
        CHECK(quiet.play());
        quiet.press(Key::Enter);
        CHECK(quiet.player().playing());
        quiet.player().skip(*quiet.runtime); // The API still can.
        CHECK(!quiet.player().playing());
        quiet.player().skip(*quiet.runtime); // And skipping nothing is nothing.
        quiet.step();
        CHECK(quiet.count("sequence.finished") == 1);
    }
    {
        // A cue that asks for the sequence to be skipped or stopped while it is being skipped does
        // not make a mess of it.
        Rig rig(R"({"cues":[{"time":1,"type":"SkipSequence"},{"time":2,"type":"StopSequence"},
                            {"time":3,"type":"SetVariable","name":"end","value":true}]})");
        rig.start();
        CHECK(rig.play());
        rig.player().skip(*rig.runtime);
        CHECK(!rig.player().playing() && rig.flag("end") && !rig.runtime->inputLocked());
        rig.step();
        CHECK(rig.count("sequence.finished") == 1);
    }
}

void stopping() {
    Rig rig(R"({"cues":[{"time":0,"type":"Fade","to":1,"seconds":1},
                        {"time":0,"type":"CameraMove","to":[9,9],"seconds":0},
                        {"time":0,"type":"StartDialogue","entity":"name:Guard","wait":true},
                        {"time":0,"type":"SetVariable","name":"never","value":true}]})");
    rig.start();
    rig.runtime->setCinematicFade(0.25F);
    CHECK(rig.play(rig.hero));
    rig.step(30);
    Dialogue &talk = *rig.entity(rig.guard).get<Dialogue>();
    CHECK(talk.active() && rig.camera().held() && rig.runtime->screenFade() > 0.5F &&
          rig.runtime->inputLocked());
    rig.player().stop(*rig.runtime);
    CHECK(!rig.player().playing() && rig.runtime->screenFade() == 0.25F && !talk.active() &&
          !rig.camera().held() && !rig.runtime->inputLocked() && !rig.flag("never"));
    rig.step();
    const GameEvent *finished = rig.last("sequence.finished");
    CHECK(finished && finished->data.get("stopped").asBool(false) &&
          !finished->data.get("skipped").asBool(true));
    rig.player().stop(*rig.runtime); // Stopping what is not playing is nothing.
    rig.step();
    CHECK(rig.count("sequence.finished") == 1);
    // And it can play again from the start.
    CHECK(rig.play() && rig.player().clock() < 0.001);
}

// ---- Rules, events and lifecycle
// ----------------------------------------------------------------------
void rulesAndLifecycle() {
    {
        Rig rig(R"({"cues":[{"type":"SetVariable","name":"who","value":"$actor.entity.name"},
                            {"type":"Wait","seconds":1},
                            {"type":"SetVariable","name":"done","value":true}]})",
                false, R"([
          {"id":"go","when":"go","then":{"type":"PlaySequence","entity":"name:Director"}},
          {"id":"other","when":"other","then":{"type":"PlaySequence","entity":"name:Director","sequence":"cut/other.ykseq"}},
          {"id":"stop","when":"stop","then":{"type":"StopSequence","entity":"name:Director"}},
          {"id":"skip","when":"skip","then":{"type":"SkipSequence","entity":"name:Director"}},
          {"id":"ask","when":"ask","if":{"type":"SequencePlaying","entity":"name:Director"},
           "then":{"type":"SetVariable","name":"asked","value":true},
           "else":{"type":"SetVariable","name":"asked","value":false}},
          {"id":"nobody","when":"nobody","then":{"type":"PlaySequence","entity":"name:Nobody"}}])");
        rig.assets.files["cut/other.ykseq"] =
            R"({"cues":[{"type":"SetVariable","name":"other_ran","value":true}]})";
        rig.start();
        const auto fire = [&](const char *name) {
            rig.runtime->events().emit(GameEvent(name, rig.console, rig.hero));
            rig.step(2);
        };
        fire("ask");
        CHECK(rig.runtime->blackboard().has("asked") && !rig.flag("asked"));
        fire("go");
        CHECK(rig.player().playing() && rig.runtime->blackboard().text("who") == "Hero");
        CHECK(rig.last("sequence.started")->other == rig.hero);
        fire("ask");
        CHECK(rig.flag("asked"));
        fire("go"); // Already playing: the action fails and the sequence goes on.
        CHECK(rig.player().playing() && rig.count("sequence.started") == 1);
        fire("skip");
        CHECK(!rig.player().playing() && rig.flag("done") &&
              rig.last("sequence.finished")->data.get("skipped").asBool(false));
        fire("stop"); // Nothing playing.
        fire("nobody");
        CHECK(rig.count("sequence.finished") == 1);
        rig.runtime->blackboard().setBool("done", false);
        fire("go");
        fire("stop");
        CHECK(!rig.player().playing() && !rig.flag("done") && rig.count("sequence.finished") == 2 &&
              rig.last("sequence.finished")->data.get("stopped").asBool(false));
        fire("other"); // Another file on the same player.
        CHECK(rig.flag("other_ran") && rig.player().sequence.path == "cut/other.ykseq" &&
              !rig.player().playing());
    }
    {
        // Animation actions.
        Rig rig(R"({"cues":[]})");
        rig.start();
        RuleContext rc(*rig.runtime);
        rc.self = rig.walker;
        const auto act = [&](const char *text) {
            auto action = Action::fromJson(J(text));
            CHECK(action);
            return action ? execute(action.value(), rc) : ActionResult::Failed;
        };
        CHECK(act(R"({"type":"TriggerAnimation","trigger":"wave"})") == ActionResult::Done);
        CHECK(act(R"({"type":"TriggerAnimation","entity":"name:Guard","trigger":"wave"})") ==
              ActionResult::Failed);
        CHECK(act(R"({"type":"PlayAnimation","clip":"idle"})") == ActionResult::Done);
        CHECK(act(R"({"type":"PlayAnimation","entity":"name:Guard","clip":"idle"})") ==
              ActionResult::Failed);
        CHECK(rig.registry.extension<RuleCatalog>()->action("PlaySequence") &&
              rig.registry.extension<RuleCatalog>()->predicate("SequencePlaying"));
    }
    {
        // An intro that plays when the scene starts.
        Rig rig(R"({"cues":[{"type":"SetVariable","name":"intro_seen","value":true}]})", true);
        rig.start();
        CHECK(rig.flag("intro_seen") && !rig.player().playing() &&
              rig.count("sequence.started") == 1 && rig.count("sequence.finished") == 1);
    }
    {
        // A file that cannot be played does not lock anything.
        Rig rig(R"({"cues":[{"type":"Fade"}]})");
        rig.start();
        CHECK(!rig.play() && !rig.player().playing() && !rig.runtime->inputLocked());
        rig.assets.files["cut/scene.ykseq"] = "{not json";
        CHECK(!rig.play());
        rig.player().sequence.path = "cut/missing.ykseq";
        CHECK(!rig.play());
        rig.player().sequence.path.clear();
        CHECK(!rig.play() && !rig.runtime->inputLocked());
        rig.step();
        CHECK(rig.count("sequence.started") == 0);
    }
    {
        // Removing the entity in the middle gives everything back.
        Rig rig(R"({"cues":[{"time":0,"type":"CameraMove","to":[9,9],"seconds":0},
                            {"time":0,"type":"Wait","seconds":5}]})");
        rig.start();
        CHECK(rig.play() && rig.runtime->inputLocked() && rig.camera().held());
        rig.runtime->destroyLater(rig.director);
        rig.step();
        CHECK(!rig.runtime->inputLocked() && !rig.camera().held() &&
              rig.count("sequence.finished") == 1 &&
              rig.last("sequence.finished")->data.get("stopped").asBool(false));
    }
    {
        // A sequence that does not lock input leaves the game playable.
        Rig rig(R"({"lockInput":false,"cues":[{"type":"Wait","seconds":1}]})");
        rig.start();
        CHECK(rig.play() && !rig.runtime->inputLocked());
    }
    {
        // A scene that starts again starts the sequence over, and nothing is left behind.
        Rig rig(R"({"cues":[{"type":"Fade","to":1,"seconds":0},{"type":"Wait","seconds":5}]})");
        rig.start();
        CHECK(rig.play() && rig.runtime->screenFade() == 1.0F && rig.runtime->inputLocked());
        CHECK(rig.runtime->restart());
        rig.step();
        CHECK(rig.runtime->screenFade() == 0.0F && !rig.runtime->inputLocked() &&
              !rig.player().playing());
    }
}

// ---- Checks against the project
// -----------------------------------------------------------------------
void validation() {
    Rig rig(R"({"cues":[]})");
    GameData data;
    std::vector<DataProblem> problems;
    data.add(J(R"({"items":[{"id":"badge"}]})"), "d.ykdata", problems);
    std::vector<std::string> warnings;
    auto sequence = SequenceDefinition::fromJson(J(R"({"cues":[
        {"time":0,"type":"GiveItem","item":"unicorn"},
        {"time":1,"type":"Explode"},
        {"time":2,"type":"GiveItem","item":"badge"},
        {"time":3,"type":"Fade","to":0}]})"),
                                                 warnings);
    CHECK(sequence);
    std::vector<DataProblem> found;
    sequence.value().visitRules("intro.ykseq", [&](const RuleSource &source) {
        data.checkRules(*rig.registry.extension<RuleCatalog>(), source, found);
    });
    CHECK(found.size() == 2);
    const auto problem = [&](const char *place, const char *part) {
        return std::any_of(found.begin(), found.end(), [&](const DataProblem &item) {
            return item.file == "intro.ykseq" && has(item.message, place) &&
                   has(item.message, part);
        });
    };
    CHECK(problem("cue 1 'GiveItem' at 0 s", "there is no item 'unicorn'"));
    CHECK(problem("cue 2 'Explode' at 1 s", "unknown action 'Explode'"));

    // The scene's own check: a player that plays on start with nothing to play.
    Rig empty(R"({"cues":[]})");
    empty.start();
    auto &player = *empty.runtime->scene().find(empty.director)->get<SequencePlayer>();
    player.sequence.path.clear();
    player.playOnStart = true;
    const ComponentType *type = empty.registry.find("SequencePlayer");
    CHECK(type && type->check);
    if (type && type->check) {
        std::vector<std::string> messages;
        type->check(empty.entity(empty.director), player, CheckContext{}, messages);
        CHECK(messages.size() == 1 && has(messages[0], "plays on start but names no sequence"));
    }
}

void project() {
    using Severity = ProjectIssue::Severity;
    const std::filesystem::path root = std::filesystem::current_path() / "sequence-test-project";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "cut");
    std::filesystem::create_directories(root / "scenes");
    Project project = Project::create(root, "Sequences");
    project.startScene = "scenes/main.ykscene";
    const auto write = [&](const char *path, const char *text) {
        CHECK(writeTextFileAtomic(root / path, text));
    };
    write("cut/good.ykseq", R"({"cues":[{"type":"Fade","to":1},
                                        {"time":2,"type":"SetVariable","name":"a","value":1},
                                        {"time":3,"type":"CameraRelease"}]})");
    write("cut/odd.ykseq", R"({"cues":[{"time":-2,"type":"Fade","to":0}]})");
    write("cut/broken.ykseq", R"({"cues":[{"type":"Fade"}]})");
    write("cut/garbage.ykseq", "{not json");
    write("cut/explosive.ykseq", R"({"cues":[{"type":"Explode"}]})");

    ComponentRegistry registry;
    registerEngineComponents(registry);
    Scene scene(registry, 9);
    scene.createEntity("Idle").add<SequencePlayer>().playOnStart = true;
    scene.createEntity("Lost").add<SequencePlayer>().sequence.path = "cut/gone.ykseq";
    scene.createEntity("Fine").add<SequencePlayer>().sequence.path = "cut/good.ykseq";
    CHECK(saveScene(scene, root / "scenes/main.ykscene"));
    setLogStderrEnabled(false);
    const auto issues = validateProject(project, registry);
    const auto about = [&](Severity severity, const char *path, const char *part) {
        return std::any_of(issues.begin(), issues.end(), [&](const ProjectIssue &issue) {
            return issue.severity == severity && issue.path == path && has(issue.message, part);
        });
    };
    CHECK(about(Severity::Error, "cut/broken.ykseq", "cue 1 ('Fade'): needs 'to'"));
    CHECK(about(Severity::Error, "cut/explosive.ykseq", "unknown action 'Explode'"));
    CHECK(about(Severity::Warning, "cut/odd.ykseq", "'time' is below 0"));
    CHECK(std::count_if(issues.begin(), issues.end(), [](const ProjectIssue &issue) {
              return issue.path == "cut/garbage.ykseq";
          }) == 1); // A file that is not JSON is reported once.
    CHECK(std::none_of(issues.begin(), issues.end(),
                       [](const ProjectIssue &issue) { return issue.path == "cut/good.ykseq"; }));
    CHECK(std::any_of(issues.begin(), issues.end(), [](const ProjectIssue &issue) {
        return has(issue.message, "plays on start but names no sequence");
    }));
    CHECK(std::any_of(issues.begin(), issues.end(), [](const ProjectIssue &issue) {
        return has(issue.message, "cut/gone.ykseq");
    }));

    // What the Inspector shows.
    const auto good = describeDefinition(project, "cut/good.ykseq");
    CHECK(good.error.empty() && !good.rows.empty());
    const auto row = [&](const DefinitionSummary &summary, const char *name) {
        for (const auto &[label, value] : summary.rows)
            if (label == name)
                return value;
        return std::string();
    };
    CHECK(row(good, "Cues") == "3 (1 rule actions)");
    CHECK(row(good, "Locks input") == "yes" && row(good, "Skippable") == "yes");
    CHECK(has(row(good, "Length").c_str(), "3.0"));
    CHECK(has(describeDefinition(project, "cut/broken.ykseq").error, "needs 'to'"));
    CHECK(row(describeDefinition(project, "cut/odd.ykseq"), "Warnings") == "1");
    setLogStderrEnabled(true);
    std::filesystem::remove_all(root);
}
} // namespace

int main() {
    setLogStderrEnabled(false);
    parsing();
    fades();
    waiting();
    moves();
    cameraWork();
    conversation();
    skipping();
    stopping();
    rulesAndLifecycle();
    validation();
    project();
    return yk::test::finish("sequence");
}
