#pragma once
#include "yk/core/Rng.hpp"
#include "yk/rules/Rules.hpp"
#include "yk/runtime/Services.hpp"
#include "yk/scene/Registry.hpp"
#include "yk/sim/Schedule.hpp"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// Places that mean something. A zone is an area of the world with a name, tags and a purpose ("the
// yard", "restricted", "dining"), a priority where areas overlap, and who may be in it; a room is a
// zone that is also a logical space a character can be sent to, owns, searches or is reported in.
// Nothing here knows what a prison is: the project's zones carry its words.
//
// Zones are looked up through the ZoneService, which keeps them in a spatial index (no scanning)
// and tells, a few times a second, which of the characters (entities with an Identity) is in which,
// and raises zone.entered / zone.exited (source: the zone's entity, other: the character; data:
// zone, name, room, allowed, actor) and zone.trespass when a character enters a zone it has no
// access to and the zone enforces it. What follows a trespass is the project's business: a rule, or
// the violation system.
namespace yk {
class GameContext;
class GameData;

enum class ZoneShape { Box, Circle, Polygon };
const std::vector<std::string> &zoneShapeNames(); // "box", "circle", "polygon"

class Zone final : public Component {
  public:
    ZoneShape shape{ZoneShape::Box};
    Vec2 size{4.0F, 4.0F}; // Box: the full extent; Circle: the width is the diameter. Before scale.
    Vec2 offset{};         // Where the shape is, in the entity's own space.
    Json polygon{Json::array()}; // Polygon corners as [[x, y], ...], in the entity's own space.
    std::string id;              // What schedules and rules call it; empty: the entity's name.
    std::string name;            // For labels and the map; empty: the id.
    bool room{false}; // A room is a zone that can be a destination, an owner's, searched.
    std::vector<std::string> tags;     // "restricted", "private", "cell"...
    std::vector<std::string> purposes; // What the space is for: "dining", "sleep", "work".
    int priority{0};                   // Where zones overlap, the highest is "the" zone there.
    int capacity{0};                   // How many can use it as a destination at once; 0: any.
    std::vector<std::string> allowedFactions; // Empty: no restriction by faction.
    std::vector<std::string> allowedRoles;    // Empty: no restriction by role.
    Json access;                              // A condition the character must meet; empty: none.
    bool countDisguise{true};                 // Faction checks see what the character looks like.
    std::string owner;                // Persistent id of who it belongs to; always allowed in.
    bool enforce{false};              // Entering without access raises zone.trespass.
    std::string trespassViolation;    // The violation a trespass is, for the violation system.
    Json environment{Json::object()}; // Flags other systems read ({"dark": true, "noisy": 0.5}).
    std::string navigationArea;       // The navigation area its cells belong to ("restricted").
    float navigationCost{1.0F};       // Path cost multiplier of its ground.
    static void describe(TypeBuilder<Zone> &type);

    void onStart(GameContext &context) override;
    void onDestroy(GameContext &context) override;

    // ---- What it is ----------------------------------------------------------------------------
    const std::string &zoneId() const; // id, or the entity's name
    const std::string &label() const;  // name, or the id
    bool hasTag(std::string_view tag) const;
    bool hasPurpose(std::string_view purpose) const;
    // The environment flag as a number (true is 1); `fallback` when it is not set.
    double environmentValue(std::string_view key, double fallback = 0.0) const;

    // ---- Where it is (world space) -------------------------------------------------------------
    bool contains(Vec2 world) const;
    Vec2 center() const;
    // A circle around the whole shape, from the entity's position.
    float boundingRadius() const;
    Rect bounds() const;
    // The outline (corners; a circle as 24 points), for drawing.
    std::vector<Vec2> outline() const;
    // A point inside it, drawn from `rng` (the center when it cannot find one).
    Vec2 pointInside(Rng &rng) const;
    int level() const;

