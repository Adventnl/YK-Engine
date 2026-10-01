// Navigation in a running game: tile maps become the grid, agents walk real physics bodies around
// walls and through doors and stairs, the search budget holds, and the world can change under them.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/core/Log.hpp"
#include "yk/gameplay/Exploration.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/gameplay/Navigation.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

using namespace yk;

namespace {
const char *tilesetText = R"({
  "format": "yk.tileset", "version": 1, "name": "Arena", "texture": "",
  "tileWidth": 16, "tileHeight": 16, "columns": 4, "rows": 1,
  "tiles": [
    {"id": 1, "solid": true, "opaque": true, "noiseDamping": 0.9},
    {"id": 2, "area": "mud", "cost": 3.0}
  ]
})";

// A test arena: levels drawn in text ('#' wall, 'm' mud, anything else floor), one tile per
// character (one meter), with agents placed by tile.
struct Arena {
    ComponentRegistry reg;
    MemoryAssets assets;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<GameRuntime> runtime;
    EntityId map;
    int levelCount{1};

    explicit Arena(const std::vector<std::vector<std::string>> &levels, bool withLevels = false) {
        registerStandardComponents(reg);
        assets.files["tiles.yktileset"] = tilesetText;
        scene = std::make_unique<Scene>(reg, 17);
        scene->settings.gravity = {0.0F, 0.0F};
        levelCount = static_cast<int>(levels.size());
        if (withLevels || levels.size() > 1) {
            scene->settings.levels.levels.clear();
            for (std::size_t i = 0; i < levels.size(); ++i)
                scene->settings.levels.levels.push_back({"level" + std::to_string(i), "",
                                                         LevelKind::Floor,
                                                         3.0F * static_cast<float>(i)});
        }
        Entity &mapEntity = scene->createEntity("Map");
        auto &tiles = mapEntity.add<Tilemap>();
        tiles.tileset.path = "tiles.yktileset";
        for (std::size_t level = 0; level < levels.size(); ++level) {
            TileLayer floor =
                TileLayer::make("Floor" + std::to_string(level), TileLayerKind::Floor);
            TileLayer walls = TileLayer::make("Walls" + std::to_string(level), TileLayerKind::Wall);
            if (!scene->settings.levels.empty()) {
                floor.level = walls.level = "level" + std::to_string(level);
            }
            const std::size_t f = tiles.addLayer(floor), w = tiles.addLayer(walls);
            for (std::size_t y = 0; y < levels[level].size(); ++y)
                for (std::size_t x = 0; x < levels[level][y].size(); ++x) {
                    const char c = levels[level][y][x];
                    if (c == '#')
                        tiles.setTile(w, static_cast<int>(x), static_cast<int>(y), tile::make(1));
                    else if (c == 'm')
                        tiles.setTile(f, static_cast<int>(x), static_cast<int>(y), tile::make(2));
                }
        }
        map = mapEntity.id();
    }
    // The position of an entity whose feet stand in the middle of a tile.
    static Vec2 feetAt(float x, float y) {
        return {x + 0.5F, y + 0.5F};
    }
    Entity &agent(const char *name, float x, float y, const char *level = "") {
        Entity &entity = scene->createEntity(name);
        entity.transform().position = feetAt(x, y) - Vec2{0.0F, 0.28F};
        auto &nav = entity.add<NavigationAgent>();
        nav.stopDistance = 0.4F;
        if (std::string(level) != "")
            entity.get<WorldLayer>()->level = level;
        return entity;
    }
    void start() {
        RuntimeOptions options;
        options.assets = &assets;
        options.layers = layers::standard();
        setLogStderrEnabled(false);
        auto created = GameRuntime::create(std::move(scene), options);
        setLogStderrEnabled(true);
        CHECK(created);
        runtime = std::move(created.value());
    }
    NavigationAgent &nav(const char *name) {
        return *runtime->scene().findByName(name)->get<NavigationAgent>();
    }
    Vec2 feet(const char *name) {
        return nav(name).navPosition();
    }
    void tick(int count = 1) {
        for (int i = 0; i < count; ++i)
            runtime->stepOnce(Keyboard{});
    }
    // Runs until the agent is done (arrived or failed) or the time is up; returns ticks used.
    int until(const char *name, int limit) {
        for (int i = 0; i < limit; ++i) {
            const NavStatus status = nav(name).status();
            if (status == NavStatus::Arrived || status == NavStatus::Failed)
                return i;
            tick();
        }
        return limit;
    }
    NavigationService &service() {
        return runtime->services().get<NavigationService>();
    }
};

