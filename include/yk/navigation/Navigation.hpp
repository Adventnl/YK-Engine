#pragma once
#include "yk/core/Math.hpp"
#include "yk/scene/EntityId.hpp"
#include "yk/world/WorldGrid.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// Navigation over a WorldGrid that has several levels: weighted cells, doors with states and
// access, links between levels (stairs, ladders, vents, drops, lifts), agent size, per-agent
// permissions and costs, a resumable A* that can be given a few thousand node expansions per tick,
// and honest answers when the goal cannot be reached (a partial path to the closest point, and why).
// Nothing here knows a scene or a game: tile maps and components feed the grid, doors and links; this
// answers questions about paths.
namespace yk::nav {
inline constexpr int maxAreas = 32;
// Agents up to this many cells of radius beyond their own cell can be routed (clearance is kept to
// this distance from anything blocked).
inline constexpr int maxClearance = 4;

// What an agent can do besides walk; a link or a door may require some. Bits are assigned by name
// (World::capabilityId) so a game defines its own ("crawl", "swim"); the first few are fixed.
namespace capability {
inline constexpr std::uint32_t walk = 1U << 0;  // Moves over ordinary ground.
inline constexpr std::uint32_t doors = 1U << 1; // Opens doors it has access to.
inline constexpr std::uint32_t climb = 1U << 2; // Uses ladders and climb points.
inline constexpr std::uint32_t crawl = 1U << 3; // Fits through vents.
inline constexpr std::uint32_t drop = 1U << 4;  // Jumps down to a lower level.
inline constexpr std::uint32_t standard = walk | doors | climb | drop;
} // namespace capability

enum class DoorState : std::uint8_t {
    Open,   // Anyone passes.
    Closed, // Opens for an agent that can use doors (and holds the access, if any is required).
    Locked, // Passes only an agent holding the required access.
    Sealed  // Nobody passes (welded, wall-like).
};

struct DoorDef {
    DoorState state{DoorState::Closed};
    std::uint64_t access{0}; // Access classes (all of them) needed to pass; 0 needs none.
    float penalty{1.5F};     // Meters of extra path cost for a closed door (the time to open it).
    EntityId owner{};
};

enum class LinkKind { Stairs, Ladder, Vent, Hole, Elevator, Drop, Climb, Teleport };
const char *linkKindName(LinkKind kind);

// A connection between two points that may be on different levels: the way from one floor to the
// next. Agents path through it when they may, and walk (or climb, or ride) between its ends.
struct LinkDef {
    LinkKind kind{LinkKind::Stairs};
    int fromLevel{0};
    Vec2 from;
    int toLevel{0};
    Vec2 to;
    bool bidirectional{true}; // False: a drop or a one-way vent only goes from `from` to `to`.
    float cost{1.0F};          // Meters of path cost on top of the walk between the two points.
    std::uint32_t capabilities{0}; // Needed by the agent (all of them); 0 needs none.
    std::uint64_t access{0};       // Access classes needed; 0 needs none.
    bool open{true};               // A closed link (an elevator that is off) cannot be used.
    int capacity{1};               // Agents that may use it at once (door queues use this).
    EntityId owner{};
};

// Everything the path search needs to know about the agent asking.
struct AgentProfile {
    int radiusCells{0}; // Cells beyond the center cell the agent needs clear (0..maxClearance-1).
    std::uint32_t areaMask{0xFFFFFFFFU}; // Areas it may walk in.
    std::array<float, maxAreas> areaCost{}; // Cost multiplier per area (>= 1); zero means 1.
    std::uint32_t capabilities{capability::standard};
    std::uint64_t access{0}; // Access classes it holds.
    AgentProfile() {
        areaCost.fill(1.0F);
    }
    float costOf(int area) const {
        const float cost = areaCost[static_cast<std::size_t>(area)];
        return cost < 1.0F ? 1.0F : cost;
    }
    friend bool operator==(const AgentProfile &, const AgentProfile &) = default;
    // A cheap hash for the path cache.
    std::uint64_t hash() const;
};

struct PathQuery {
    int startLevel{0};
    Vec2 start;
    int goalLevel{0};
    Vec2 goal;
    float tolerance{0.4F}; // The search ends once within this distance (meters) of the goal.
    AgentProfile profile;
    int maxExpansions{40000}; // Nodes the whole search may expand; beyond it the best path so far.
};

enum class PathStatus {
    Found,       // Reaches the goal (within the tolerance).
    Partial,     // A path to the nearest point it could reach; the goal itself is not reached.
    Unreachable, // Nowhere to go (the start is enclosed or blocked); the path is empty.
    Invalid      // The query is outside the grid or has no levels.
};
enum class PathFailure { None, BudgetExceeded, NoRoute, GoalBlocked, StartBlocked, BadInput, Stuck };
const char *pathStatusName(PathStatus status);
const char *pathFailureName(PathFailure failure);

struct PathPoint {
    Vec2 position;
    int level{0};
    // Non-zero: the way from the previous point to this one is this link (the previous point is
    // its entry, this point its exit).
    std::uint32_t viaLink{0};
    // Non-zero: this point is a cell of that door (the agent opens it on the way through).
    std::uint16_t door{0};
};

struct NavPath {
    PathStatus status{PathStatus::Invalid};
    PathFailure failure{PathFailure::None};
    std::vector<PathPoint> points; // Start to end; the first is where the agent already is.
    float cost{0.0F};              // Search cost (meters, weighted) of the whole path.
    float length{0.0F};            // Straight-segment length in meters.
    int expansions{0};
    bool goalAdjusted{false}; // The goal cell was blocked; the nearest walkable one was used.
    bool complete() const {
        return status == PathStatus::Found;
    }
};

class NavigationWorld {
  public:
    NavigationWorld();

