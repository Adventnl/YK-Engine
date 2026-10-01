// The world model: the spatial hash, world levels, and what levels do to physics.
#include "support/check.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include "yk/world/SpatialHash.hpp"
#include "yk/world/WorldLevels.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

using namespace yk;

namespace {
EntityId id(std::uint64_t value) {
    return EntityId{value};
}

void spatialHash() {
    SpatialHash hash(4.0F);
    hash.insert(id(1), {0.0F, 0.0F}, 0.0F, 0);
    hash.insert(id(2), {3.0F, 0.0F}, 0.0F, 0);
    hash.insert(id(3), {50.0F, 50.0F}, 0.0F, 0);
    hash.insert(id(4), {0.0F, 1.0F}, 0.0F, 1);                       // Same place, upstairs.
    hash.insert(id(5), {-30.0F, 0.0F}, 0.0F, SpatialHash::anyLevel); // On every level.
    CHECK(hash.size() == 5 && hash.contains(id(3)) && !hash.contains(id(9)));
    // Nearest first; the upstairs item is not seen from the ground level.
    auto near = hash.queryCircle({0.0F, 0.0F}, 5.0F, 0);
    CHECK(near.size() == 2 && near[0].id == id(1) && near[1].id == id(2));
    CHECK_NEAR(near[1].distance, 3.0);
    // Every level when asked so, and items on every level are seen from any level.
    CHECK(hash.queryCircle({0.0F, 0.0F}, 5.0F, SpatialHash::anyLevel).size() == 3);
    const auto wide = hash.queryCircle({-28.0F, 0.0F}, 3.0F, 1);
    CHECK(wide.size() == 1 && wide[0].id == id(5));
    // An item with a radius is found by a circle that only touches its edge.
    hash.insert(id(6), {20.0F, 0.0F}, 3.0F, 0);
    CHECK(hash.queryCircle({15.0F, 0.0F}, 2.1F, 0).size() == 1); // 5 away; reach 3 + 2.1.
    CHECK(hash.queryCircle({15.0F, 0.0F}, 1.9F, 0).empty());     // Reach 4.9: just short.
    // Each item once, however many buckets it spans (the radius-3 item sits in several).
    const auto big = hash.queryCircle({20.0F, 0.0F}, 30.0F, 0);
    CHECK(big.size() == 3 && std::count_if(big.begin(), big.end(), [](const SpatialHash::Hit &hit) {
                                 return hit.id == id(6);
                             }) == 1);
    // Moving within a bucket and across buckets.
    hash.update(id(1), {1.0F, 1.0F}, 0.0F, 0);
    CHECK(hash.queryCircle({1.0F, 1.0F}, 0.1F, 0).front().id == id(1));
    hash.update(id(1), {100.0F, 100.0F}, 0.0F, 0);
    CHECK(hash.queryCircle({0.0F, 0.0F}, 2.0F, 0).empty());
    CHECK(hash.queryCircle({100.0F, 100.0F}, 0.5F, 0).front().id == id(1));
    hash.update(id(1), {100.0F, 100.0F}, 0.0F, 1); // Changing level moves it between grids.
    CHECK(hash.queryCircle({100.0F, 100.0F}, 0.5F, 0).empty());
    CHECK(hash.queryCircle({100.0F, 100.0F}, 0.5F, 1).front().id == id(1));
    // Removal, and queries that cannot match.
    CHECK(hash.remove(id(1)) && !hash.remove(id(1)));
    CHECK(hash.queryCircle({100.0F, 100.0F}, 0.5F, 1).empty());
    CHECK(hash.queryCircle({0.0F, 0.0F}, -1.0F, 0).empty());
    CHECK(hash.queryCircle({std::nanf(""), 0.0F}, 5.0F, 0).empty());
    hash.insert(id(7), {std::nanf(""), 0.0F}); // Rejected quietly.
    CHECK(!hash.contains(id(7)));
    // Rectangles and "nearest that qualifies".
    const auto inside = hash.queryRect({{-1.0F, -1.0F}, {5.0F, 5.0F}}, 0);
    CHECK(inside.size() == 1 && inside[0] == id(2));
    CHECK(hash.nearest({0.0F, 0.0F}, 10.0F, 0) == id(2));
    CHECK(hash.nearest({0.0F, 0.0F}, 10.0F, 0, [](EntityId who) { return who != id(2); }) ==
          EntityId{});
    CHECK(hash.nearest({0.0F, 0.0F}, 1.0F, 0) == EntityId{});
    Vec2 position;
    float radius = 0;
    int level = 0;
    CHECK(hash.find(id(6), position, radius, level) && radius == 3.0F && level == 0);
    hash.clear();
    CHECK(hash.size() == 0 && hash.queryCircle({0, 0}, 100.0F, SpatialHash::anyLevel).empty());
}

void spatialHashScales() {
    // Ten thousand scattered items: a query touches a handful of buckets and a few items, not all.
    SpatialHash hash(4.0F);
    std::mt19937 random(7);
    std::uniform_real_distribution<float> where(0.0F, 400.0F);
    for (std::uint64_t i = 1; i <= 10000; ++i)
        hash.insert(id(i), {where(random), where(random)}, 0.0F, static_cast<int>(i % 3));
    const std::uint64_t before = hash.bucketVisits();
    std::size_t found = 0;
    for (int i = 0; i < 1000; ++i)
        found += hash.queryCircle({where(random), where(random)}, 8.0F, 1).size();
    const std::uint64_t visited = hash.bucketVisits() - before;
    CHECK(visited / 1000 < 100); // About 25 buckets of two level slots per query.
    CHECK(found > 0);
    // Moving everything one step keeps the answers right (compared with brute force).
    std::vector<Vec2> positions(10001);
    for (std::uint64_t i = 1; i <= 10000; ++i) {
        Vec2 at;
        float r = 0;
        int l = 0;
        hash.find(id(i), at, r, l);
        positions[i] = at + Vec2{1.5F, -0.5F};
        hash.update(id(i), positions[i], 0.0F, static_cast<int>(i % 3));
    }
    for (int q = 0; q < 20; ++q) {
        const Vec2 center{where(random), where(random)};
        std::size_t expected = 0;
        for (std::uint64_t i = 1; i <= 10000; ++i)
            if (i % 3 == 0 && distance(positions[i], center) <= 12.0F)
                ++expected;
        CHECK(hash.queryCircle(center, 12.0F, 0).size() == expected);
    }
}

void levelSets() {
    WorldLevelSet set;
    CHECK(set.empty() && set.count() == 1 && set.indexOf("") == 0 &&
          set.indexOf("x") == unknownLevel);
    set.levels = {{"vents", "Vents", LevelKind::Vent, -1.0F},
                  {"ground", "", LevelKind::Floor, 0.0F},
                  {"floor1", "First floor", LevelKind::Floor, 3.0F},
                  {"roof", "Roof", LevelKind::Roof, 6.0F}};
    set.viewMode = LevelViewMode::FocusAndBelow;
    set.belowAlpha = 0.5F;
    CHECK(set.validate() && set.count() == 4);
    CHECK(set.indexOf("floor1") == 2 && set.indexOf("*") == everyLevel &&
          set.indexOf("nowhere") == unknownLevel && set.idOf(3) == "roof" && set.idOf(9).empty());
    CHECK(set.at(1)->label() == "ground" && set.at(2)->label() == "First floor");
    auto round = WorldLevelSet::fromJson(set.toJson());
    CHECK(round && round.value() == set);
    // Mistakes are named.
    WorldLevelSet bad = set;
    bad.levels.push_back({"roof", "", LevelKind::Roof, 0.0F});
    CHECK(!bad.validate() && bad.validate().error().find("twice") != std::string::npos);
    bad.levels.back().id = "Upper Case";
    CHECK(!bad.validate() && bad.validate().error().find("lower case") != std::string::npos);
    bad.levels.back().id = "";
    CHECK(!bad.validate());
    CHECK(!WorldLevelSet::fromJson(Json::parse("{\"list\":[{\"name\":\"x\"}]}").value()));
    CHECK(!WorldLevelSet::fromJson(
        Json::parse("{\"list\":[{\"id\":\"a\",\"kind\":\"Moon\"}]}").value()));
    CHECK(!WorldLevelSet::fromJson(Json::parse("{\"view\":\"Sideways\"}").value()));
    CHECK(!WorldLevelSet::fromJson(Json::parse("[]").value()));
    CHECK(!WorldLevelSet::fromJson(Json::parse("{\"belowAlpha\":3}").value()));
}

ComponentRegistry registry() {
    ComponentRegistry result;
    registerEngineComponents(result);
    return result;
}

void scenesCarryLevels() {
    ComponentRegistry reg = registry();
    Scene scene(reg, 1);
    // A scene with no levels writes exactly what it always wrote.
    CHECK(!sceneToJson(scene).get("settings").contains("levels"));
    scene.settings.levels.levels = {{"ground", "Ground", LevelKind::Floor, 0.0F},
                                    {"upstairs", "Upstairs", LevelKind::Floor, 3.0F}};
    Entity &hall = scene.createEntity("Hall");
    Entity &stair = scene.createEntity("Stair", hall.id());
    stair.add<WorldLayer>().level = "upstairs";
    const Json saved = sceneToJson(scene);
    CHECK(saved.get("settings").contains("levels"));
    auto loaded = sceneFromJson(saved, reg);
    CHECK(loaded);
    if (!loaded)
        return;
    CHECK(loaded.value()->settings.levels == scene.settings.levels);
    CHECK(sceneToJson(*loaded.value()) == saved);
    const Entity *loadedStair = loaded.value()->findByName("Stair");
    CHECK(levelOf(*loadedStair) == 1 && levelOf(*loaded.value()->findByName("Hall")) == 0);
    // A child with no WorldLayer of its own takes its parent's level.
    Entity &part = loaded.value()->createEntity("Part", loadedStair->id());
    CHECK(levelOf(part) == 1);
    part.add<WorldLayer>().level = "*";
    CHECK(levelOf(part) == everyLevel);
    part.get<WorldLayer>()->level = "nowhere"; // Unknown: the first level; validation reports it.
    CHECK(levelOf(part) == 0);
    // A scene with broken levels refuses to load, naming the setting.
    Json broken = saved;
    broken.find("settings")->find("levels")->find("list")->at(1).set("id", "ground");
    auto refused = sceneFromJson(broken, reg);
    CHECK(!refused && refused.error().find("settings.") != std::string::npos);
}

// ---- Levels and physics
// --------------------------------------------------------------------------
struct Crates {
    ComponentRegistry reg = registry();
    std::unique_ptr<Scene> scene = std::make_unique<Scene>(reg, 21);
    std::unique_ptr<GameRuntime> runtime;
    EntityId a, b;
    Crates(const char *levelA, const char *levelB) {
        scene->settings.levels.levels = {{"ground", "", LevelKind::Floor, 0.0F},
                                         {"upstairs", "", LevelKind::Floor, 3.0F}};
        Entity &floor = scene->createEntity("Floor");
        floor.transform().position = {0, 10};
        floor.add<RigidBody>().type = RigidBodyType::Static;
        floor.add<Collider>().size = {20, 1};
        floor.add<WorldLayer>().level = "*"; // The ground under every floor of this test.
        const auto crate = [&](const char *name, float y, const char *level) {
            Entity &entity = scene->createEntity(name);
            entity.transform().position = {0, y};
            entity.add<RigidBody>();
            entity.add<Collider>();
            entity.add<WorldLayer>().level = level;
            return entity.id();
        };
        b = crate("B", 9.0F, levelB);
        a = crate("A", 5.0F, levelA); // Dropped onto B.
        auto created = GameRuntime::create(std::move(scene), {});
        CHECK(created);
        runtime = std::move(created.value());
    }
    float y(EntityId who) const {
        return runtime->scene().find(who)->worldPosition().y;
    }
    void run(int ticks) {
        for (int i = 0; i < ticks; ++i)
            runtime->stepOnce(Keyboard{});
    }
};

void levelsSeparatePhysics() {
    // Same level: the dropped crate lands on the one below.
    Crates same("ground", "ground");
    same.run(180);
    CHECK_NEAR(same.y(same.b), 9.0, 0.06);
    CHECK_NEAR(same.y(same.a), 8.0, 0.08);
    // Different levels: it falls through to the floor next to it, at the same place.
    Crates apart("ground", "upstairs");
    apart.run(180);
    CHECK_NEAR(apart.y(apart.b), 9.0, 0.06);
    CHECK_NEAR(apart.y(apart.a), 9.0, 0.06);
    // Moving the lower one to the upper's level makes them meet: the overlap is pushed apart.
    CHECK(apart.runtime->scene().find(apart.a)->get<WorldLayer>()->moveTo(*apart.runtime,
                                                                          "upstairs"));
    apart.run(120);
    const Vec2 pa = apart.runtime->scene().find(apart.a)->worldPosition();
    const Vec2 pb = apart.runtime->scene().find(apart.b)->worldPosition();
    CHECK(distance(pa, pb) > 0.9F);
    CHECK(
        !apart.runtime->scene().find(apart.a)->get<WorldLayer>()->moveTo(*apart.runtime, "attic"));
    // The move was announced.
    bool announced = false;
    for (const GameEvent &event : apart.runtime->events().recent())
        if (event.name == "level_changed" && event.source == apart.a) {
            announced = event.data.get("from").asInt() == 0 && event.data.get("to").asInt() == 1;
        }
    CHECK(announced);
}

void levelsFilterQueries() {
    Crates crates("ground", "upstairs");
    crates.run(180);
    physics::World &world = crates.runtime->physics();
    const Vec2 from{0.0F, 3.0F}, to{0.0F, 12.0F};
    // A ray down the shaft hits whichever crate it meets first; limited to a level it skips the
    // other level's crate and goes on to the next thing on its own level.
    physics::QueryFilter any;
    const auto hitAny = world.rayCast(from, to - from, any).value();
    CHECK(hitAny);
    const auto bodyOfHit = [&](const std::optional<physics::RayHit> &hit) {
        return crates.runtime->entityOfShape(hit->shape);
    };
    CHECK(bodyOfHit(hitAny)->name() == "A" || bodyOfHit(hitAny)->name() == "B");
    physics::QueryFilter groundOnly;
    groundOnly.level = 0;
    const auto hitGround = world.rayCast(from, to - from, groundOnly).value();
    CHECK(hitGround && bodyOfHit(hitGround)->name() == "A");
    physics::QueryFilter upstairsOnly;
    upstairsOnly.level = 1;
    const auto hitUp = world.rayCast(from, to - from, upstairsOnly).value();
    CHECK(hitUp && bodyOfHit(hitUp)->name() == "B");
    // Shapes on every level (the floor) answer every level.
    const auto floorGround = world.queryAabb({{-1.0F, 9.9F}, {2.0F, 2.0F}}, groundOnly).value();
    const auto floorUp = world.queryAabb({{-1.0F, 9.9F}, {2.0F, 2.0F}}, upstairsOnly).value();
    CHECK(!floorGround.empty() && !floorUp.empty());
    bool floorFound = false;
    for (const auto &shape : floorUp)
        floorFound = floorFound || crates.runtime->entityOfShape(shape)->name() == "Floor";
    CHECK(floorFound);
    // The shape's level can be read back.
    const auto shapeLevel = world.level(hitGround->shape);
    CHECK(shapeLevel && shapeLevel.value() == 0);
}

void noLevelsNoCost() {
    // A scene that never mentions levels creates shapes on every level (no filter callback at all).
    ComponentRegistry reg = registry();
    auto scene = std::make_unique<Scene>(reg, 5);
    Entity &box = scene->createEntity("Box");
    box.add<RigidBody>();
    box.add<Collider>();
    const EntityId boxId = box.id();
    auto created = GameRuntime::create(std::move(scene), {});
    CHECK(created);
    auto &runtime = *created.value();
    const auto shapes = runtime.bodyShapes(boxId);
    CHECK(shapes.size() == 1);
    CHECK(runtime.physics().level(shapes[0]).value() == physics::allLevels);
}
} // namespace

int main() {
    spatialHash();
    spatialHashScales();
    levelSets();
    scenesCarryLevels();
    levelsSeparatePhysics();
    levelsFilterQueries();
    noLevelsNoCost();
    return yk::test::finish("world");
}