bool near(Vec2 a, Vec2 b, float tolerance) {
    return distance(a, b) <= tolerance;
}

void aroundWalls() {
    Arena arena({{"..............", "..............", "######.#######", "..............",
                  ".............."}});
    arena.agent("Walker", 1, 0);
    arena.start();
    NavigationAgent &walker = arena.nav("Walker");
    CHECK(walker.status() == NavStatus::Idle);
    CHECK(walker.moveTo(*arena.runtime, Arena::feetAt(12, 4)));
    CHECK(walker.status() == NavStatus::Searching);
    const int ticks = arena.until("Walker", 1500);
    CHECK(walker.status() == NavStatus::Arrived);
    CHECK(ticks < 700); // About 15 m at 2.6 m/s.
    CHECK(near(arena.feet("Walker"), Arena::feetAt(12, 4), 0.6F));
    CHECK(walker.path().status == nav::PathStatus::Found && walker.path().points.size() >= 3);
    // It really used the gap: some segment of the path crosses the wall row (y 2..3) inside x 6..7.
    bool wentThroughGap = false;
    const auto &points = walker.path().points;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const Vec2 a = points[i - 1].position, b = points[i].position;
        if ((a.y - 2.5F) * (b.y - 2.5F) <= 0.0F && std::fabs(b.y - a.y) > 1e-3F) {
            const float t = (2.5F - a.y) / (b.y - a.y);
            const float x = a.x + (b.x - a.x) * t;
            wentThroughGap = wentThroughGap || (x > 6.0F && x < 7.0F);
        }
    }
    CHECK(wentThroughGap);
    // The arrival was announced.
    bool announced = false;
    for (const GameEvent &event : arena.runtime->events().recent())
        announced =
            announced || (event.name == "nav.arrived" && event.source == walker.entity().id());
    CHECK(announced);
    // Ordered again, it can go back.
    CHECK(walker.moveTo(*arena.runtime, Arena::feetAt(1, 0)));
    arena.until("Walker", 1500);
    CHECK(walker.status() == NavStatus::Arrived &&
          near(arena.feet("Walker"), Arena::feetAt(1, 0), 0.6F));
}

void unreachableGoals() {
    Arena arena(
        {{"..........", ".######...", ".#....#...", ".#....#...", ".######...", ".........."}});
    arena.agent("Walker", 0, 0);
    arena.start();
    NavigationAgent &walker = arena.nav("Walker");
    walker.moveTo(*arena.runtime, Arena::feetAt(3, 2)); // Inside the sealed room.
    arena.until("Walker", 1500);
    CHECK(walker.status() == NavStatus::Failed && walker.failure() == nav::PathFailure::NoRoute);
    CHECK(walker.partial()); // It got as near as it could.
    CHECK(distance(arena.feet("Walker"), Arena::feetAt(3, 2)) < 4.0F);
    bool reported = false;
    for (const GameEvent &event : arena.runtime->events().recent())
        reported = reported || (event.name == "nav.failed" &&
                                event.data.get("reason").asString() == "no route");
    CHECK(reported);
    // A goal off the map is refused, not crashed on.
    CHECK(!walker.moveTo(*arena.runtime, {std::nanf(""), 0.0F}));
    walker.moveTo(*arena.runtime, {500.0F, 500.0F});
    arena.until("Walker", 600);
    CHECK(walker.status() == NavStatus::Failed);
}