    // ---- The grid and what is derived from it -------------------------------------------------
    // (Re)sizes the grid, clearing every cell, door and link-independent state.
    void configure(const GridSpec &spec);
    WorldGrid &grid() {
        return grid_;
    }
    const WorldGrid &grid() const {
        return grid_;
    }
    // Recomputes clearance where cells changed since the last refresh. Call once per tick (cheap
    // when nothing changed). Returns true when something changed, so cached paths are stale.
    bool refresh();
    // Incremented whenever passability, doors or links changed (not by searches).
    std::uint64_t revision() const {
        return revision_;
    }

    // ---- Vocabulary ---------------------------------------------------------------------------
    // Navigation areas by name; 0 is "walkable". Returns -1 when the table of 32 is full.
    int areaId(const std::string &name);
    int findArea(const std::string &name) const;
    const std::string &areaName(int id) const;
    int capabilityId(const std::string &name); // Bit index 0..31 (the first five are fixed); -1 full.
    std::uint32_t capabilityMask(const std::vector<std::string> &names);

    // ---- Doors and links ----------------------------------------------------------------------
    // Registers a door covering the cells of `area`; returns its id (never 0).
    std::uint16_t addDoor(const DoorDef &door, const CellRect &area);
    bool setDoorState(std::uint16_t id, DoorState state);
    // Changes the access classes a door asks for (a security change revokes a key class).
    bool setDoorAccess(std::uint16_t id, std::uint64_t access);
    const DoorDef *door(std::uint16_t id) const;
    void removeDoor(std::uint16_t id);

    std::uint32_t addLink(const LinkDef &link); // Returns its id (never 0).
    bool setLinkOpen(std::uint32_t id, bool open);
    // Changes a link's ends and rules in place (a lift that moved); false for an unknown id.
    bool replaceLink(std::uint32_t id, const LinkDef &link);
    void removeLink(std::uint32_t id);
    const LinkDef *link(std::uint32_t id) const;
    const std::map<std::uint32_t, LinkDef> &links() const {
        return links_;
    }
    // The cell a link end lies in.
    void linkCell(int level, Vec2 point, int &x, int &y) const;

