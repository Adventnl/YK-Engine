// Perception: what characters see and hear. Sight needs range, the cone (or the all-round range)
// and a clear line over the navigation grid; hidden and invisible subjects are harder to see;
// awareness builds, crosses the suspicious and aware thresholds, falls once the subject is gone and
// leaves a memory of where it was; noise carries by loudness and is damped by walls; footsteps
// are noise; rules ask CanSee / AwareOf and make noise; state saves; a hundred looking at a
// hundred stays cheap.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/core/Log.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/gameplay/Navigation.hpp"
#include "yk/gameplay/Perception.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/sim/Identity.hpp"
#include "yk/stats/Stats.hpp"
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using namespace yk;

namespace {
const char *tilesetText = R"({
  "format": "yk.tileset", "version": 1, "name": "Arena", "texture": "",
  "tileWidth": 16, "tileHeight": 16, "columns": 4, "rows": 1,
  "tiles": [{"id": 1, "solid": true, "opaque": true, "noiseDamping": 0.9}]
})";

struct Rig {
    ComponentRegistry registry;
    MemoryAssets assets;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<GameRuntime> runtime;
    std::vector<GameEvent> heard;

    // Rows of text, one tile (a meter) per character, '#' a wall; x grows right, y down the text.
    explicit Rig(const std::vector<std::string> &rows) {
        registerStandardComponents(registry);
        assets.files["tiles.yktileset"] = tilesetText;
        assets.files["data/world.ykdata"] =
            R"({"format": "yk.data", "version": 1, "factions": [{"id": "staff"}, {"id": "inmates"}]})";
        scene = std::make_unique<Scene>(registry, 5);
        scene->settings.gravity = {0.0F, 0.0F};
        auto &settings = scene->createEntity("Navigation").add<NavigationSettings>();
        settings.boundsMin = {-2.0F, -2.0F};
        settings.boundsMax = {24.0F, 10.0F};
        Entity &map = scene->createEntity("Map");
        auto &tiles = map.add<Tilemap>();
        tiles.tileset.path = "tiles.yktileset";
        const std::size_t walls = tiles.addLayer(TileLayer::make("Walls", TileLayerKind::Wall));
        for (std::size_t y = 0; y < rows.size(); ++y)
            for (std::size_t x = 0; x < rows[y].size(); ++x)
                if (rows[y][x] == '#')
                    tiles.setTile(walls, static_cast<int>(x), static_cast<int>(y), tile::make(1));
    }
    // A character at the middle of a tile.
    Entity &person(const std::string &name, float x, float y, const char *faction = "") {
        Entity &entity = scene->createEntity(name);
        entity.transform().position = {x + 0.5F, y + 0.5F};
        auto &who = entity.add<Identity>();
        who.id = std::string("npc.") + name;
        who.faction = faction;
        return entity;
    }
    Perceiver &watcher(const char *name, float x, float y, Vec2 look) {
        Entity &entity = person(name, x, y, "staff");
        auto &eyes = entity.add<Perceiver>();
        eyes.lookDirection = look;
        eyes.noticeSeconds = 0.5F;
        return eyes;
    }
    void start() {
        RuntimeOptions options;
        options.assets = &assets;
        options.layers = layers::standard();
        auto created = GameRuntime::create(std::move(scene), options);
        CHECK(created);
        if (!created)
            return;
        runtime = std::move(created.value());
        runtime->events().subscribe("*", [this](const GameEvent &e) { heard.push_back(e); });
    }
    void tick(int count = 1) {
        for (int i = 0; i < count; ++i)
            runtime->stepOnce(Keyboard{});
    }
    Entity &entity(const char *name) {
        return *runtime->scene().findByName(name);
    }
    Perceiver &eyes(const char *name) {
        return *entity(name).get<Perceiver>();
    }
    int count(const char *name) const {
        return static_cast<int>(std::count_if(heard.begin(), heard.end(),
                                              [&](const GameEvent &e) { return e.name == name; }));
    }
};