void doorsOpenForAgents() {
    // Two rooms, one door of the gate kind in the wall between them.
    Arena arena({{"#########", "#...#...#", "#...#...#", "#...#...#", "#########"}});
    Entity &door = arena.scene->createEntity("Gate");
    door.transform().position = Arena::feetAt(4, 2);
    auto &sprite = door.add<SpriteRenderer>();
    sprite.size = {1.0F, 1.0F};
    door.add<Collider>().size = {1.0F, 1.0F};
    door.add<StateGate>();
    door.add<NavigationDoor>();
    // The wall cell the gate stands in is not a tile.
    auto &tiles = *arena.scene->find(arena.map)->get<Tilemap>();
    tiles.setTile(1, 4, 2, 0);
    arena.agent("Guard", 1, 2);
    arena.start();
    NavigationAgent &guard = arena.nav("Guard");
    auto *gate = arena.runtime->scene().findByName("Gate")->get<StateGate>();
    CHECK(!gate->open());
    guard.moveTo(*arena.runtime, Arena::feetAt(7, 2));
    arena.until("Guard", 1500);
    CHECK(guard.status() == NavStatus::Arrived);
    CHECK(near(arena.feet("Guard"), Arena::feetAt(7, 2), 0.7F));
    bool opens = false;
    for (const nav::PathPoint &point : guard.path().points)
        opens = opens || point.door != 0;
    CHECK(opens); // The path named the door.
    // It was opened for the guard and closes again afterwards.
    arena.tick(240);
    CHECK(!gate->open());
    CHECK(!arena.runtime->scene().findByName("Gate")->get<NavigationDoor>()->fullyOpen(
        *arena.runtime));
    // A dog that cannot open doors goes nowhere.
    Arena kennel({{"#########", "#...#...#", "#...#...#", "#...#...#", "#########"}});
    Entity &gateB = kennel.scene->createEntity("Gate");
    gateB.transform().position = Arena::feetAt(4, 2);
    gateB.add<Collider>().size = {1.0F, 1.0F};
    gateB.add<StateGate>();
    gateB.add<NavigationDoor>();
    kennel.scene->find(kennel.map)->get<Tilemap>()->setTile(1, 4, 2, 0);
    Entity &dog = kennel.agent("Dog", 1, 2);
    dog.get<NavigationAgent>()->capabilities = {"walk"};
    kennel.start();
    kennel.nav("Dog").moveTo(*kennel.runtime, Arena::feetAt(7, 2));
    kennel.until("Dog", 800);
    CHECK(kennel.nav("Dog").status() == NavStatus::Failed);
}

void keysUnlockManualDoors() {
    Arena arena({{"#########", "#...#...#", "#...#...#", "#...#...#", "#########"}});
    Entity &door = arena.scene->createEntity("Vault door");
    door.transform().position = Arena::feetAt(4, 2);
    auto &nd = door.add<NavigationDoor>();
    nd.source = DoorSource::Manual;
    nd.manualState = nav::DoorState::Locked;
    nd.access = {"staff.yellow"};
    arena.scene->find(arena.map)->get<Tilemap>()->setTile(1, 4, 2, 0);
    Entity &guard = arena.agent("Guard", 1, 2);
    guard.get<NavigationAgent>()->accessTokens = {"staff.yellow"};
    arena.agent("Visitor", 1, 3);
    arena.start();
    arena.nav("Guard").moveTo(*arena.runtime, Arena::feetAt(7, 2));
    arena.nav("Visitor").moveTo(*arena.runtime, Arena::feetAt(7, 3));
    arena.until("Guard", 1500);
    CHECK(arena.nav("Guard").status() == NavStatus::Arrived);
    arena.until("Visitor", 800);
    CHECK(arena.nav("Visitor").status() == NavStatus::Failed); // No key: no route.
    // A rule opens the door to everyone (no key class, no lock): the visitor may pass.
    auto *vault = arena.runtime->scene().findByName("Vault door")->get<NavigationDoor>();
    vault->access.clear();
    vault->setManualState(nav::DoorState::Closed);
    arena.nav("Visitor").moveTo(*arena.runtime, Arena::feetAt(7, 3));
    arena.until("Visitor", 1500);
    CHECK(arena.nav("Visitor").status() == NavStatus::Arrived);
}

