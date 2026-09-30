// Plays the demo game (YK-DemoGame, a separate project that only consumes the engine) headlessly
// with a scripted "bot": if the level is broken (an unreachable exit, a gate that never opens, a
// hazard on the safe path, a controller that does not fit its animation) this fails.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Validation.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/scene/RegistryDocs.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <set>

using namespace yk;

namespace {
struct Actor {
    const char *name;
    Key left, right, jump, use;
};
constexpr Actor ember{"Ember", Key::A, Key::D, Key::W, Key::S};
constexpr Actor tide{"Tide", Key::Left, Key::Right, Key::Up, Key::Down};

struct Game {
    ComponentRegistry registry;
    Project project;
    ProjectAssets assets;
    std::unique_ptr<GameRuntime> runtime;
    Keyboard keyboard;
    std::vector<std::string> events;
    std::set<std::string> animationStates[2]; // Ember, Tide: controller states seen so far.
    bool trace{environmentVariable("YK_TEST_TRACE").has_value()};

    Game() : project(loadProject()), assets(project) {
        registerStandardComponents(registry);
        auto loaded = loadScene(project.resolve(project.startScene).value(), registry);
        CHECK(loaded);
        if (!loaded)
            return;
        RuntimeOptions options;
        options.layers = project.layers;
        options.inputMap = project.input;
        options.assets = &assets;
        auto created = GameRuntime::create(std::move(loaded.value()), options);
        CHECK(created);
        if (created) {
            runtime = std::move(created.value());
            runtime->events().subscribe(
                "*", [this](const GameEvent &event) { events.push_back(event.name); });
        }
    }
    static Project loadProject() {
        auto loaded = Project::load(std::filesystem::path(YK_SOURCE_DIR) / "YK-DemoGame");
        CHECK(loaded);
        return loaded ? loaded.value() : Project{};
    }
    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) {
            runtime->stepOnce(keyboard);
            keyboard.beginFrame();
            for (int who = 0; who < 2; ++who)
                if (const auto *animated =
                        entity(who ? tide.name : ember.name).get<AnimatedSprite>())
                    animationStates[who].insert(animated->state());
            if (trace && runtime->tick() % 15 == 0)
                std::fprintf(stderr,
                             "t=%4llu ember=(%.2f,%.2f)%s tide=(%.2f,%.2f)%s lavaShuttle=%.2f "
                             "gooShuttle=%.2f "
                             "gate=%.2f plates=%d%d\n",
                             static_cast<unsigned long long>(runtime->tick()), pos(ember).x,
                             pos(ember).y, grounded(ember) ? "g" : " ", pos(tide).x, pos(tide).y,
                             grounded(tide) ? "g" : " ", entity("Shuttle Lava").worldPosition().x,
                             entity("Shuttle Goo").worldPosition().x,
                             static_cast<double>(entity("Gate").get<Door>()->openAmount()),
                             entity("Plate Near").get<PressurePlate>()->pressed() ? 1 : 0,
                             entity("Plate Far").get<PressurePlate>()->pressed() ? 1 : 0);
        }
    }
    Entity &entity(const char *name) {
        return *runtime->scene().findByName(name);
    }
    Vec2 pos(const Actor &actor) {
        return entity(actor.name).worldPosition();
    }
    bool grounded(const Actor &actor) {
        return entity(actor.name).get<PlatformerController>()->grounded();
    }
    bool alive(const Actor &actor) {
        return entity(actor.name).get<Killable>()->alive();
    }
    void release(const Actor &actor) {
        keyboard.set(actor.left, false);
        keyboard.set(actor.right, false);
    }
    // Steers toward x; true (with the keys released) once within `tolerance`.
    bool walkTo(const Actor &actor, float x, float tolerance = 0.2F) {
        const float delta = x - pos(actor).x;
        if (std::fabs(delta) <= tolerance) {
            release(actor);
            return true;
        }
        keyboard.set(delta > 0 ? actor.right : actor.left, true);
        keyboard.set(delta > 0 ? actor.left : actor.right, false);
        return false;
    }
    // Ticks until done() or the limit; false on timeout.
    bool until(int limit, const std::function<bool()> &done) {
        for (int i = 0; i < limit; ++i) {
            if (done())
                return true;
            tick();
        }
        return done();
    }
    // Jumps (holding the key for `hold` ticks) while steering `direction` (+1 right, -1 left, 0
    // none) until `airTicks` have passed, then releases the steering and waits to land.
    bool hop(const Actor &actor, int direction, int hold = 40, int airTicks = 40) {
        keyboard.set(actor.jump, true);
        for (int i = 0; i < airTicks; ++i) {
            keyboard.set(actor.right, direction > 0);
            keyboard.set(actor.left, direction < 0);
            if (i == hold)
                keyboard.set(actor.jump, false);
            tick();
        }
        keyboard.set(actor.jump, false);
        release(actor);
        return until(120, [&] { return grounded(actor); });
    }
    float velocityX(const Actor &actor) {
        const auto body = runtime->bodyOf(entity(actor.name).id());
        return body ? runtime->physics().state(*body).value().linearVelocity.x : 0.0F;
    }
    // Jumps and steers in the air so that the landing is near `targetX`; true once it has landed.
    bool hopTo(const Actor &actor, float targetX, int holdTicks = 45) {
        keyboard.set(actor.jump, true);
        bool airborne = false;
        int ticks = 0;
        const bool landed = until(240, [&] {
            if (++ticks > holdTicks)
                keyboard.set(actor.jump, false);
            airborne = airborne || !grounded(actor);
            const float error = targetX - (pos(actor).x + velocityX(actor) * 0.2F);
            keyboard.set(actor.right, error > 0.08F);
            keyboard.set(actor.left, error < -0.08F);
            return airborne && grounded(actor);
        });
        keyboard.set(actor.jump, false);
        release(actor);
        return landed;
    }
    // Presses the "use" key for a few ticks (a lever flips on the press).
    void use(const Actor &actor) {
        keyboard.set(actor.use, true);
        tick(3);
        keyboard.set(actor.use, false);
        tick(2);
    }
    bool happened(const std::string &name) const {
        return std::find(events.begin(), events.end(), name) != events.end();
    }
    Entity &at(const char *name) {
        return entity(name);
    }
};