const std::vector<std::string> open = {"....................", "....................",
                                       "....................", "....................",
                                       "...................."};

void seeing() {
    Rig rig(open);
    rig.watcher("Guard", 2, 2, {1, 0});
    rig.person("Front", 8, 2, "inmates");  // 6 m ahead.
    rig.person("Behind", 0, 2, "inmates"); // 2 m behind it.
    rig.person("Far", 19, 2, "inmates");   // 17 m: out of range.
    rig.person("Beside", 2, 4, "inmates"); // 2 m to the side, 90 degrees off.
    rig.person("Near", 2, 3, "inmates");   // 1 m: inside the all-round range.
    rig.start();
    rig.tick(1);
    Perceiver &guard = rig.eyes("Guard");
    const Entity &guardEntity = rig.entity("Guard");
    CHECK(canSee(*rig.runtime, guardEntity, rig.entity("Front")));
    CHECK(!canSee(*rig.runtime, guardEntity, rig.entity("Behind")));
    CHECK(!canSee(*rig.runtime, guardEntity, rig.entity("Far")));
    CHECK(!canSee(*rig.runtime, guardEntity, rig.entity("Beside")));
    CHECK(canSee(*rig.runtime, guardEntity, rig.entity("Near")));
    rig.tick(60);
    CHECK(guard.stateOf(rig.entity("Front").id()) == AwarenessState::Aware);
    CHECK(guard.stateOf(rig.entity("Near").id()) == AwarenessState::Aware);
    CHECK(guard.stateOf(rig.entity("Behind").id()) == AwarenessState::Unaware);
    CHECK(guard.stateOf(rig.entity("Far").id()) == AwarenessState::Unaware);
    CHECK(rig.count("perception.noticed") == 2 && rig.count("perception.suspicious") == 2);
    CHECK(guard.sees(rig.entity("Front").id()) && !guard.sees(rig.entity("Behind").id()));
    // Turning around changes who it sees.
    guard.lookDirection = {-1, 0};
    CHECK(canSee(*rig.runtime, guardEntity, rig.entity("Behind")) &&
          !canSee(*rig.runtime, guardEntity, rig.entity("Front")));
}

void blocked() {
    Rig rig({"....#...............", "....#...............", "....#...............",
             "....#...............", "....#..............."});
    rig.watcher("Guard", 2, 2, {1, 0});
    rig.person("Other", 8, 2, "inmates");
    rig.start();
    rig.tick(60);
    CHECK(!canSee(*rig.runtime, rig.entity("Guard"), rig.entity("Other")));
    CHECK(rig.eyes("Guard").stateOf(rig.entity("Other").id()) == AwarenessState::Unaware);
    CHECK(rig.count("perception.suspicious") == 0);
}

void modifiers() {
    Rig rig(open);
    rig.watcher("Guard", 2, 2, {1, 0});
    Entity &hider = rig.person("Hider", 8, 2, "inmates");
    hider.add<Perceivable>();
    Entity &dark = rig.person("Dark", 8, 3, "inmates");
    dark.add<Perceivable>().visibility = 0.4F; // Seen from 4 m.
    rig.person("Friend", 7, 1, "staff");
    rig.start();
    rig.tick(1);
    Entity &guard = rig.entity("Guard");
    CHECK(canSee(*rig.runtime, guard, rig.entity("Hider")));
    rig.entity("Hider").get<Perceivable>()->hidden = true;
    CHECK(!canSee(*rig.runtime, guard, rig.entity("Hider")));
    rig.entity("Hider").transform().position = {3.2F, 2.5F}; // Next to it.
    CHECK(canSee(*rig.runtime, guard, rig.entity("Hider")));
    CHECK(!canSee(*rig.runtime, guard, rig.entity("Dark"))); // 6 m with range 4.
    // A friend is seen but not watched.
    rig.tick(60);
    CHECK(rig.eyes("Guard").stateOf(rig.entity("Friend").id()) == AwarenessState::Unaware);
    rig.eyes("Guard").interest = "any";
    rig.tick(60);
    CHECK(rig.eyes("Guard").stateOf(rig.entity("Friend").id()) == AwarenessState::Aware);
    rig.eyes("Guard").blind = true;
    CHECK(!canSee(*rig.runtime, guard, rig.entity("Friend")));
}