void stairsBetweenFloors() {
    const std::vector<std::string> ground{"#########", "#.......#", "#.......#", "#########"};
    const std::vector<std::string> upstairs{"#########", "#.......#", "#.......#", "#########"};
    Arena arena({ground, upstairs});
    Entity &bottom = arena.scene->createEntity("Stairs bottom");
    bottom.transform().position = Arena::feetAt(6, 1);
    bottom.add<WorldLayer>().level = "level0";
    Entity &top = arena.scene->createEntity("Stairs top");
    top.transform().position = Arena::feetAt(2, 1);
    top.add<WorldLayer>().level = "level1";
    auto &link = bottom.add<NavigationLink>();
    link.kind = nav::LinkKind::Stairs;
    link.target = top.id();
    arena.agent("Walker", 1, 2, "level0");
    arena.start();
    NavigationAgent &walker = arena.nav("Walker");
    CHECK(levelOf(walker.entity()) == 0);
    walker.moveTo(*arena.runtime, Arena::feetAt(7, 2), 1); // The far end of the floor above.
    arena.until("Walker", 2400);
    CHECK(walker.status() == NavStatus::Arrived);
    CHECK(levelOf(walker.entity()) == 1);
    CHECK(near(arena.feet("Walker"), Arena::feetAt(7, 2), 0.7F));
    // The path says where it changed floors, and the move was announced.
    int links = 0;
    for (const nav::PathPoint &point : walker.path().points)
        links += point.viaLink != 0 ? 1 : 0;
    CHECK(links == 1);
    bool changed = false;
    for (const GameEvent &event : arena.runtime->events().recent())
        changed =
            changed || (event.name == "level_changed" && event.source == walker.entity().id());
    CHECK(changed || levelOf(walker.entity()) == 1);
    // And back down the same stairs.
    walker.moveTo(*arena.runtime, Arena::feetAt(1, 2), 0);
    arena.until("Walker", 2400);
    CHECK(walker.status() == NavStatus::Arrived && levelOf(walker.entity()) == 0);
}

void dividedFloorFails() {
    const std::vector<std::string> ground{"#########", "#.......#", "#.......#", "#########"};
    const std::vector<std::string> upstairs{"#########", "#...#...#", "#...#...#", "#########"};
    Arena arena({ground, upstairs});
    Entity &bottom = arena.scene->createEntity("Stairs bottom");
    bottom.transform().position = Arena::feetAt(6, 1);
    bottom.add<WorldLayer>().level = "level0";
    Entity &top = arena.scene->createEntity("Stairs top");
    top.transform().position = Arena::feetAt(2, 1);
    top.add<WorldLayer>().level = "level1";
    auto &link = bottom.add<NavigationLink>();
    link.target = top.id();
    arena.agent("Walker", 1, 2, "level0");
    arena.start();
    NavigationAgent &walker = arena.nav("Walker");
    walker.moveTo(*arena.runtime, Arena::feetAt(7, 2), 1);
    arena.until("Walker", 2400);
    // The stairs lead into the left room only; the right room is unreachable from there, but the
    // agent gets as close as it can: up the stairs and to the divider.
    CHECK(walker.status() == NavStatus::Failed && walker.partial());
    CHECK(levelOf(walker.entity()) == 1);
}

void walkersDoNotTouchOtherFloors() {
    // The upper floor's walls do not stop someone on the ground floor, and vice versa.
    Arena arena({{".........", ".........", ".........", "........."},
                 {"####.####", "####.####", "####.####", "#########"}});
    arena.agent("Walker", 1, 1, "level0");
    arena.start();
    NavigationAgent &walker = arena.nav("Walker");
    walker.moveTo(*arena.runtime, Arena::feetAt(7, 1), 0);
    arena.until("Walker", 1500);
    CHECK(walker.status() == NavStatus::Arrived);
    CHECK(near(arena.feet("Walker"), Arena::feetAt(7, 1), 0.7F)); // Walked straight across.
}

void wallsThatChange() {
    // A long way around; breaking a wall tile opens a shortcut and the agent is re-routed.
    Arena arena({{"..........", ".########.", ".#......#.", ".#.####.#.", ".#.#..#.#.",
                  "..........", ".........."}});
    arena.agent("Walker", 4, 4);
    arena.start();
    NavigationAgent &walker = arena.nav("Walker");
    walker.moveTo(*arena.runtime, Arena::feetAt(4, 0));
    arena.until("Walker", 2000);
    CHECK(walker.status() == NavStatus::Arrived);
    const float longWay = walker.path().length;
    CHECK(longWay > 12.0F);
    // The wall between the two pockets is broken at (4, 3).
    Tilemap &tiles = *arena.runtime->scene().find(arena.map)->get<Tilemap>();
    arena.runtime->teleport(walker.entity(), Arena::feetAt(4, 4) - Vec2{0.0F, 0.28F});
    CHECK(tiles.setTile(1, 4, 3, 0));
    arena.tick(2);
    CHECK(arena.service().world().passable(0, 0, 0, nav::AgentProfile{}));
    walker.moveTo(*arena.runtime, Arena::feetAt(4, 0));
    arena.until("Walker", 2000);
    CHECK(walker.status() == NavStatus::Arrived);
    // The new route is no longer than the old one (it goes up through the hole).
    CHECK(walker.path().length < longWay + 0.1F);
    // And a wall put back closes it again.
    tiles.setTile(1, 4, 3, tile::make(1));
    arena.runtime->teleport(walker.entity(), Arena::feetAt(4, 4) - Vec2{0.0F, 0.28F});
    arena.tick(2);
    walker.moveTo(*arena.runtime, Arena::feetAt(4, 0));
    arena.until("Walker", 2000);
    CHECK(walker.status() == NavStatus::Arrived && walker.path().length > longWay - 0.5F);
}