double runtimeNumber(Game &game, const char *name) {
    return game.runtime->blackboard().number(name);
}

void projectIsValid() {
    Game game;
    const auto issues = validateProject(game.project, game.registry);
    for (const ProjectIssue &issue : issues)
        std::fprintf(stderr, "issue: %s: %s\n", issue.path.c_str(), issue.message.c_str());
    CHECK(issues.empty());
    CHECK(game.project.startScene == "scenes/level01.ykscene");
    CHECK(game.project.layers.indexOf("Player") >= 0 &&
          !game.project.layers.interacts(2, 2)); // Players pass through each other.
    // The level is built from prefabs: every placed instance says which one.
    std::size_t instances = 0;
    game.runtime->scene().forEach([&](const Entity &entity) {
        if (!entity.prefabSource().empty())
            ++instances;
    });
    CHECK(instances > 60);
}

void levelIsCompletable() {
    Game game;
    if (!game.runtime)
        return;
    GameRuntime &runtime = *game.runtime;
    game.tick(30);
    CHECK(game.grounded(ember) && game.grounded(tide));

    // 1. Ember wades through the lava pit (it does not hurt her) collecting the gems on its
    //    floor, then climbs out and flips the lever with the "use" button.
    CHECK(game.until(900, [&] { return game.walkTo(ember, 13.6F, 0.3F); }));
    CHECK(game.alive(ember));
    CHECK(runtime.blackboard().number("ember_gems") >= 2.0);
    CHECK(game.hop(ember, 1, 30, 30));
    CHECK(game.until(300, [&] { return game.walkTo(ember, 15.6F, 0.15F); }));
    game.use(ember);
    CHECK(game.entity("Lever").get<Lever>()->on());
    CHECK(game.until(300, [&] { return game.entity("Gate").get<Door>()->openAmount() >= 1.0F; }));
    CHECK(game.happened("lever_toggled") && game.happened("door_opened"));

    // 2. Tide cannot wade through lava: he jumps onto the platform that hovers over it, rides
    //    across and drops off on the far side.
    const auto shuttleX = [&] { return game.entity("Shuttle Lava").worldPosition().x; };
    CHECK(game.until(900, [&] { return game.walkTo(tide, 7.3F, 0.15F); }));
    CHECK(game.until(600, [&] { return shuttleX() < 9.56F; })); // It has just arrived and waits.
    game.keyboard.set(tide.jump, true);
    bool airborne = false;
    CHECK(game.until(150, [&] {
        airborne = airborne || !game.grounded(tide);
        game.keyboard.set(tide.right, game.pos(tide).x < shuttleX() - 0.9F);
        if (game.pos(tide).y < 13.8F)
            game.keyboard.set(tide.jump, false);
        return airborne && game.grounded(tide);
    }));
    game.keyboard.set(tide.jump, false);
    game.release(tide);
    CHECK(game.alive(tide) && game.pos(tide).y < 14.0F); // On the platform, not in the lava.
    CHECK(game.until(600, [&] { return shuttleX() > 12.4F; }));
    CHECK(game.until(300, [&] { return game.walkTo(tide, 15.4F, 0.3F); }));
    CHECK(game.until(200, [&] { return game.grounded(tide); }));
    CHECK(game.alive(tide) && game.pos(tide).y > 16.0F);    // Dropped onto the far side.
    CHECK(runtime.blackboard().number("tide_gems") >= 1.0); // The gem above the lava, on the way.

    // 3. Through the open gate to the checkpoint (both) and to the edge of the water.
    CHECK(game.until(900, [&] {
        const bool a = game.walkTo(ember, 24.3F, 0.2F);
        const bool b = game.walkTo(tide, 23.8F, 0.2F);
        return a && b;
    }));
    CHECK(game.happened("checkpoint_reached"));

    // 4. Water: Tide wades through it (blue gems on the floor of the pool) and climbs out. Ember
    //    takes the jump-through ledges over it and collects a red gem on the way.
    CHECK(game.until(900, [&] { return game.walkTo(tide, 30.4F, 0.3F); }));
    CHECK(game.alive(tide));
    CHECK(runtime.blackboard().number("tide_gems") >= 3.0);
    CHECK(game.hop(tide, 1, 30, 30));
    CHECK(game.until(300, [&] { return game.walkTo(tide, 33.0F, 0.3F); }));
    CHECK(game.until(300, [&] { return game.walkTo(ember, 24.7F, 0.1F); }));
    CHECK(game.hopTo(ember, 26.4F));
    CHECK(game.pos(ember).y < 15.7F); // Standing on the first ledge.
    CHECK(game.until(300, [&] { return game.walkTo(ember, 27.6F, 0.2F); }));
    CHECK(game.hopTo(ember, 29.4F));
    CHECK(game.pos(ember).y < 15.7F); // ...and on the second.
    CHECK(runtime.blackboard().number("ember_gems") >= 4.0);
    CHECK(game.until(300, [&] { return game.walkTo(ember, 32.0F, 0.3F); }));
    CHECK(game.alive(ember) && game.alive(tide));

    // 5. Up the stairs of jump-through ledges to the upper floor, one after the other.
    const auto climbStairs = [&](const Actor &who) {
        CHECK(game.until(600, [&] { return game.walkTo(who, 41.3F, 0.2F); }));
        CHECK(game.hopTo(who, 41.3F));
        CHECK(game.pos(who).y < 14.8F); // Stair 1
        CHECK(game.hopTo(who, 39.0F));
        CHECK(game.pos(who).y < 13.0F); // Stair 2
        CHECK(game.hopTo(who, 36.8F));
        CHECK(game.pos(who).y < 11.2F); // Stair 3
        CHECK(game.hopTo(who, 34.8F));
        CHECK(game.pos(who).y < 10.2F); // The upper floor
    };
    climbStairs(ember);
    CHECK(game.until(300, [&] { return game.walkTo(ember, 33.4F, 0.2F); }));
    CHECK(game.entity("Plate Near").get<PressurePlate>()->pressed());
    climbStairs(tide);

    // 6. The pit of goo kills both, and the only way across is a platform that runs only while a
    //    plate on either side is held. Ember holds the near plate; Tide rides across and holds the
    //    far one; then Ember can follow.
    const auto gooX = [&] { return game.entity("Shuttle Goo").worldPosition().x; };
    CHECK(game.until(300, [&] { return game.walkTo(tide, 32.5F, 0.2F); }));
    CHECK(game.until(600, [&] { return gooX() > 30.4F; })); // It has come to the near edge.
    CHECK(game.until(300, [&] { return game.walkTo(tide, 30.9F, 0.2F); }));
    CHECK(game.until(900, [&] { return gooX() < 27.6F; })); // Carried across.
    CHECK(game.until(300, [&] { return game.walkTo(tide, 25.0F, 0.2F); }));
    CHECK(game.alive(tide) && game.pos(tide).y < 10.2F);
    CHECK(game.entity("Plate Far").get<PressurePlate>()->pressed());
    // Ember steps off her plate; the far plate keeps the platform going for her.
    CHECK(game.until(300, [&] { return game.walkTo(ember, 32.6F, 0.2F); }));
    CHECK(game.until(900, [&] { return gooX() > 30.4F; }));
    CHECK(game.until(300, [&] { return game.walkTo(ember, 30.9F, 0.2F); }));
    CHECK(game.until(900, [&] { return gooX() < 27.6F; }));
    CHECK(game.until(300, [&] { return game.walkTo(ember, 21.4F, 0.2F); }));
    CHECK(game.alive(ember) && game.alive(tide));

    // 7. Into the exits.
    CHECK(game.until(300, [&] { return game.walkTo(tide, 23.6F, 0.2F); }));
    CHECK(game.until(600, [&] { return game.entity("Level Flow").get<LevelFlow>()->completed(); }));
    CHECK(game.happened("level_completed") &&
          runtime.blackboard().text("level_state") == "complete");
    CHECK(runtime.blackboard().text("level_message") == "LEVEL COMPLETE!");
    CHECK(!runtime.blackboard().has("deaths")); // Nobody had to die for it.
    std::fprintf(
        stderr, "completed in %.1f simulated seconds; gems: ember %.0f/%.0f tide %.0f/%.0f\n",
        runtime.time(), runtime.blackboard().number("ember_gems"),
        runtime.blackboard().number("ember_gems_total"), runtime.blackboard().number("tide_gems"),
        runtime.blackboard().number("tide_gems_total"));
}

