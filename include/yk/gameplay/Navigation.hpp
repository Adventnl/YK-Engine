#pragma once
#include "yk/gameplay/Character.hpp"
#include "yk/navigation/Navigation.hpp"
#include "yk/runtime/Services.hpp"
#include "yk/world/SpatialHash.hpp"
#include "yk/world/Tilemap.hpp"
#include "yk/world/WorldLevels.hpp"
#include <deque>
#include <unordered_map>

namespace yk {
class NavigationAgent;
class NavigationDoor;

// Scene-wide navigation settings. Optional: without it the grid covers every tile map at a third of
// the smallest tile's size, and a few thousand node expansions of path search are spent per tick.
class NavigationSettings final : public Component {
  public:
    float cellSize{0.0F}; // Meters; 0 derives it (a third of the smallest tile map cell, else 0.5).
    Vec2 boundsMin{}, boundsMax{}; // Explicit grid bounds in world space; equal values derive them.
    float padding{2.0F};           // Meters added around what the tile maps and obstacles cover.
    int expansionsPerTick{6000};   // Path search budget for all agents together, per fixed tick.
    std::vector<std::string> areas; // Navigation areas to register first ("mud", "restricted").
    static void describe(TypeBuilder<NavigationSettings> &type);
};

// Something that stands in the way of walkers (a crate, a counter) and/or of sight, as a footprint
// on the navigation grid. Static obstacles are placed once; `dynamic` ones follow their entity.
class NavigationObstacle final : public Component {
  public:
    Vec2 size{1.0F, 1.0F}; // World units, before the entity's scale.
    Vec2 offset{};
    bool blocksMovement{true};
    bool blocksSight{false};
    bool dynamic{false};
    static void describe(TypeBuilder<NavigationObstacle> &type);
    void onStart(GameContext &context) override;
    void onDestroy(GameContext &context) override;
};

enum class DoorSource { StateGate, Mechanism, Manual };
const std::vector<std::string> &doorSourceNames();
const std::vector<std::string> &doorStateNames();

// A door as the navigation grid knows it: the cells it covers, whether it is open, closed, locked
// or sealed, and which access classes pass it. The state is read from the entity's StateGate or
// Door mechanism, or held by the component itself (Manual: scripts, rules and security set it).
// Agents that may use the door open it on the way through; it closes again after
// `autoCloseSeconds`.
class NavigationDoor final : public Component {
  public:
    Vec2 size{1.0F, 1.0F};
    Vec2 offset{};
    DoorSource source{DoorSource::StateGate};
    nav::DoorState manualState{nav::DoorState::Closed};
    std::vector<std::string> access; // Access classes (all of them) needed to pass; none: anyone.
    float openPenalty{1.5F};         // Path cost of waiting for it to open, in meters.
    bool blocksSightWhenClosed{true};
    float autoCloseSeconds{2.0F}; // After an agent opened it; 0 leaves it open.
    static void describe(TypeBuilder<NavigationDoor> &type);
    void onStart(GameContext &context) override;
    void onDestroy(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;

    nav::DoorState state(GameContext &context) const;
    // Asks the door to open for `agent`: true when it is (or becomes) open. Locked or sealed doors
    // refuse; the caller already knows (the path only goes through doors the agent may use).
    // `accessHeld` is what the agent holds, `accessRequired` what the door needs: a key holder
    // unlocks a Manual door.
    bool open(GameContext &context, EntityId agent, std::uint64_t accessHeld = 0,
              std::uint64_t accessRequired = 0);
    // Open and no longer in the way (a gate has finished moving aside).
    bool fullyOpen(GameContext &context) const;
    void setManualState(nav::DoorState state) {
        manualState = state;
    }
    std::uint16_t navId() const {
        return navId_;
    }

  private:
    friend class NavigationService;
    std::uint16_t navId_{0};
    float openFor_{0.0F}; // Time left before an opened door closes again.
    bool openedByAgent_{false};
};

// A way between two points, usually on different levels: stairs, a ladder, a vent, a hole, a drop,
// a lift. This entity is one end; `target` names the other.
class NavigationLink final : public Component {
  public:
    nav::LinkKind kind{nav::LinkKind::Stairs};
    EntityRef target;
    bool bidirectional{true};
    float cost{1.0F};
    float traverseSeconds{0.0F};           // 0: a sensible time for the kind.
    std::vector<std::string> capabilities; // What an agent must be able to do ("climb", "crawl").
    std::vector<std::string> access;       // Access classes it must hold.
    bool open{true};
    int capacity{1};
    static void describe(TypeBuilder<NavigationLink> &type);
    void onStart(GameContext &context) override;
    void onDestroy(GameContext &context) override;
    float seconds() const;