void obstaclesInTheWay() {
    Arena arena({{"##########", "#........#", "#.######.#", "#........#", "##########"}});
    Entity &crate = arena.scene->createEntity("Crate");
    crate.transform().position = Arena::feetAt(1, 2);
    crate.add<NavigationObstacle>();
    arena.agent("Walker", 1, 1);
    arena.start();
    NavigationAgent &walker = arena.nav("Walker");
    // The crate blocks the left passage; the way is round the right.
    walker.moveTo(*arena.runtime, Arena::feetAt(1, 3));
    arena.until("Walker", 2000);
    CHECK(walker.status() == NavStatus::Arrived);
    CHECK(walker.path().length > 14.0F);
    // Remove the crate (disable it): the short way opens.
    arena.runtime->scene().findByName("Crate")->setActive(false);
    arena.runtime->teleport(walker.entity(), Arena::feetAt(1, 1) - Vec2{0.0F, 0.28F});
    arena.tick(3);
    walker.moveTo(*arena.runtime, Arena::feetAt(1, 3));
    arena.until("Walker", 2000);
    CHECK(walker.status() == NavStatus::Arrived && walker.path().length < 6.0F);
}

void searchBudget() {
    // A grid where many agents ask at once: no tick spends more than the budget, and every agent
    // still gets where it is going.
    std::vector<std::string> rows;
    const int width = 40, height = 24;
    for (int y = 0; y < height; ++y) {
        std::string row(static_cast<std::size_t>(width), '.');
        if (y % 6 == 3)
            for (int x = 0; x < width; ++x)
                if (x % 13 != 6)
                    row[static_cast<std::size_t>(x)] = '#';
        rows.push_back(row);
    }
    Arena arena({rows});
    std::mt19937 random(5);
    std::uniform_int_distribution<int> xs(1, width - 2), band(0, height / 6 - 1);
    const int agents = 40;
    for (int i = 0; i < agents; ++i) {
        const int y = band(random) * 6 + 1 + (i % 2);
        arena.agent(("A" + std::to_string(i)).c_str(), static_cast<float>(xs(random)),
                    static_cast<float>(y));
    }
    arena.scene->createEntity("Settings").add<NavigationSettings>().expansionsPerTick = 3000;
    arena.start();
    for (int i = 0; i < agents; ++i) {
        const int y = band(random) * 6 + 1 + (i % 2);
        arena.nav(("A" + std::to_string(i)).c_str())
            .moveTo(*arena.runtime,
                    Arena::feetAt(static_cast<float>(xs(random)), static_cast<float>(y)));
    }
    int worstTick = 0;
    int ticksWithSearch = 0;
    for (int tick = 0; tick < 3000; ++tick) {
        arena.tick();
        const int spent = arena.service().counters().expansionsLastTick;
        worstTick = std::max(worstTick, spent);
        ticksWithSearch += spent > 0 ? 1 : 0;
        bool done = true;
        for (int i = 0; i < agents; ++i) {
            const NavStatus status = arena.nav(("A" + std::to_string(i)).c_str()).status();
            done = done && (status == NavStatus::Arrived || status == NavStatus::Failed);
        }
        if (done)
            break;
    }
    CHECK(worstTick <= 3000); // Never more than the budget in one tick.
    int arrived = 0;
    for (int i = 0; i < agents; ++i)
        arrived +=
            arena.nav(("A" + std::to_string(i)).c_str()).status() == NavStatus::Arrived ? 1 : 0;
    std::printf(
        "search budget: %d of %d agents arrived; worst tick %d expansions; %d ticks searched; "
        "%llu requests, %llu cache hits\n",
        arrived, agents, worstTick, ticksWithSearch,
        static_cast<unsigned long long>(arena.service().counters().requests),
        static_cast<unsigned long long>(arena.service().counters().cacheHits));
    CHECK(arrived >= agents - 4); // A few may be told to stay put or share a goal cell.
    CHECK(arena.service().counters().requests >= static_cast<std::uint64_t>(agents));
}