    // ---- Who may be in it ----------------------------------------------------------------------
    // True when the character may be here: the owner always may; otherwise it must pass the faction
    // list, the role list and the condition. `why` says what stopped it.
    bool allows(GameContext &context, const Entity &actor, std::string *why = nullptr) const;
    bool hasCondition() const {
        return !access.isNull() && !(access.isObject() && access.size() == 0);
    }
    bool restricted() const {
        return !allowedFactions.empty() || !allowedRoles.empty() || hasCondition();
    }
    // The polygon's corners (empty for the other shapes, or when the list is not valid).
    const std::vector<Vec2> &corners() const;

  private:
    mutable Json conditionSource_;
    mutable Condition condition_;
    mutable bool conditionOk_{false};
    mutable Json cornersSource_;
    mutable std::vector<Vec2> corners_;
};

// Where zones are, and who is in them. One per running game.
class ZoneService final : public Service {
  public:
    const char *name() const override {
        return "zones";
    }
    UpdatePhase phase() const override {
        return UpdatePhase::PostSimulation;
    }
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onShutdown(GameContext &context) override;
    void describe(std::vector<std::pair<std::string, std::string>> &rows) const override;

    // Zones register themselves (Zone::onStart / onDestroy).
    void add(GameContext &context, const Zone &zone);
    void remove(GameContext &context, EntityId zone);

    // ---- Where --------------------------------------------------------------------------------
    // The zones that contain the point on that level, the highest priority first (then the
    // smaller).
    std::vector<Zone *> at(GameContext &context, Vec2 point, int level = 0) const;
    Zone *zoneAt(GameContext &context, Vec2 point, int level = 0) const;
    // The room at the point: the highest-priority zone there that is a room.
    Zone *roomAt(GameContext &context, Vec2 point, int level = 0) const;

    // ---- What ---------------------------------------------------------------------------------
    // By id (a zone's or a room's).
    Zone *find(GameContext &context, std::string_view id) const;
    std::vector<Zone *> all(GameContext &context) const;
    std::vector<Zone *> rooms(GameContext &context) const;
    std::vector<Zone *> withTag(GameContext &context, std::string_view tag) const;
    std::vector<Zone *> withPurpose(GameContext &context, std::string_view purpose) const;

    // ---- Who ----------------------------------------------------------------------------------
    // The characters inside a zone as of the last look, and the zones a character is in.
    std::vector<EntityId> occupants(EntityId zone) const;
    std::vector<Zone *> zonesOf(GameContext &context, EntityId actor) const;
    // Looks at every character now (the service otherwise looks at a few each tick).
    void refresh(GameContext &context);
    // A character is gone: it leaves every zone it was in.
    void forget(GameContext &context, EntityId actor);
    // Changes when a zone is added or removed (the navigation grid follows).
    std::uint64_t revision() const {
        return revision_;
    }

    // ---- Destinations -------------------------------------------------------------------------
    struct Place {
        Vec2 point{};
        int level{0};
        EntityId zone{}; // The zone it is in, if the destination named one
    };
    // Where a schedule's destination is for this character: a zone, a room (the nearest with free
    // room when several match a purpose), an entity, a point, the character's home. None when
    // nothing matches.
    std::optional<Place> destinationFor(GameContext &context,
                                        const ScheduleDestination &destination, const Entity &actor,
                                        Rng &rng) const;
    // Whether the character is at the destination now.
    bool isAt(GameContext &context, const ScheduleDestination &destination,
              const Entity &actor) const;

  private:
    struct Record {
        EntityId id;
        std::string zoneId;
    };
    void look(GameContext &context, EntityId actor);
    std::vector<Record> records_;
    std::map<EntityId, std::vector<EntityId>> occupants_;        // zone -> characters
    std::unordered_map<EntityId, std::vector<EntityId>> inside_; // character -> zones
    std::size_t cursor_{0};
    float carry_{0.0F};
    std::uint64_t revision_{0};
};

void registerZoneRules(RuleCatalog &catalog);
void registerZoneComponents(ComponentRegistry &registry);
} // namespace yk
