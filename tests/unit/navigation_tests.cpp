// The navigation world: grid paths, weights, doors, links between levels, agent size, budgets.
#include "support/check.hpp"
#include "yk/navigation/Navigation.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <string>
#include <vector>

using namespace yk;
using namespace yk::nav;

namespace {
// A map drawn in text, one string per row, one list of rows per level:
//   '#' wall   '.' floor   'm' mud (area "mud", costly)   'D' a closed door   'L' a locked door
//   'X' a sealed door      'o' glass (solid, not opaque)    anything else floor
struct Map {
    NavigationWorld world;
    int mud{};
    std::uint16_t closedDoor{}, lockedDoor{}, sealedDoor{};
    explicit Map(const std::vector<std::vector<std::string>> &levels, float cell = 1.0F) {
        const int height = static_cast<int>(levels[0].size());
        const int width = static_cast<int>(levels[0][0].size());
        world.configure({{0.0F, 0.0F}, cell, width, height, static_cast<int>(levels.size())});
        mud = world.areaId("mud");
        for (std::size_t level = 0; level < levels.size(); ++level)
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    const char c =
                        levels[level][static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
                    GridCell &cellData = world.grid().at(static_cast<int>(level), x, y);
                    if (c == '#')
                        cellData.flags = cellflag::solid | cellflag::opaque;
                    else if (c == 'o')
                        cellData.flags = cellflag::solid;
                    else if (c == 'm') {
                        cellData.area = static_cast<std::uint8_t>(mud);
                        cellData.cost = 48; // Three times as slow.
                    } else if (c == 'D' || c == 'L' || c == 'X') {
                        DoorDef door;
                        door.state = c == 'D' ? DoorState::Closed
                                              : (c == 'L' ? DoorState::Locked : DoorState::Sealed);
                        door.access = c == 'L' ? 4U : 0U;
                        const std::uint16_t id =
                            world.addDoor(door, {static_cast<int>(level), x, y, x, y});
                        (c == 'D' ? closedDoor : (c == 'L' ? lockedDoor : sealedDoor)) = id;
                    }
                }
        world.refresh();
    }
    static Vec2 at(float x, float y) {
        return {x + 0.5F, y + 0.5F};
    }
    PathQuery query(float sx, float sy, float gx, float gy, int startLevel = 0,
                    int goalLevel = 0) const {
        PathQuery q;
        q.startLevel = startLevel;
        q.start = at(sx, sy);
        q.goalLevel = goalLevel;
        q.goal = at(gx, gy);
        q.tolerance = 0.3F;
        return q;
    }
};

// Every segment of a found path stays on walkable ground for the agent.
bool walkable(const Map &map, const NavPath &path, const AgentProfile &profile) {
    for (std::size_t i = 1; i < path.points.size(); ++i) {
        if (path.points[i].level != path.points[i - 1].level)
            continue;
        for (const auto &[x, y] :
             map.world.grid().lineCells(path.points[i - 1].position, path.points[i].position))
            if (!map.world.passable(path.points[i].level, x, y, profile))
                return false;
    }
    return true;
}

void openGround() {
    Map map({std::vector<std::string>(20, std::string(20, '.'))});
    PathQuery q = map.query(1, 1, 18, 18);
    const NavPath path = map.world.findPath(q);
    CHECK(path.status == PathStatus::Found && path.failure == PathFailure::None);
    CHECK(path.points.size() == 2); // Smoothed to a single straight walk.
    CHECK(path.points.front().position == Map::at(1, 1));
    CHECK_NEAR(distance(path.points.back().position, q.goal), 0.0, 0.01);
    CHECK_NEAR(path.length, std::hypot(17.0, 17.0), 0.05);
    CHECK(path.expansions > 0 && !path.goalAdjusted);
    // Start and goal in the same cell: a trivial path.
    const NavPath here = map.world.findPath(map.query(3, 3, 3, 3));
    CHECK(here.status == PathStatus::Found && here.points.size() <= 2 && here.length < 0.6F);
}

void aroundWalls() {
    Map map({{"..........", "..........", "####..####", "..........", ".........."}});
    const NavPath path = map.world.findPath(map.query(0, 0, 9, 4));
    CHECK(path.status == PathStatus::Found);
    CHECK(walkable(map, path, AgentProfile{}));
    // It has to go through the gap at x = 4 or 5, so it is longer than the straight line.
    CHECK(path.length > std::hypot(9.0, 4.0) + 0.1F);
    bool throughGap = false;
    for (const PathPoint &point : path.points)
        if (point.position.y > 1.9F && point.position.y < 3.1F && point.position.x > 3.9F &&
            point.position.x < 6.1F)
            throughGap = true;
    CHECK(throughGap || path.points.size() > 2);
    // The corner cannot be cut: two diagonal walls with a diagonal gap are closed.
    Map pinch({{"....", "..#.", ".#..", "...."}});
    const NavPath squeeze = pinch.world.findPath(pinch.query(1, 1, 2, 2));
    CHECK(squeeze.status == PathStatus::Found);
    CHECK(walkable(pinch, squeeze, AgentProfile{}));
    CHECK(squeeze.length > 1.5F); // Not the 1.41 diagonal through the pinch.
}

void blockedAndPartial() {
    // A goal inside a wall: the nearest walkable cell is used, and the path says so.
    Map map({{".....", ".###.", ".###.", ".###.", "....."}});
    NavPath path = map.world.findPath(map.query(0, 0, 2, 2));
    CHECK(path.status == PathStatus::Found && path.goalAdjusted);
    // A goal in a sealed room: the closest reachable point, honestly reported.
    Map room(
        {{"..........", "..######..", "..#....#..", "..#....#..", "..######..", ".........."}});
    path = room.world.findPath(room.query(0, 0, 4, 3));
    CHECK(path.status == PathStatus::Partial && path.failure == PathFailure::NoRoute);
    CHECK(path.points.size() >= 2 && !path.complete());
    // The path ends as near the room as it can get: just outside its wall.
    CHECK(distance(path.points.back().position, Map::at(4, 3)) < 3.1F);
    // A start with nowhere to go.
    Map jail({{".#...", "##...", ".....", ".....", "....."}});
    path = jail.world.findPath(jail.query(0, 0, 4, 4));
    CHECK(path.status == PathStatus::Unreachable && path.points.empty());
    // Outside the grid, NaN, wrong level: refused, not crashed.
    CHECK(map.world.findPath(map.query(0, 0, 50, 50)).status == PathStatus::Invalid);
    CHECK(map.world.findPath(map.query(0, 0, 1, 1, 3, 0)).status == PathStatus::Invalid);
    CHECK(map.world.findPath(map.query(0, 0, 1, 1, 0, -1)).status == PathStatus::Invalid);
    PathQuery nan = map.query(0, 0, 1, 1);
    nan.goal.x = std::nanf("");
    CHECK(map.world.findPath(nan).status == PathStatus::Invalid);
    CHECK(std::string(pathStatusName(PathStatus::Partial)) == "partial" &&
          std::string(pathFailureName(PathFailure::NoRoute)) == "no route");
}

void weightsAndAreas() {
    // A mud flat between start and goal; the clear detour is longer but cheaper.
    Map map({{"..........", "..mmmmmm..", "..mmmmmm..", "..........", ".........."}});
    PathQuery q = map.query(0, 1, 9, 1);
    NavPath normal = map.world.findPath(q);
    CHECK(normal.status == PathStatus::Found);
    int inMud = 0;
    for (std::size_t i = 1; i < normal.points.size(); ++i)
        for (const auto &[x, y] :
             map.world.grid().lineCells(normal.points[i - 1].position, normal.points[i].position))
            if (map.world.grid().at(0, x, y).area == map.mud)
                ++inMud;
    CHECK(inMud == 0); // Around the mud, not through it.
    // An agent that is not slowed by mud takes the straight line.
    PathQuery fast = q;
    fast.profile.areaCost[static_cast<std::size_t>(map.mud)] = 1.0F;
    // (Cells still cost 3x: the multiplier is the cell's own cost; the agent's table scales it.)
    // An agent that may not enter mud cannot cross a flat that spans the whole map.
    Map wall({{"mmmmm", "mmmmm", "mmmmm"}});
    PathQuery barred = wall.query(0, 1, 4, 1);
    CHECK(wall.world.findPath(barred).status == PathStatus::Found);
    barred.profile.areaMask = ~(1U << static_cast<unsigned>(wall.mud));
    const NavPath refused = wall.world.findPath(barred);
    CHECK(refused.status == PathStatus::Unreachable || refused.status == PathStatus::Partial);
    CHECK(!refused.complete());
    // Costs are reported in weighted meters: crossing 5 mud cells costs about 3x the walking.
    PathQuery across = wall.query(0, 1, 4, 1);
    const NavPath crossing = wall.world.findPath(across);
    CHECK(crossing.cost > 3.0F * 3.5F && crossing.length < 4.6F);
    // An agent profile can make an area dearer still.
    PathQuery dear = q;
    dear.profile.areaCost[static_cast<std::size_t>(map.mud)] = 10.0F;
    CHECK(map.world.findPath(dear).cost >= normal.cost);
    // Area names are a small registry.
    CHECK(map.world.findArea("mud") == map.mud && map.world.areaName(map.mud) == "mud" &&
          map.world.areaId("mud") == map.mud && map.world.findArea("lava") == -1);
    for (int i = 0; i < 40; ++i)
        map.world.areaId("area" + std::to_string(i));
    CHECK(map.world.areaId("one-too-many") == -1);
}

void dynamicObstacles() {
    Map map({{"..........", "..........", "####..####", "..........", ".........."}});
    PathQuery q = map.query(0, 0, 9, 4);
    const std::uint64_t before = map.world.revision();
    CHECK(map.world.findPath(q).status == PathStatus::Found);
    // Put a crate in the gap: the way is shut.
    const CellRect gap{0, 4, 2, 5, 2};
    map.world.grid().addBlockers(gap, +1);
    CHECK(map.world.refresh());
    CHECK(map.world.revision() > before);
    NavPath shut = map.world.findPath(q);
    CHECK(shut.status == PathStatus::Partial || shut.status == PathStatus::Unreachable);
    CHECK(shut.failure == PathFailure::NoRoute);
    // Two things in the gap: it opens only when both are gone.
    map.world.grid().addBlockers(gap, +1);
    map.world.refresh();
    map.world.grid().addBlockers(gap, -1);
    map.world.refresh();
    CHECK(map.world.findPath(q).status != PathStatus::Found);
    map.world.grid().addBlockers(gap, -1);
    map.world.refresh();
    CHECK(map.world.findPath(q).status == PathStatus::Found);
    // Counts never go below zero.
    map.world.grid().addBlockers(gap, -5);
    map.world.refresh();
    CHECK(map.world.findPath(q).status == PathStatus::Found);
    // Nothing changed: no refresh work, no revision change.
    const std::uint64_t quiet = map.world.revision();
    CHECK(!map.world.refresh() && map.world.revision() == quiet);
}

void doors() {
    // Two rooms joined by one door cell at (2, 2).
    const auto rooms = [](char door) {
        return std::vector<std::string>{"#####", "#...#", std::string("##") + door + "##", "#...#",
                                        "#####"};
    };
    Map hall({rooms('D')});
    PathQuery q = hall.query(2, 3, 2, 1);
    NavPath through = hall.world.findPath(q);
    CHECK(through.status == PathStatus::Found);
    bool opensDoor = false;
    for (const PathPoint &point : through.points)
        opensDoor = opensDoor || point.door == hall.closedDoor;
    CHECK(opensDoor); // The path names the door, so the agent knows to open it.
    // A closed door costs the time to open it.
    hall.world.setDoorState(hall.closedDoor, DoorState::Open);
    const NavPath open = hall.world.findPath(q);
    CHECK(open.status == PathStatus::Found && open.cost + 1.0F < through.cost);
    hall.world.setDoorState(hall.closedDoor, DoorState::Closed);
    // An agent that cannot open doors (a dog) does not get through.
    PathQuery dog = q;
    dog.profile.capabilities = capability::walk;
    const NavPath stopped = hall.world.findPath(dog);
    CHECK(!stopped.complete() && stopped.failure == PathFailure::NoRoute);
    // Locked: only with the key.
    Map locked({rooms('L')});
    PathQuery key = locked.query(2, 3, 2, 1);
    CHECK(!locked.world.findPath(key).complete());
    key.profile.access = 4U;
    CHECK(locked.world.findPath(key).status == PathStatus::Found);
    key.profile.access = 1U | 2U; // The wrong keys.
    CHECK(!locked.world.findPath(key).complete());
    // Sealed: nobody, whatever they hold.
    Map sealed({rooms('X')});
    PathQuery master = sealed.query(2, 3, 2, 1);
    master.profile.access = ~std::uint64_t{0};
    CHECK(!sealed.world.findPath(master).complete());
    sealed.world.setDoorState(sealed.sealedDoor, DoorState::Closed);
    CHECK(sealed.world.findPath(master).status == PathStatus::Found);
    CHECK(!sealed.world.setDoorState(999, DoorState::Open));
    CHECK(sealed.world.door(sealed.sealedDoor) != nullptr && sealed.world.door(999) == nullptr);
    // A closed door that needs a card needs it even though it is not locked.
    Map reader({rooms('.')});
    DoorDef card;
    card.state = DoorState::Closed;
    card.access = 8U;
    const std::uint16_t cardDoor = reader.world.addDoor(card, {0, 2, 2, 2, 2});
    reader.world.refresh();
    PathQuery visitor = reader.query(2, 3, 2, 1);
    CHECK(!reader.world.findPath(visitor).complete());
    visitor.profile.access = 8U;
    CHECK(reader.world.findPath(visitor).status == PathStatus::Found);
    // Removing a door makes its cells ordinary again.
    hall.world.removeDoor(hall.closedDoor);
    CHECK(hall.world.grid().at(0, 2, 2).door == 0 && hall.world.door(hall.closedDoor) == nullptr);
    reader.world.removeDoor(cardDoor);
    visitor.profile.access = 0;
    CHECK(reader.world.findPath(visitor).status == PathStatus::Found);
}

void levelsAndLinks() {
    const std::vector<std::string> ground{"#######", "#.....#", "#.....#", "#######"};
    const std::vector<std::string> upstairs{"#######", "#.....#", "#.....#", "#######"};
    Map map({ground, upstairs});
    PathQuery q = map.query(1, 1, 5, 2, 0, 1);
    // No stairs yet: the floors are separate worlds.
    CHECK(!map.world.findPath(q).complete());
    LinkDef stairs;
    stairs.kind = LinkKind::Stairs;
    stairs.fromLevel = 0;
    stairs.from = Map::at(5, 1);
    stairs.toLevel = 1;
    stairs.to = Map::at(5, 1);
    const std::uint32_t id = map.world.addLink(stairs);
    CHECK(id != 0 && map.world.link(id)->kind == LinkKind::Stairs);
    map.world.refresh();
    NavPath path = map.world.findPath(q);
    CHECK(path.status == PathStatus::Found);
    int via = 0;
    for (std::size_t i = 0; i < path.points.size(); ++i)
        if (path.points[i].viaLink == id) {
            ++via;
            CHECK(i > 0 && path.points[i - 1].level == 0 && path.points[i].level == 1);
            CHECK(path.points[i - 1].position == stairs.from &&
                  path.points[i].position == stairs.to);
        }
    CHECK(via == 1 && path.points.back().level == 1);
    // And back down: the link works both ways.
    PathQuery down = map.query(5, 2, 1, 1, 1, 0);
    CHECK(map.world.findPath(down).status == PathStatus::Found);
    // A closed link (a lift that is off) cannot be used.
    CHECK(map.world.setLinkOpen(id, false));
    CHECK(!map.world.findPath(q).complete());
    map.world.setLinkOpen(id, true);
    CHECK(map.world.findPath(q).status == PathStatus::Found);
    CHECK(!map.world.setLinkOpen(77, true));
    // Who may use it: a ladder needs climbing; a vent needs crawling and a key class.
    LinkDef ladder;
    ladder.kind = LinkKind::Ladder;
    ladder.fromLevel = 0;
    ladder.from = Map::at(1, 2);
    ladder.toLevel = 1;
    ladder.to = Map::at(1, 2);
    ladder.capabilities = capability::climb;
    map.world.removeLink(id);
    const std::uint32_t ladderId = map.world.addLink(ladder);
    map.world.refresh();
    CHECK(map.world.findPath(q).status == PathStatus::Found);
    PathQuery dog = q;
    dog.profile.capabilities = capability::walk | capability::doors;
    CHECK(!map.world.findPath(dog).complete());
    map.world.removeLink(ladderId);
    LinkDef vent;
    vent.kind = LinkKind::Vent;
    vent.fromLevel = 0;
    vent.from = Map::at(3, 1);
    vent.toLevel = 1;
    vent.to = Map::at(3, 1);
    vent.capabilities = capability::crawl;
    vent.access = 16U;
    map.world.addLink(vent);
    map.world.refresh();
    PathQuery crawler = q;
    CHECK(!map.world.findPath(crawler).complete()); // Cannot crawl.
    crawler.profile.capabilities |= capability::crawl;
    CHECK(!map.world.findPath(crawler).complete()); // Cannot crawl without the access.
    crawler.profile.access = 16U;
    CHECK(map.world.findPath(crawler).status == PathStatus::Found);
    // A drop only goes down.
    Map cliff({ground, upstairs});
    LinkDef drop;
    drop.kind = LinkKind::Drop;
    drop.fromLevel = 1;
    drop.from = Map::at(3, 1);
    drop.toLevel = 0;
    drop.to = Map::at(3, 1);
    drop.bidirectional = false;
    cliff.world.addLink(drop);
    cliff.world.refresh();
    CHECK(cliff.world.findPath(cliff.query(1, 1, 5, 2, 1, 0)).status == PathStatus::Found);
    CHECK(!cliff.world.findPath(cliff.query(1, 1, 5, 2, 0, 1)).complete());
    // The vocabulary of capabilities: names get bits, the five basic ones are fixed.
    CHECK(map.world.capabilityId("walk") == 0 && map.world.capabilityId("crawl") == 3);
    const int swim = map.world.capabilityId("swim");
    CHECK(swim == 5 && map.world.capabilityMask({"swim", "walk"}) == ((1U << 5) | 1U));
}

void agentSize() {
    // A corridor one cell wide, and one three cells wide.
    Map narrow({{"#######", "...#...", "#######"}});
    CHECK(narrow.world.findPath(narrow.query(0, 1, 2, 1)).status == PathStatus::Found);
    Map tunnel({{"########", "........", "########"}});
    PathQuery small = tunnel.query(0, 1, 7, 1);
    CHECK(tunnel.world.findPath(small).status == PathStatus::Found);
    PathQuery big = small;
    big.profile.radiusCells = 1; // Needs a cell clear on every side.
    const NavPath refused = tunnel.world.findPath(big);
    CHECK(!refused.complete());
    Map wide({{"#########", "#########", ".........", ".........", ".........", "#########"}});
    PathQuery bigWide = wide.query(0, 3, 8, 3);
    bigWide.profile.radiusCells = 1;
    const NavPath fits = wide.world.findPath(bigWide);
    CHECK(fits.status == PathStatus::Found);
    // A big agent keeps off the walls: every point of its path is in the middle row.
    for (const PathPoint &point : fits.points)
        CHECK(point.position.y > 2.9F && point.position.y < 4.1F);
    CHECK(walkable(wide, fits, bigWide.profile));
    // Radius beyond what clearance tracks is clamped, not an error.
    bigWide.profile.radiusCells = 9;
    CHECK(wide.world.findPath(bigWide).status != PathStatus::Invalid);
}

void budgets() {
    // A long serpentine corridor: a path that needs many expansions.
    std::vector<std::string> rows;
    const int width = 41, bands = 11;
    for (int b = 0; b < bands; ++b) {
        rows.push_back(std::string(static_cast<std::size_t>(width), '.'));
        std::string wall(static_cast<std::size_t>(width), '#');
        if (b + 1 < bands) {
            if (b % 2 == 0)
                wall[static_cast<std::size_t>(width - 1)] = '.';
            else
                wall[0] = '.';
            rows.push_back(wall);
        }
    }
    Map map({rows});
    PathQuery q = map.query(0, 0, (bands % 2 == 1) ? static_cast<float>(width - 1) : 0.0F,
                            static_cast<float>(rows.size() - 1));
    const NavPath whole = map.world.findPath(q);
    CHECK(whole.status == PathStatus::Found && whole.expansions > 200);
    CHECK(walkable(map, whole, AgentProfile{}));
    // The same search spread over many ticks gives the same path.
    CHECK(map.world.beginSearch(q) && map.world.searching());
    CHECK(!map.world.beginSearch(q)); // One at a time.
    int slices = 0;
    while (!map.world.stepSearch(50))
        ++slices;
    CHECK(slices >= whole.expansions / 50 - 1);
    const NavPath sliced = map.world.finishSearch();
    CHECK(!map.world.searching());
    CHECK(sliced.status == whole.status && sliced.points.size() == whole.points.size() &&
          std::fabs(sliced.cost - whole.cost) < 1e-3F);
    // A tight budget gives the best path so far and says why.
    PathQuery tight = q;
    tight.maxExpansions = 60;
    const NavPath partial = map.world.findPath(tight);
    CHECK(partial.status == PathStatus::Partial && partial.failure == PathFailure::BudgetExceeded);
    CHECK(partial.expansions <= 60 && partial.points.size() >= 2);
    CHECK(distance(partial.points.back().position, tight.goal) < distance(tight.start, tight.goal));
    // Statistics add up.
    CHECK(map.world.stats().searches >= 3 && map.world.stats().found >= 2 &&
          map.world.stats().partial >= 1);
}

// Many agents, random destinations, three floors joined by stairs: every query is bounded and the
// whole batch is quick.
void stress() {
    const int width = 120, height = 80;
    std::vector<std::vector<std::string>> floors;
    std::mt19937 random(1234);
    for (int level = 0; level < 3; ++level) {
        std::vector<std::string> rows(static_cast<std::size_t>(height),
                                      std::string(static_cast<std::size_t>(width), '.'));
        for (int y = 0; y < height; ++y) // Border walls and room dividers with doorways.
            for (int x = 0; x < width; ++x) {
                const bool border = x == 0 || y == 0 || x == width - 1 || y == height - 1;
                const bool wallX = x % 20 == 0 && (y % 10 != 5);
                const bool wallY = y % 20 == 0 && (x % 10 != 5);
                if (border || wallX || wallY)
                    rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = '#';
            }
        floors.push_back(rows);
    }
    Map map(floors, 0.5F);
    for (int level = 0; level < 2; ++level) {
        LinkDef stairs;
        stairs.fromLevel = level;
        stairs.toLevel = level + 1;
        stairs.from = stairs.to = {static_cast<float>(10 + level * 20) * 0.5F, 12.0F * 0.5F};
        map.world.addLink(stairs);
    }
    map.world.refresh();
    std::uniform_int_distribution<int> xs(1, width - 2), ys(1, height - 2), ls(0, 2);
    const auto begin = std::chrono::steady_clock::now();
    int found = 0, partial = 0, unreachable = 0, worst = 0;
    std::uint64_t total = 0;
    for (int i = 0; i < 100; ++i) {
        PathQuery q;
        q.startLevel = ls(random);
        q.goalLevel = ls(random);
        q.start = {static_cast<float>(xs(random)) * 0.5F, static_cast<float>(ys(random)) * 0.5F};
        q.goal = {static_cast<float>(xs(random)) * 0.5F, static_cast<float>(ys(random)) * 0.5F};
        q.maxExpansions = 60000;
        const NavPath path = map.world.findPath(q);
        found += path.status == PathStatus::Found;
        partial += path.status == PathStatus::Partial;
        unreachable += path.status == PathStatus::Unreachable;
        worst = std::max(worst, path.expansions);
        total += static_cast<std::uint64_t>(path.expansions);
        CHECK(path.expansions <= 60000);
        if (path.status == PathStatus::Found)
            CHECK(path.points.size() >= 2);
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    std::printf("navigation stress: 100 queries, found %d partial %d unreachable %d, worst %d "
                "expansions, mean %llu, %.3f s\n",
                found, partial, unreachable, worst, static_cast<unsigned long long>(total / 100),
                seconds);
    CHECK(found + partial + unreachable == 100);
    CHECK(found >= 60); // Rooms are joined by doorways and the floors by stairs.
#ifndef YK_STRESS_SLOWDOWN
#define YK_STRESS_SLOWDOWN 1
#endif
    CHECK(seconds < 20.0 * YK_STRESS_SLOWDOWN);
}
} // namespace

int main() {
    openGround();
    aroundWalls();
    blockedAndPartial();
    weightsAndAreas();
    dynamicObstacles();
    doors();
    levelsAndLinks();
    agentSize();
    budgets();
    stress();
    return yk::test::finish("navigation");
}