void hazardsAreCharacterSpecific() {
    // Lava hurts only Tide, water only Ember, goo both; a dead character returns to the last
    // checkpoint (or the start), and nobody else is affected.
    Game game;
    if (!game.runtime)
        return;
    game.tick(10);
    game.runtime->teleport(game.entity(tide.name), {11.0F, 16.2F}); // Into the lava.
    CHECK(game.until(120, [&] { return !game.alive(tide); }));
    CHECK(game.alive(ember));
    CHECK(game.until(400, [&] { return game.alive(tide); })); // Returns to the start.
    game.tick(30);
    CHECK(game.pos(tide).x < 5.0F);
    game.runtime->teleport(game.entity(ember.name), {11.0F, 16.2F});
    game.tick(90);
    CHECK(game.alive(ember) && game.pos(ember).y > 17.0F); // Ember stands in the lava, unharmed.

    game.runtime->teleport(game.entity(ember.name), {23.5F, 16.0F}); // Touch the checkpoint...
    game.tick(20);
    CHECK(game.happened("checkpoint_reached"));
    game.runtime->teleport(game.entity(ember.name), {28.0F, 16.2F}); // ...then fall into the water.
    CHECK(game.until(120, [&] { return !game.alive(ember); }));
    CHECK(game.until(400, [&] { return game.alive(ember); }));
    game.tick(20);
    CHECK_NEAR(game.pos(ember).x, 23.5, 0.4); // Back at the checkpoint, not the start.
    game.runtime->teleport(game.entity(tide.name), {26.0F, 16.2F});
    game.tick(60);
    CHECK(game.alive(tide) && game.pos(tide).y > 17.0F); // The water is safe for Tide...
    CHECK(game.until(300, [&] { return game.walkTo(tide, 29.0F, 0.2F); }));
    CHECK(game.alive(tide) && runtimeNumber(game, "tide_gems") >= 2.0); // ...and holds his gems.

    game.runtime->teleport(game.entity(ember.name), {30.8F, 9.0F}); // The goo pit: both die.
    game.runtime->teleport(game.entity(tide.name), {30.2F, 9.0F});
    CHECK(game.until(200, [&] { return !game.alive(ember) && !game.alive(tide); }));
    CHECK(runtimeNumber(game, "deaths") == 4.0); // Lava (Tide), water (Ember), goo (both).
}