  private:
    friend class NavigationService;
    std::uint32_t navId_{0};
};

enum class NavStatus { Idle, Searching, Moving, Arrived, Failed };
const char *navStatusName(NavStatus status);

// Walks an entity to wherever it is told to go: asks the navigation service for a path (a few
// thousand node expansions at a time, never a whole search in one tick), follows it, opens the
// doors on the way, takes stairs and vents, keeps clear of other agents, and reports arrival or why
// it could not. It steers through the entity's CharacterMotor.
class NavigationAgent final : public Component {
  public:
    float radius{0.28F};          // Body radius in meters (decides which gaps it fits through).
    Vec2 footOffset{0.0F, 0.28F}; // Where the feet are, from the entity's position.
    float arriveDistance{0.22F};  // How close counts as reaching a waypoint.
    float stopDistance{0.35F};    // How close to the destination counts as arriving.
    float repathInterval{0.7F};   // Least seconds between path requests when the way is blocked.
    float avoidanceRadius{0.9F};  // Neighbors closer than this are steered around.
    float avoidanceStrength{1.0F};
    bool run{false};
    std::vector<std::string> capabilities{"walk", "doors", "climb", "drop"};
    std::vector<std::string> forbiddenAreas; // Navigation areas it will not enter.
    std::vector<std::string> areaCosts;      // "name=multiplier" (mud=2): dearer or cheaper ground.
    std::vector<std::string> accessTokens;   // Access classes it holds.
    static void describe(TypeBuilder<NavigationAgent> &type);

    // ---- Orders --------------------------------------------------------------------------------
    // Go to a point on `level` (-1: the level it is on). False when there is nothing to path on.
    bool moveTo(GameContext &context, Vec2 goal, int level = -1, float tolerance = -1.0F);
    bool moveToEntity(GameContext &context, EntityId target, float tolerance = -1.0F);
    void stop(GameContext &context);

    // ---- Reports -------------------------------------------------------------------------------
    NavStatus status() const {
        return status_;
    }
    nav::PathFailure failure() const {
        return failure_;
    }
    const nav::NavPath &path() const {
        return path_;
    }
    std::size_t pathIndex() const {
        return index_;
    }
    Vec2 destination() const {
        return goal_;
    }
    int destinationLevel() const {
        return goalLevel_;
    }
    bool partial() const {
        return path_.status == nav::PathStatus::Partial;
    }
    // Where it stands, as the navigation grid sees it.
    Vec2 navPosition() const;
    // Seconds spent waiting (a door, a queue, a lift) in the current trip.
    float waited() const {
        return waited_;
    }
    int repaths() const {
        return repaths_;
    }
    nav::AgentProfile profile(GameContext &context) const;

    void onStart(GameContext &context) override;
    void onDestroy(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    void requestPath(GameContext &context);
    void fail(GameContext &context, nav::PathFailure reason);
    void finish(GameContext &context);
    Vec2 avoidance(GameContext &context, Vec2 position, Vec2 heading) const;

    NavStatus status_{NavStatus::Idle};
    nav::PathFailure failure_{nav::PathFailure::None};
    nav::NavPath path_;
    std::size_t index_{1};
    Vec2 goal_{};
    int goalLevel_{0};
    float tolerance_{0.35F};
    EntityId followTarget_{};
    std::uint64_t request_{0};
    float sinceRequest_{100.0F};
    float waited_{0.0F};
    float waitingOnDoor_{0.0F};
    int repaths_{0};
    std::uint64_t pathRevision_{0};
    // Stuck detection.
    Vec2 progressPoint_{};
    float progressTimer_{0.0F};
    int stuck_{0};
    // A link being used.
    std::uint32_t linkInUse_{0};
    float linkLeft_{0.0F};
    Vec2 linkExit_{};
    int linkExitLevel_{0};
    std::uint32_t pendingLink_{0}; // The link the next waypoint is reached through.
    friend class NavigationService;
};

// Builds and keeps the navigation world of the running scene: the grid from the tile maps (solid
// tiles, ground kinds, sound damping), obstacles, doors and links from their components, and the
// queue of path requests, answered a little each tick. Components reach it through
// GameContext::services().
class NavigationService final : public Service {
  public:
    using RequestId = std::uint64_t;
    const char *name() const override {
        return "navigation";
    }
    // Runs before the components of the Steering phase, so agents see this tick's answers.
    UpdatePhase phase() const override {
        return UpdatePhase::Steering;
    }
    void onFixedUpdate(GameContext &context, float seconds) override;
    void describe(std::vector<std::pair<std::string, std::string>> &rows) const override;

