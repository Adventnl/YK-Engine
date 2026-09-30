// The rules of a level and how a game moves on: intro, completion sequence, failure and restart,
// the fade between scenes, variables carried to the next scene, and reactions to events.
#include "support/check.hpp"
#include "support/world.hpp"
#include "yk/core/Log.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/runtime/GameSession.hpp"
#include <algorithm>
#include <cmath>

using namespace yk;
using namespace yk::test;

namespace {
// A sensor box that is a Goal for characters tagged `tag`.
Entity &goal(World &w, const char *name, float x, const char *tag) {
    Entity &entity = w.box(name, {x, floorTop - 0.9F}, {1.2F, 1.8F}, layers::sensor);
    entity.get<Collider>()->isTrigger = true;
    entity.add<SpriteRenderer>().size = {1.2F, 1.8F};
    entity.add<Goal>().requiredTag = tag;
    return entity;
}

// Two characters (tags "a" and "b", Player1 and Player2) and their exits; returns the LevelFlow.
LevelFlow &twoCharacterLevel(World &w, float aAt = -4.0F, float bAt = 4.0F) {
    w.ground();
    w.character("A", {aAt, restingHeight}, "Player1", "a");
    w.character("B", {bAt, restingHeight}, "Player2", "b");
    Entity &exitA = goal(w, "Exit A", -4.0F, "a");
    Entity &exitB = goal(w, "Exit B", 10.0F, "b");
    Entity &flow = w.scene->createEntity("Flow");
    auto &rules = flow.add<LevelFlow>();
    rules.goals = {exitA.id(), exitB.id()};
    return rules;
}

std::string state(World &w) {
    return w.runtime->blackboard().text("level_state");
}

// The whole sequence of finishing a level, as a player sees it: the first character waits in its
// exit, the second walks into its own, the level is complete, nobody can be steered any more,
// both walk into their exits and vanish, and only then does the game move on.
void completionSequence() {
    World w;
    LevelFlow &rules = twoCharacterLevel(w);
    rules.completeDelay = 1.0F;
    rules.nextScene = "scenes/next.ykscene";
    w.start();
    w.tick(30);
    CHECK(state(w) == "playing" && w.happened("level_started") && !w.happened("level_completed"));
    CHECK(w.runtime->blackboard().text("level_message").empty());
    CHECK(w.at("Exit A").get<Goal>()->satisfied() && !w.at("Exit B").get<Goal>()->satisfied());
    CHECK(!w.runtime->inputLocked());
    w.down(Key::Right);
    for (int i = 0; i < 200 && state(w) == "playing"; ++i)
        w.tick();
    w.up(Key::Right);
    CHECK(state(w) == "complete" && w.count("level_completed") == 1);
    CHECK(w.runtime->blackboard().text("level_message") == "LEVEL COMPLETE!");
    CHECK(w.runtime->inputLocked()); // The game is not steerable now.
    CHECK(w.at("Flow").get<LevelFlow>()->completed());
    // B is on its way to the middle of its exit; holding keys changes nothing about that.
    const float bStart = w.position("B").x;
    w.down(Key::Left); // B's "move left" is ignored while locked.
    w.down(Key::A);
    w.tick(10);
    CHECK(w.position("B").x >= bStart - 0.05F);
    w.tick(50);
    w.up(Key::Left);
    w.up(Key::A);
    CHECK_NEAR(w.position("B").x, 10.0F, 0.06); // Inside the exit, at its middle.
    CHECK_NEAR(w.position("A").x, -4.0F, 0.06);
    CHECK(w.at("A").get<SpriteRenderer>()->color.a == 0); // Both have gone through.
    CHECK(w.at("B").get<SpriteRenderer>()->color.a == 0);
    CHECK(!w.at("A").get<PlayerInput>()->enabled);
    // The game moves on after the delay, not before.
    CHECK(w.runtime->sceneChangeRequested().empty());
    w.tick(60);
    CHECK(w.runtime->sceneChangeRequested() == "scenes/next.ykscene");
}

// A level that stays where it is when completed (no next scene), and one that lets the player
// continue early.
void completionWithoutNextSceneAndContinue() {
    {
        World w;
        LevelFlow &rules = twoCharacterLevel(w, -4.0F, 10.0F);
        rules.completeDelay = 0.5F;
        w.start();
        w.tick(240);
        CHECK(state(w) == "complete");
        CHECK(w.runtime->sceneChangeRequested().empty()); // Nowhere to go: it stays.
    }
    {
        World w;
        LevelFlow &rules = twoCharacterLevel(w, -4.0F, 10.0F);
        rules.completeDelay = 100.0F;
        rules.nextScene = "scenes/next.ykscene";
        rules.continueAction = "Continue";
        w.start();
        w.tick(60);
        CHECK(state(w) == "complete" && w.runtime->sceneChangeRequested().empty());
        w.down(Key::Enter); // Not a player's action: it works while their input is locked.
        w.tick(3);
        w.up(Key::Enter);
        CHECK(w.runtime->sceneChangeRequested() == "scenes/next.ykscene");
    }
}

// An intro that ignores input, a time limit that fails the level, and the restart that follows.
void introTimeLimitAndFailure() {
    World w;
    LevelFlow &rules = twoCharacterLevel(w);
    rules.introDuration = 1.0F;
    rules.introMessage = "GET READY";
    rules.timeLimit = 3.0F;
    rules.restartDelay = 1.0F;
    w.start();
    w.tick(2);
    CHECK(state(w) == "intro" && w.runtime->blackboard().text("level_message") == "GET READY");
    CHECK(w.runtime->inputLocked() && !w.happened("level_started"));
    const float start = w.position("B").x;
    w.down(Key::Left);
    w.tick(30);
    CHECK_NEAR(w.position("B").x, start, 0.02); // Half a second in: still standing.
    w.tick(40);
    CHECK(state(w) == "playing" && w.happened("level_started") && !w.runtime->inputLocked());
    CHECK(w.runtime->blackboard().text("level_message").empty());
    CHECK(w.position("B").x < start - 0.5F); // Now the held key moves it.
    w.up(Key::Left);
    // The clock runs (whole seconds) and counts down.
    CHECK(w.runtime->blackboard().number("level_time_left") <= 3.0);
    for (int i = 0; i < 400 && state(w) == "playing"; ++i)
        w.tick();
    CHECK(state(w) == "failed" && w.happened("level_failed"));
    CHECK(w.runtime->blackboard().text("level_message") == "TIME'S UP!");
    CHECK(w.runtime->blackboard().number("level_time_left") == 0.0);
    CHECK(w.runtime->inputLocked());
    // A second later the level starts over, with its intro again.
    const auto failedAt = w.runtime->tick();
    w.tick(62);
    CHECK(w.runtime->tick() < failedAt && state(w) == "intro");
    CHECK(w.runtime->blackboard().text("level_message") == "GET READY");
}

void deathFailsTheLevel() {
    World w;
    LevelFlow &rules = twoCharacterLevel(w);
    rules.restartOnDeath = true;
    rules.restartDelay = 0.5F;
    w.start();
    w.tick(20);
    CHECK(state(w) == "playing");
    w.at("A").get<Killable>()->kill(*w.runtime, {});
    w.tick(2);
    CHECK(state(w) == "failed" && w.runtime->blackboard().text("level_message") == "TRY AGAIN");
    CHECK(w.at("Flow").get<LevelFlow>()->failed() && w.happened("level_failed"));
    CHECK(w.runtime->inputLocked());
    const auto before = w.runtime->tick();
    w.tick(40); // The restart comes half a second (30 ticks) after the failure.
    CHECK(w.runtime->tick() < before); // The level started over.
    CHECK(state(w) == "playing" && !w.runtime->inputLocked());
    CHECK(w.at("A").get<Killable>()->alive());
}

// The fade: covering the screen, doing the work while it is covered, uncovering it; and no input
// reaches the players while it goes on.
void transitionsFade() {
    World w;
    w.transitionSeconds = 0.5F;
    w.ground();
    w.character("A", {0.0F, restingHeight});
    w.start();
    w.tick(10);
    CHECK_NEAR(w.runtime->screenFade(), 0.0, 1e-6);
    CHECK(!w.runtime->transitioning());
    w.runtime->requestRestart();
    w.down(Key::D);
    float last = 0.0F;
    bool rising = true;
    const float startX = w.position("A").x;
    w.tick(20, [&](int) { // 1/3 s into a 1/2 s fade out
        const float now = w.runtime->screenFade();
        rising = rising && now >= last;
        last = now;
    });
    CHECK(rising && last > 0.5F && last < 1.0F && w.runtime->transitioning());
    CHECK(w.runtime->inputLocked());
    CHECK_NEAR(w.position("A").x, startX, 0.05); // The held key does nothing while it fades.
    // Fully covered, the restart happens; then the screen fades back in.
    bool restarted = false;
    float covered = 0.0F;
    for (int i = 0; i < 60; ++i) {
        w.tick();
        covered = std::max(covered, w.runtime->screenFade());
        restarted = restarted || w.runtime->tick() < 10;
    }
    CHECK(restarted && covered >= 0.999F);
    w.up(Key::D);
    CHECK_NEAR(w.runtime->screenFade(), 0.0, 1e-6);
    CHECK(!w.runtime->transitioning() && !w.runtime->inputLocked());
    // A second request while one is running is ignored (the screen never jumps).
    w.runtime->requestRestart();
    w.tick(5);
    const float partway = w.runtime->screenFade();
    w.runtime->requestSceneChange("scenes/x.ykscene");
    w.tick(2);
    CHECK(w.runtime->screenFade() >= partway);
    CHECK(w.runtime->sceneChangeRequested().empty()); // Not until the screen is covered.
    w.tick(60);
    // The restart won, and the second request was dropped.
    CHECK(w.runtime->sceneChangeRequested().empty());

    // A scene change is offered to the host only once the screen is covered.
    World s;
    s.transitionSeconds = 0.5F;
    s.ground();
    s.character("A", {0.0F, restingHeight});
    s.start();
    s.runtime->requestSceneChange("scenes/two.ykscene");
    s.tick(15);
    CHECK(s.runtime->sceneChangeRequested().empty() && s.runtime->screenFade() < 1.0F);
    s.tick(20);
    CHECK(s.runtime->sceneChangeRequested() == "scenes/two.ykscene");
    CHECK_NEAR(s.runtime->screenFade(), 1.0, 1e-6);
    s.runtime->clearSceneChangeRequest(); // The host declined (a scene that would not load).
    s.tick(40);
    CHECK_NEAR(s.runtime->screenFade(), 0.0, 1e-6); // Uncovered again, still playing.
    CHECK(s.runtime->sceneChangeRequested().empty());
}

// A scene that follows another starts covered and fades in.
void nextSceneStartsCovered() {
    World w;
    w.transitionSeconds = 0.4F;
    w.ground();
    w.character("A", {0.0F, restingHeight});
    RuntimeOptions options;
    options.layers = testLayers();
    options.transitionSeconds = 0.4F;
    options.startCovered = true;
    auto created = GameRuntime::create(std::move(w.scene), options);
    CHECK(created);
    if (!created)
        return;
    GameRuntime &runtime = *created.value();
    CHECK_NEAR(runtime.screenFade(), 1.0, 1e-6);
    runtime.stepOnce(Keyboard{});
    CHECK(runtime.screenFade() < 1.0F && runtime.transitioning());
    for (int i = 0; i < 40; ++i)
        runtime.stepOnce(Keyboard{});
    CHECK_NEAR(runtime.screenFade(), 0.0, 1e-6);
    CHECK(!runtime.transitioning());
}

// The session moves the game from scene to scene: loads the next one, carries over the variables
// the first one kept (and only those), fades it in, and survives a scene that cannot be loaded.
void sessionFollowsScenes() {
    setLogStderrEnabled(false);
    ComponentRegistry registry = makeRegistry();
    const auto firstScene = [&] {
        auto scene = std::make_unique<Scene>(registry, 21);
        Entity &floor = scene->createEntity("Ground");
        floor.transform().position = {0.0F, floorTop + 0.5F};
        auto &collider = floor.add<Collider>();
        collider.size = {80.0F, 1.0F};
        collider.layer = layers::solid;
        Entity &hero = scene->createEntity("Hero");
        hero.transform().position = {0.0F, restingHeight};
        hero.add<PlatformerController>();
        hero.add<Killable>();
        hero.addTag("hero");
        Entity &exit = scene->createEntity("Exit");
        exit.transform().position = {0.0F, floorTop - 0.9F};
        auto &zone = exit.add<Collider>();
        zone.size = {2.0F, 2.0F};
        zone.isTrigger = true;
        zone.layer = layers::sensor;
        exit.add<Goal>().requiredTag = "hero";
        Entity &flow = scene->createEntity("Flow");
        auto &rules = flow.add<LevelFlow>();
        rules.goals = {exit.id()};
        rules.completeDelay = 0.5F;
        rules.nextScene = "scenes/second.ykscene";
        rules.keepVariables = {"score"};
        return scene;
    };
    int loads = 0;
    std::string fail;
    const GameSession::SceneLoader loader = [&](const std::string &path) {
        ++loads;
        if (path == fail)
            return Result<std::unique_ptr<Scene>>(Error{"the scene file is damaged"});
        auto scene = std::make_unique<Scene>(registry, 22);
        Entity &marker = scene->createEntity("Second Scene");
        (void)marker;
        return Result<std::unique_ptr<Scene>>(std::move(scene));
    };
    RuntimeOptions options;
    options.layers = testLayers();
    options.transitionSeconds = 0.4F;
    auto created = GameSession::create(firstScene(), "scenes/first.ykscene", options, loader);
    CHECK(created);
    if (!created)
        return;
    GameSession &session = *created.value();
    session.runtime().blackboard().set("score", 120.0);
    session.runtime().blackboard().set("scratch", 7.0); // Not kept: does not travel.
    InputFrame none;
    bool changed = false;
    for (int i = 0; i < 400 && !changed; ++i)
        changed = session.update(1.0 / 60.0, none);
    CHECK(changed && session.scenePath() == "scenes/second.ykscene");
    CHECK(session.runtime().scene().findByName("Second Scene") != nullptr);
    CHECK(session.runtime().blackboard().number("score") == 120.0); // Carried over.
    CHECK(!session.runtime().blackboard().has("scratch"));          // Left behind.
    CHECK(session.runtime().screenFade() > 0.9F);                   // Starts covered...
    for (int i = 0; i < 60; ++i)
        session.update(1.0 / 60.0, none);
    CHECK_NEAR(session.runtime().screenFade(), 0.0, 1e-6); // ...and fades in.
    CHECK(loads == 1);
    // A restart of the second scene brings the carried variable back (it is part of the start).
    session.runtime().blackboard().set("score", 999.0);
    CHECK(session.restart());
    CHECK(session.runtime().blackboard().number("score") == 120.0);

    // A scene that cannot be loaded is reported and the game goes on where it was.
    fail = "scenes/broken.ykscene";
    session.runtime().requestSceneChange(fail);
    for (int i = 0; i < 80; ++i)
        session.update(1.0 / 60.0, none);
    CHECK(session.scenePath() == "scenes/second.ykscene");
    CHECK(session.runtime().sceneChangeRequested().empty());
    CHECK_NEAR(session.runtime().screenFade(), 0.0, 1e-6); // Not left black.
    setLogStderrEnabled(true);
}

// Pause, step and the viewport survive scene changes.
void sessionKeepsPauseAndViewport() {
    ComponentRegistry registry = makeRegistry();
    const GameSession::SceneLoader loader = [&](const std::string &) {
        return Result<std::unique_ptr<Scene>>(std::make_unique<Scene>(registry, 3));
    };
    RuntimeOptions options;
    options.layers = testLayers();
    auto created = GameSession::create(std::make_unique<Scene>(registry, 2), "a", options, loader);
    CHECK(created);
    if (!created)
        return;
    GameSession &session = *created.value();
    session.setViewportSize({640.0F, 360.0F});
    session.setPaused(true);
    const auto ticks = session.runtime().tick();
    InputFrame none;
    session.update(1.0, none);
    CHECK(session.runtime().tick() == ticks); // Paused.
    session.step(none);
    CHECK(session.runtime().tick() == ticks + 1);
    session.runtime().requestSceneChange("b"); // Instant: no transition in these options.
    CHECK(session.step(none) || session.scenePath() == "b");
    CHECK(session.scenePath() == "b" && session.paused());
    CHECK(session.runtime().viewportSize() == Vec2{640.0F, 360.0F});
    CHECK(session.runtime().paused());
}

// Reactions: a delay, a signal that a door follows, a chain of events, a timer, one-shots, and a
// filter on who raised the event.
void eventActionsReact() {
    {
        // scene_started + a delay is a timer that starts with the level; a door follows the signal.
        World w;
        w.ground();
        Entity &door =
            w.body("Door", {6.0F, floorTop - 1.5F}, {0.6F, 3.0F}, RigidBodyType::Kinematic);
        auto &gate = door.add<Door>();
        gate.openOffset = {0.0F, -3.2F};
        gate.speed = 8.0F;
        Entity &relay = w.scene->createEntity("Relay");
        auto &action = relay.add<EventAction>();
        action.onEvent = "scene_started";
        action.delay = 0.5F;
        action.signal = SignalChange::Set;
        action.targets = {door.id()};
        w.start();
        w.tick(20);
        CHECK(w.at("Door").get<Door>()->openAmount() < 1e-4F); // 1/3 s: not yet.
        CHECK(!w.at("Relay").get<EventAction>()->signalOn());
        w.tick(40);
        CHECK(w.at("Relay").get<EventAction>()->signalOn());
        CHECK_NEAR(w.at("Door").get<Door>()->openAmount(), 1.0, 1e-6);
        CHECK(w.at("Relay").get<EventAction>()->runCount() == 1);
    }
    {
        // A chain: a lever raises an event; one action re-raises it after a delay; another reacts
        // to that by switching an entity on and adding to the score.
        World w;
        w.ground();
        Entity &lever = w.box("Lever", {-3.0F, floorTop - 0.4F}, {0.5F, 0.8F}, layers::sensor);
        lever.get<Collider>()->isTrigger = true;
        lever.add<Lever>();
        Entity &bridge = w.scene->createEntity("Bridge");
        bridge.setActive(false);
        Entity &first = w.scene->createEntity("First");
        auto &a = first.add<EventAction>();
        a.onEvent = "lever_toggled";
        a.delay = 0.5F;
        a.raiseEvent = "bridge_go";
        Entity &second = w.scene->createEntity("Second");
        auto &b = second.add<EventAction>();
        b.onEvent = "bridge_go";
        b.delay = 1.0F;
        b.activate = {bridge.id()};
        b.variableChange = VariableChange::Add;
        b.variable = "score";
        b.amount = 50.0F;
        b.once = true;
        w.character("Hero", {-6.0F, restingHeight});
        w.start();
        w.tick(30);
        CHECK(!w.at("Bridge").active());
        w.runtime->teleport(w.at("Hero"), {-3.0F, restingHeight}); // Touches the lever.
        w.tick(45); // 0.75 s: the first action has run, the second is still waiting.
        CHECK(w.happened("lever_toggled") && w.happened("bridge_go") && !w.at("Bridge").active());
        w.tick(60); // The second action was due 1 s after the first one's event.
        CHECK(w.at("Bridge").active() && w.runtime->blackboard().number("score") == 50.0);
        // Toggling again does not run the one-shot a second time.
        w.runtime->teleport(w.at("Hero"), {-6.0F, restingHeight});
        w.tick(30);
        w.runtime->teleport(w.at("Hero"), {-3.0F, restingHeight});
        w.tick(200);
        CHECK(w.runtime->blackboard().number("score") == 50.0);
        CHECK(w.at("Second").get<EventAction>()->runCount() == 1);
    }
    {
        // A repeating timer, and a filter on the entity that raised the event.
        World w;
        w.ground();
        Entity &clock = w.scene->createEntity("Clock");
        auto &tick = clock.add<EventAction>();
        tick.every = 0.5F;
        tick.variableChange = VariableChange::Add;
        tick.variable = "beats";
        Entity &one = w.box("Lever One", {-3.0F, floorTop - 0.4F}, {0.5F, 0.8F}, layers::sensor);
        one.get<Collider>()->isTrigger = true;
        one.add<Lever>();
        Entity &two = w.box("Lever Two", {3.0F, floorTop - 0.4F}, {0.5F, 0.8F}, layers::sensor);
        two.get<Collider>()->isTrigger = true;
        two.add<Lever>();
        Entity &filter = w.scene->createEntity("Only Two");
        auto &only = filter.add<EventAction>();
        only.onEvent = "lever_toggled";
        only.from = two.id();
        only.variableChange = VariableChange::Add;
        only.variable = "two";
        Entity &any = w.scene->createEntity("Any Lever");
        auto &anyLever = any.add<EventAction>();
        anyLever.onEvent = "lever_toggled";
        anyLever.variableChange = VariableChange::Add;
        anyLever.variable = "any";
        w.character("Hero", {-6.0F, restingHeight});
        w.start();
        w.tick(126); // 2.1 s.
        CHECK(w.runtime->blackboard().number("beats") == 4.0);
        w.runtime->teleport(w.at("Hero"), {-3.0F, restingHeight}); // Lever One.
        w.tick(30);
        CHECK(w.runtime->blackboard().number("any") == 1.0 &&
              w.runtime->blackboard().number("two") == 0.0);
        w.runtime->teleport(w.at("Hero"), {3.0F, restingHeight}); // Lever Two.
        w.tick(30);
        CHECK(w.runtime->blackboard().number("any") == 2.0 &&
              w.runtime->blackboard().number("two") == 1.0);
    }
    {
        // The last kind of actions: an animation trigger, a scene change and a restart.
        World w;
        w.assets.files["anim/hero.ykanim"] = R"({"format":"yk.animation","version":2,
            "texture":"assets/hero.png","columns":2,"rows":1,"clips":[
            {"name":"idle","first":0,"count":1,"fps":4},{"name":"pop","first":1,"count":1,"fps":4}]})";
        w.assets.files["anim/hero.ykctl"] = R"({"format":"yk.animator","version":1,
            "parameters":[{"name":"pop","type":"trigger"}],"entry":"Idle",
            "states":[{"name":"Idle","clip":"idle"},{"name":"Popped","clip":"pop"}],
            "transitions":[{"from":"Idle","to":"Popped","when":[{"parameter":"pop"}]}]})";
        w.ground();
        Entity &prop = w.scene->createEntity("Prop");
        prop.add<SpriteRenderer>();
        auto &animated = prop.add<AnimatedSprite>();
        animated.animation.path = "anim/hero.ykanim";
        animated.controller.path = "anim/hero.ykctl";
        Entity &relay = w.scene->createEntity("Relay");
        auto &action = relay.add<EventAction>();
        action.onEvent = "scene_started";
        action.delay = 0.2F;
        action.animate = {prop.id()};
        action.animationTrigger = "pop";
        action.changeScene = "scenes/elsewhere.ykscene";
        w.start();
        w.tick(5);
        CHECK(w.at("Prop").get<AnimatedSprite>()->state() == "Idle");
        w.tick(30);
        CHECK(w.at("Prop").get<AnimatedSprite>()->state() == "Popped");
        CHECK(w.runtime->sceneChangeRequested() == "scenes/elsewhere.ykscene");
    }
}
} // namespace

int main() {
    completionSequence();
    completionWithoutNextSceneAndContinue();
    introTimeLimitAndFailure();
    deathFailsTheLevel();
    transitionsFade();
    nextSceneStartsCovered();
    sessionFollowsScenes();
    sessionKeepsPauseAndViewport();
    eventActionsReact();
    return yk::test::finish("level_flow");
}