void forgetting() {
    Rig rig(open);
    Perceiver &guard = rig.watcher("Guard", 2, 2, {1, 0});
    guard.loseSeconds = 2.0F;
    guard.memorySeconds = 5.0F;
    rig.person("Thief", 7, 2, "inmates");
    rig.start();
    rig.tick(60);
    const EntityId thief = rig.entity("Thief").id();
    CHECK(guard.stateOf(thief) == AwarenessState::Aware);
    // It slips away.
    rig.entity("Thief").transform().position = {2.5F, 4.5F + 12.0F};
    rig.tick(30);
    CHECK(guard.about(thief) && !guard.about(thief)->seeing);
    CHECK(guard.about(thief)->lastKnown.x > 6.0F && guard.about(thief)->lastSense == "sight");
    rig.tick(120);
    CHECK(guard.stateOf(thief) == AwarenessState::Unaware && rig.count("perception.lost") == 1);
    CHECK(guard.about(thief)); // Still remembered where it was.
    rig.tick(60 * 6);
    CHECK(!guard.about(thief));
    // Forgetting on purpose.
    rig.entity("Thief").transform().position = {7.5F, 2.5F};
    rig.tick(60);
    CHECK(guard.stateOf(thief) == AwarenessState::Aware);
    guard.forget(*rig.runtime, thief);
    rig.tick(1);
    CHECK(!guard.about(thief) && rig.count("perception.lost") == 2);
}

void noise() {
    Rig rig({"....#...............", "....#...............", "....#...............",
             "....#...............", "....#..............."});
    rig.watcher("Near", 6, 2, {0, 1});
    rig.watcher("Walled", 2, 2, {0, 1});
    rig.watcher("Far", 18, 4, {0, 1});
    rig.person("Maker", 8, 2, "inmates");
    rig.start();
    rig.tick(1);
    auto &service = rig.runtime->services().get<PerceptionService>();
    const Entity &maker = rig.entity("Maker");
    const int heard = static_cast<int>(
        service.makeNoise(*rig.runtime, maker.worldPosition(), 0, 5.0F, "bang", maker.id()));
    CHECK(heard == 1); // Near hears it; the wall damps it for Walled; Far is 10 m off.
    CHECK(rig.eyes("Near").lastNoise() && rig.eyes("Near").lastNoise()->kind == "bang");
    CHECK(rig.eyes("Near").stateOf(maker.id()) == AwarenessState::Suspicious);
    CHECK(!rig.eyes("Walled").lastNoise() && !rig.eyes("Far").lastNoise());
    rig.tick(1);
    CHECK(rig.count("perception.heard") == 1);
    // A loud noise carries far in the open, but a thick wall still stops it.
    CHECK(service.makeNoise(*rig.runtime, maker.worldPosition(), 0, 100.0F, "alarm") == 2);
    CHECK(!rig.eyes("Walled").lastNoise() && rig.eyes("Far").lastNoise());
    // Deaf ears hear nothing; silence makes no noise.
    rig.eyes("Near").deaf = true;
    rig.eyes("Near").forgetAll(*rig.runtime);
    CHECK(service.makeNoise(*rig.runtime, maker.worldPosition(), 0, 0.0F, "none") == 0);
    // Another level does not hear it.
    CHECK(service.makeNoise(*rig.runtime, maker.worldPosition(), 3, 60.0F, "elsewhere") == 0);
}