void charactersAnimateFromTheirController() {
    // The real sprite sheets and the shared state machine: every state of a character is reached
    // by playing (idle, run, jump, fall, land, interact with the lever, death).
    Game game;
    if (!game.runtime)
        return;
    game.tick(30);
    game.until(90, [&] { return game.walkTo(ember, 5.0F, 0.2F); });
    game.tick(10);
    game.until(300, [&] { return game.walkTo(ember, 3.0F, 0.2F); });
    game.hop(ember, 0, 12, 12);
    game.runtime->teleport(game.entity(ember.name), {15.6F, 16.0F});
    game.tick(40);
    game.use(ember);
    game.tick(30);
    game.runtime->teleport(game.entity(ember.name), {28.0F, 16.2F});
    game.tick(60);
    game.tick(120);
    for (const char *state : {"Idle", "Run", "Jump", "Fall", "Land", "Interact", "Death"}) {
        if (!game.animationStates[0].contains(state))
            std::fprintf(stderr, "Ember never entered the animation state '%s'\n", state);
        CHECK(game.animationStates[0].contains(state));
    }
    CHECK(game.entity(ember.name).get<AnimatedSprite>()->state() ==
          "Idle"); // Alive and well again.
}

void restartResets() {
    Game game;
    if (!game.runtime)
        return;
    game.tick(30);
    game.keyboard.set(tide.right, true);
    game.tick(60);
    game.keyboard.set(tide.right, false);
    game.runtime->blackboard().set("ember_gems", 1.0);
    game.keyboard.set(Key::R, true);
    game.tick(1);
    CHECK(game.runtime->tick() <= 1); // Restarted the whole level.
    CHECK(!game.runtime->blackboard().has("ember_gems") ||
          game.runtime->blackboard().number("ember_gems") == 0.0);
    game.tick(20);
    CHECK_NEAR(game.pos(tide).x, 3.4, 0.2);
    CHECK(!game.entity("Lever").get<Lever>()->on());
}

void componentReferenceIsCurrent() {
    // docs/components.md is generated from the registry; it must match what the code registers.
    ComponentRegistry registry;
    registerStandardComponents(registry);
    auto stored = readTextFile(std::filesystem::path(YK_SOURCE_DIR) / "docs" / "components.md");
    CHECK(stored);
    if (stored)
        CHECK(stored.value() == describeRegistryMarkdown(registry));
}
} // namespace

int main() {
    projectIsValid();
    levelIsCompletable();
    hazardsAreCharacterSpecific();
    charactersAnimateFromTheirController();
    restartResets();
    componentReferenceIsCurrent();
    return yk::test::finish("demo");
}