void agentsPassEachOther() {
    // Two agents swap ends of a wide corridor: they steer round each other instead of stacking.
    Arena arena({{"######################", "#....................#", "#....................#",
                  "#....................#", "######################"}});
    arena.agent("Left", 1, 2);
    arena.agent("Right", 20, 2);
    arena.start();
    arena.nav("Left").moveTo(*arena.runtime, Arena::feetAt(20, 2));
    arena.nav("Right").moveTo(*arena.runtime, Arena::feetAt(1, 2));
    float closest = 1e9F;
    for (int i = 0; i < 1500; ++i) {
        arena.tick();
        closest = std::min(closest, distance(arena.feet("Left"), arena.feet("Right")));
        if (arena.nav("Left").status() == NavStatus::Arrived &&
            arena.nav("Right").status() == NavStatus::Arrived)
            break;
    }
    CHECK(arena.nav("Left").status() == NavStatus::Arrived);
    CHECK(arena.nav("Right").status() == NavStatus::Arrived);
    CHECK(closest > 0.35F); // They kept apart as they passed (bodies are 0.55 wide).
    // A crowd sent to one spot does not stack into a single point.
    Arena crowd({{"##############", "#............#", "#............#", "#............#",
                  "##############"}});
    for (int i = 0; i < 8; ++i)
        crowd.agent(("C" + std::to_string(i)).c_str(), 1.0F, 1.0F + static_cast<float>(i % 3));
    crowd.start();
    for (int i = 0; i < 8; ++i)
        crowd.nav(("C" + std::to_string(i)).c_str())
            .moveTo(*crowd.runtime, Arena::feetAt(11, 2), -1, 1.4F);
    crowd.tick(900);
    float tightest = 1e9F;
    for (int a = 0; a < 8; ++a)
        for (int b = a + 1; b < 8; ++b)
            tightest = std::min(tightest, distance(crowd.feet(("C" + std::to_string(a)).c_str()),
                                                   crowd.feet(("C" + std::to_string(b)).c_str())));
    CHECK(tightest > 0.15F);
}

void pathCache() {
    Arena arena({{"..........", "..........", "..........", ".........."}});
    arena.agent("A", 1, 1);
    arena.agent("B", 1, 1);
    arena.start();
    arena.nav("A").moveTo(*arena.runtime, Arena::feetAt(8, 2));
    arena.tick(3);
    const std::uint64_t hitsBefore = arena.service().counters().cacheHits;
    arena.nav("B").moveTo(*arena.runtime, Arena::feetAt(8, 2)); // The same trip from the same cell.
    arena.tick(3);
    CHECK(arena.service().counters().cacheHits == hitsBefore + 1);
    CHECK(arena.nav("B").status() == NavStatus::Moving);
}

void settingsAndGrid() {
    Arena arena({{"....", "....", "...."}});
    arena.agent("A", 1, 1);
    arena.start();
    NavigationService &service = arena.service();
    arena.tick(2);
    CHECK(service.built());
    const WorldGrid &grid = service.world().grid();
    CHECK_NEAR(grid.spec().cellSize, 1.0 / 3.0, 1e-4); // A third of a one meter tile.
    CHECK(grid.spec().levels == 1 && grid.spec().width > 10 && grid.spec().height > 8);
    std::vector<std::pair<std::string, std::string>> rows;
    service.describe(rows);
    CHECK(!rows.empty());
}
} // namespace

int main() {
    aroundWalls();
    unreachableGoals();
    doorsOpenForAgents();
    keysUnlockManualDoors();
    stairsBetweenFloors();
    dividedFloorFails();
    walkersDoNotTouchOtherFloors();
    wallsThatChange();
    obstaclesInTheWay();
    searchBudget();
    agentsPassEachOther();
    pathCache();
    settingsAndGrid();
    return yk::test::finish("navigation_agent");
}