    nav::NavigationWorld &world() {
        return world_;
    }
    const nav::NavigationWorld &world() const {
        return world_;
    }
    // The grid is built from the scene on first use; call after changing what it is built from in
    // bulk (a scene loaded around it).
    void rebuild(GameContext &context);
    // Builds the grid now when it has not been (an agent asking before the service's first tick).
    void ensureBuilt(GameContext &context);
    bool built() const {
        return built_;
    }

    // ---- Path requests -------------------------------------------------------------------------
    RequestId requestPath(EntityId requester, const nav::PathQuery &query);
    // The finished path, once (true when it was ready); a request that has not finished is left.
    bool take(RequestId id, nav::NavPath &path);
    void cancel(RequestId id);
    std::size_t queued() const {
        return queue_.size();
    }

    // ---- Vocabulary shared with agents and doors
    // ------------------------------------------------- Access classes by name; a door and an agent
    // holding the same name agree. 64 at most.
    std::uint64_t accessMask(const std::vector<std::string> &names);
    int accessBit(const std::string &name);

    // ---- Agents (for avoidance)
    // ------------------------------------------------------------------
    void publishAgent(EntityId id, Vec2 position, Vec2 heading, int level);
    void forgetAgent(EntityId id);
    struct Neighbor {
        EntityId id;
        Vec2 position;
        Vec2 heading;
    };
    std::vector<Neighbor> neighbors(Vec2 position, float radius, int level, EntityId except) const;

    // ---- Doors and links in use
    // -------------------------------------------------------------------
    NavigationDoor *doorById(GameContext &context, std::uint16_t navId);
    // Door passage: one direction at a time. True when the agent may go now.
    bool claimDoor(std::uint16_t navId, EntityId agent, Vec2 direction, double now);
    void releaseDoor(std::uint16_t navId, EntityId agent);
    bool claimLink(std::uint32_t linkId, EntityId agent);
    void releaseLink(std::uint32_t linkId, EntityId agent);

    struct Counters {
        std::uint64_t requests{}, cacheHits{}, searches{};
        int expansionsLastTick{};
        int queueHighWater{};
    };
    const Counters &counters() const {
        return counters_;
    }

  private:
    struct ObstacleRecord {
        EntityId entity;
        CellRect cells{};
        bool applied{false};
        bool movement{false}, sight{false};
    };
    struct DoorRecord {
        EntityId entity;
        std::uint16_t navId{0};
        nav::DoorState applied{nav::DoorState::Closed};
        bool sightApplied{false};
        CellRect cells{};
    };
    struct LinkRecord {
        EntityId entity;
        std::uint32_t navId{0};
    };
    struct TilemapRecord {
        EntityId entity;
        std::uint64_t sequence{};
    };
    struct Request {
        RequestId id{};
        EntityId requester;
        nav::PathQuery query;
        std::uint64_t cacheKey{};
        bool cancelled{false};
    };
    struct DoorUse {
        EntityId agent;
        Vec2 direction;
        double since{};
    };
    friend class NavigationObstacle;
    friend class NavigationDoor;
    friend class NavigationLink;

    void rebuildStatic(GameContext &context);
    void recomputeCells(GameContext &context, int level, int minX, int minY, int maxX, int maxY);
    void syncTilemaps(GameContext &context);
    void syncObstacles(GameContext &context);
    void syncDoors(GameContext &context);
    void syncLinks(GameContext &context);
    CellRect footprint(const Entity &entity, Vec2 size, Vec2 offset) const;
    std::uint64_t cacheKey(const nav::PathQuery &query) const;

    nav::NavigationWorld world_;
    bool built_{false};
    std::uint64_t zoneRevision_{0};
    int expansionsPerTick_{6000};
    std::vector<ObstacleRecord> obstacles_;
    std::vector<DoorRecord> doors_;
    std::vector<LinkRecord> links_;
    std::vector<TilemapRecord> tilemaps_;
    std::vector<std::string> accessNames_;
    SpatialHash agents_{3.0F};
    std::unordered_map<EntityId, std::pair<Vec2, Vec2>> agentState_; // position, heading
    std::deque<Request> queue_;
    Request activeRequest_;
    std::unordered_map<RequestId, nav::NavPath> finished_;
    RequestId nextRequest_{1};
    std::unordered_map<std::uint64_t, nav::NavPath> cache_;
    std::uint64_t cacheRevision_{0};
    std::map<std::uint16_t, DoorUse> doorUse_;
    std::map<std::uint32_t, std::vector<EntityId>> linkUse_;
    Counters counters_;
    double clock_{0.0};
};

void registerNavigationComponents(ComponentRegistry &registry);
} // namespace yk