void footsteps() {
    Rig rig(open);
    rig.watcher("Listener", 2, 2, {0, 1}).sightRange = 0.0F;
    Entity &walker = rig.person("Walker", 10, 2, "inmates");
    walker.add<Perceivable>();
    rig.start();
    rig.tick(1);
    // Walking 4 m/s toward the listener: footsteps carry 3 m and are heard once close.
    for (int i = 0; i < 180; ++i) {
        Vec2 at = rig.entity("Walker").transform().position;
        at.x -= 4.0F / 60.0F;
        rig.entity("Walker").transform().position = at;
        rig.tick();
    }
    CHECK(rig.count("perception.heard") >= 1);
    CHECK(rig.eyes("Listener").stateOf(rig.entity("Walker").id()) != AwarenessState::Unaware);
    // A sneaker (noise factor 0 via walkNoise 0) is not heard.
    Rig quiet(open);
    quiet.watcher("Listener", 2, 2, {0, 1}).sightRange = 0.0F;
    quiet.person("Walker", 10, 2, "inmates").add<Perceivable>().walkNoise = 0.0F;
    quiet.start();
    for (int i = 0; i < 180; ++i) {
        Vec2 at = quiet.entity("Walker").transform().position;
        at.x -= 4.0F / 60.0F;
        quiet.entity("Walker").transform().position = at;
        quiet.tick();
    }
    CHECK(quiet.count("perception.heard") == 0);
}

void rules() {
    Rig rig(open);
    rig.watcher("Guard", 2, 2, {1, 0});
    rig.person("Thief", 6, 2, "inmates");
    rig.start();
    rig.tick(60);
    RuleContext context(*rig.runtime);
    context.actor = rig.entity("Guard").id();
    const auto holds = [&](const char *json) {
        auto condition = Condition::fromJson(Json::parse(json).value());
        CHECK(condition);
        return condition && evaluate(condition.value(), context);
    };
    CHECK(holds(R"({"type": "CanSee", "target": "name:Thief"})"));
    CHECK(holds(R"({"type": "AwareOf", "target": "name:Thief"})"));
    CHECK(!holds(R"({"type": "CanSee", "target": "name:Guard", "entity": "name:Thief"})"));
    const auto act = [&](const char *json) {
        auto action = Action::fromJson(Json::parse(json).value());
        CHECK(action);
        if (action)
            execute({action.value()}, context);
    };
    act(R"({"type": "MakeNoise", "loudness": 20, "kind": "scream", "entity": "name:Thief"})");
    rig.tick(1);
    CHECK(rig.count("perception.heard") >= 1);
    act(R"({"type": "ForgetSubject", "target": "name:Thief"})");
    CHECK(!holds(R"({"type": "AwareOf", "target": "name:Thief"})"));
}

void saving() {
    Rig rig(open);
    rig.watcher("Guard", 2, 2, {1, 0});
    rig.person("Thief", 6, 2, "inmates");
    rig.start();
    rig.tick(60);
    Perceiver &guard = rig.eyes("Guard");
    const Json state = guard.saveState();
    const EntityId thief = rig.entity("Thief").id();
    guard.forgetAll(*rig.runtime);
    CHECK(!guard.about(thief));
    CHECK(guard.loadState(*rig.runtime, state));
    CHECK(guard.about(thief) && guard.stateOf(thief) == AwarenessState::Aware);
}

void crowd() {
    Rig rig(open);
    for (int i = 0; i < 100; ++i) {
        const std::string name = "P" + std::to_string(i);
        Entity &entity = rig.person(name, static_cast<float>(i % 20), static_cast<float>(i / 20),
                                    i % 2 ? "staff" : "inmates");
        auto &eyes = entity.add<Perceiver>();
        eyes.lookDirection = {1, 0};
        entity.add<Perceivable>();
    }
    rig.start();
    const auto begin = std::chrono::steady_clock::now();
    rig.tick(120);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    CHECK(seconds < 5.0);
    CHECK(rig.runtime->services().get<PerceptionService>().looks() > 1000);
}
} // namespace

int main() {
    setLogStderrEnabled(false);
    seeing();
    blocked();
    modifiers();
    forgetting();
    noise();
    footsteps();
    rules();
    saving();
    crowd();
    return yk::test::finish("perception");
}