    // ---- Questions ----------------------------------------------------------------------------
    // Can this agent stand in the cell (size, area, door permission)?
    bool passable(int level, int x, int y, const AgentProfile &profile) const;
    // The nearest cell the agent can stand in, searching rings up to `maxRing` cells out.
    bool nearestPassable(int level, Vec2 around, int maxRing, const AgentProfile &profile,
                         Vec2 &found) const;
    // Does the straight walk between two points stay inside walkable cells of the same cost?
    bool segmentWalkable(int level, Vec2 from, Vec2 to, const AgentProfile &profile) const;
    bool linkUsable(const LinkDef &link, const AgentProfile &profile) const;

    // ---- Searching ----------------------------------------------------------------------------
    // Finds a path in one call (up to the query's budget).
    NavPath findPath(const PathQuery &query);
    // The same, in steps: start a search, spend a few expansions at a time, then take the result.
    // One search runs at a time; the service queues the others.
    bool beginSearch(const PathQuery &query);
    bool searching() const {
        return search_.active;
    }
    // Spends up to `budget` expansions; true when the search has finished.
    bool stepSearch(int budget);
    NavPath finishSearch();
    // Nodes the running search has expanded so far (the service measures what each tick spends).
    int activeExpansions() const {
        return search_.expansions;
    }

    struct Stats {
        std::uint64_t searches{};
        std::uint64_t expansions{};
        std::uint64_t found{}, partial{}, unreachable{};
        int lastExpansions{};
    };
    const Stats &stats() const {
        return stats_;
    }

  private:
    struct Node {
        float g{0};
        std::uint32_t parent{0};
        std::uint32_t link{0};
        std::uint32_t stamp{0}; // (search number << 1) | closed.
    };
    struct HeapEntry {
        float f;
        float g;
        std::uint32_t node;
    };
    struct Search {
        bool active{false};
        bool done{false};
        PathQuery query;
        NavPath result;
        std::uint32_t start{0}, goalNode{0};
        bool haveGoalNode{false};
        bool goalAdjusted{false};
        std::vector<HeapEntry> heap;
        std::uint32_t best{0};
        float bestH{0};
        int expansions{0};
        std::uint32_t terminal{0};
        bool reached{false};
        bool budgetExceeded{false};
    };

    std::uint32_t nodeIndex(int level, int x, int y) const {
        return static_cast<std::uint32_t>(grid_.index(level, x, y));
    }
    void decode(std::uint32_t node, int &level, int &x, int &y) const;
    float heuristic(const Search &search, int level, int x, int y) const;
    float cellCost(const GridCell &cell, const AgentProfile &profile) const;
    NavPath reconstruct(const Search &search, std::uint32_t terminal, bool complete);
    void smooth(const AgentProfile &profile, std::vector<PathPoint> &points) const;
    void rebuildClearance(int level, int minX, int minY, int maxX, int maxY);
    void rebuildLinkIndex();
    bool doorAllows(const DoorDef &door, const AgentProfile &profile) const;

    WorldGrid grid_;
    std::vector<std::uint8_t> clearance_; // Per cell: Chebyshev distance to a blocked cell, capped.
    std::vector<Node> nodes_;
    std::uint32_t searchNumber_{0};
    std::vector<std::string> areaNames_;
    std::vector<std::string> capabilityNames_;
    std::map<std::uint16_t, DoorDef> doors_;
    std::uint16_t nextDoor_{1};
    std::map<std::uint32_t, LinkDef> links_;
    std::uint32_t nextLink_{1};
    // Node index -> (link id, true when this end is the link's `from` end).
    std::unordered_map<std::uint32_t, std::vector<std::pair<std::uint32_t, bool>>> linkIndex_;
    bool linkIndexDirty_{true};
    std::uint64_t revision_{1};
    Search search_;
    Stats stats_;
};
} // namespace yk::nav
