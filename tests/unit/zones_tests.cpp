// Zones and rooms: the shapes and where a point is, the service that finds zones and tells who is
// in which (events, trespass, access by faction, role, disguise and condition), where a schedule's
// destinations lead, the rule facts and predicates, and what the validator says.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Project.hpp"
#include "yk/assets/Validation.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include "yk/sim/Clock.hpp"
#include "yk/sim/Identity.hpp"
#include "yk/sim/Schedule.hpp"
#include "yk/sim/Zones.hpp"
#include "yk/stats/Stats.hpp"
#include "yk/world/WorldLevels.hpp"
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

const char *dataText = R"({
  "format": "yk.data", "version": 1,
  "stats": [{"id": "health", "max": 100}],
  "effects": [{"id": "uniform", "flags": ["disguise.guards"]}],
  "factions": [{"id": "guards"}, {"id": "inmates"}],
  "schedules": [
    {"id": "routine", "blocks": [
      {"id": "assembly", "from": "07:00", "to": "08:00", "activity": "Assembly",
       "destination": {"zone": "yard"}, "tolerance": 5,
       "requires": {"enter": "zone:yard", "stay": 5}},
      {"id": "chores", "from": "08:00", "to": "09:00", "activity": "Chores",
       "destination": {"purpose": "work"}, "requires": {"action": "chore_done"}},
      {"id": "gym", "from": "09:00", "to": "10:00", "activity": "Gym",
       "destination": {"room": "gym"}, "requires": {"enter": "room:gym"}},
      {"id": "visit", "from": "10:00", "to": "11:00", "activity": "Visit",
       "destination": {"point": [0, 0]}},
      {"id": "lights", "from": "22:00", "to": "06:00", "activity": "Lights out",
       "destination": "home", "requires": {"enter": "room:cell"}}]}
  ]
})";

struct Rig {
    ComponentRegistry registry;
    MemoryAssets assets;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<GameRuntime> runtime;
    std::vector<GameEvent> heard;

    Rig() {
        registerEngineComponents(registry);
        assets.files["data/world.ykdata"] = dataText;
        scene = std::make_unique<Scene>(registry, 17);
        scene->settings.levels.levels.clear();
        scene->settings.levels.levels.push_back({"ground", "", LevelKind::Floor, 0.0F});
        scene->settings.levels.levels.push_back({"upper", "", LevelKind::Floor, 3.0F});
    }
    Zone &zone(const char *name, Vec2 at, Vec2 size, const char *level = "") {
        Entity &entity = scene->createEntity(name);
        entity.setWorldPosition(at);
        auto &z = entity.add<Zone>();
        z.size = size;
        if (std::string(level) != "")
            entity.add<WorldLayer>().level = level;
        return z;
    }
    Entity &actor(const char *name, const char *id, const char *faction, const char *role, Vec2 at,
                  const char *level = "") {
        Entity &entity = scene->createEntity(name);
        entity.setWorldPosition(at);
        auto &who = entity.add<Identity>();
        who.id = id;
        who.faction = faction;
        who.role = role;
        if (std::string(level) != "")
            entity.add<WorldLayer>().level = level;
        return entity;
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
        for (int i = 0; i < ticks; ++i)
            runtime->stepOnce(Keyboard{});
    }
    ZoneService &zones() {
        return runtime->services().get<ZoneService>();
    }
    // A clock for the scene: `scale` minutes of the world per second, starting at `time`.
    void clock(float scale, const char *time) {
        auto &settings = scene->createEntity("Clock").add<ClockSettings>();
        settings.minutesPerSecond = scale;
        settings.startTime = time;
    }
    // Steps until the clock reads `time` (and then once more).
    void until(const char *time, int limit = 20000) {
        const int wanted = parseTimeOfDay(time).value();
        for (int i = 0; i < limit && runtime->services().get<WorldClock>().minutesOfDay() != wanted;
             ++i)
            step();
        step();
    }
    Entity &entity(const char *name) {
        return *runtime->scene().findByName(name);
    }
    Zone &zoneOf(const char *name) {
        return *entity(name).get<Zone>();
    }
    // Moves an actor and lets the service look.
    void put(const char *name, Vec2 at) {
        entity(name).setWorldPosition(at);
        zones().refresh(*runtime);
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

std::vector<std::string> names(const std::vector<Zone *> &list) {
    std::vector<std::string> found;
    for (const Zone *zone : list)
        found.push_back(zone->zoneId());
    return found;
}

// ---- Shapes
// -------------------------------------------------------------------------------------------
void shapes() {
    Rig rig;
    // A box with an offset, rotated a quarter turn and stretched: x 2 wide to 4 wide in the world.
    Zone &box = rig.zone("Box", {10.0F, 10.0F}, {4.0F, 2.0F});
    box.offset = {1.0F, 0.0F};
    Entity &boxEntity = box.entity();
    boxEntity.transform().rotationDegrees = 90.0F;
    boxEntity.transform().scale = {2.0F, 1.0F};
    Zone &circle = rig.zone("Circle", {-10.0F, 0.0F}, {6.0F, 6.0F});
    circle.shape = ZoneShape::Circle;
    // An L: the bottom bar and the left bar.
    Zone &ell = rig.zone("Ell", {30.0F, 0.0F}, {1.0F, 1.0F});
    ell.shape = ZoneShape::Polygon;
    ell.polygon = J("[[0,0],[6,0],[6,2],[2,2],[2,6],[0,6]]");
    Zone &bad = rig.zone("Bad", {50.0F, 0.0F}, {1.0F, 1.0F});
    bad.shape = ZoneShape::Polygon;
    bad.polygon = J("[[0,0],[1,1]]");
    Zone &worse = rig.zone("Worse", {60.0F, 0.0F}, {1.0F, 1.0F});
    worse.shape = ZoneShape::Polygon;
    worse.polygon = J(R"([[0,0],[1,1],"x"])");
    rig.start();
    // The box: in its own space it spans x from -1 to 3 (offset 1, width 4) and y from -1 to 1; the
    // scale doubles x (so -2..6 before turning), and a quarter turn maps x to y.
    const Zone &b = rig.zoneOf("Box");
    CHECK(b.contains({10.0F, 12.0F}) && b.contains({10.0F, 11.0F}) && !b.contains({10.0F, 17.0F}) &&
          b.contains({10.5F, 10.0F}) && !b.contains({11.5F, 10.0F}));
    // The center is the shape's: offset (1, 0) scaled to (2, 0) and turned to (0, 2).
    CHECK(distance(b.center(), {10.0F, 12.0F}) < 0.001F);
    const Rect box1 = b.bounds();
    CHECK_NEAR(box1.size.x, 2.0, 0.01); // The unscaled y (2 wide) becomes x after the turn.
    CHECK_NEAR(box1.size.y, 8.0, 0.01);
    CHECK(b.boundingRadius() > 4.0F && b.outline().size() == 4);
    // The circle: the width is the diameter.
    const Zone &c = rig.zoneOf("Circle");
    CHECK(c.contains({-10.0F, 0.0F}) && c.contains({-7.1F, 0.0F}) &&
          !c.contains({-6.9F + 0.0F, 3.2F}) && !c.contains({-10.0F, 3.1F}) &&
          c.contains({-10.0F, 2.9F}));
    CHECK(c.outline().size() == 24 && c.boundingRadius() > 3.0F && c.boundingRadius() < 3.1F);
    // The L is not its bounding box.
    const Zone &l = rig.zoneOf("Ell");
    CHECK(l.corners().size() == 6);
    CHECK(l.contains({31.0F, 1.0F}) && l.contains({35.0F, 1.0F}) && l.contains({31.0F, 5.0F}) &&
          !l.contains({34.0F, 4.0F}) && !l.contains({29.0F, 1.0F}) && !l.contains({31.0F, 7.0F}));
    const Rect lb = l.bounds();
    CHECK_NEAR(lb.position.x, 30.0, 0.001);
    CHECK_NEAR(lb.size.x, 6.0, 0.001);
    CHECK(l.outline().size() == 6);
    // Without three valid corners a polygon holds nothing.
    CHECK(rig.zoneOf("Bad").corners().size() == 2 && !rig.zoneOf("Bad").contains({50.5F, 0.5F}) &&
          rig.zoneOf("Worse").corners().empty() && !rig.zoneOf("Worse").contains({60.5F, 0.5F}));
    // A random point inside is inside, for every shape that has an inside.
    Rng rng(7);
    for (const char *name : {"Box", "Circle", "Ell"})
        for (int i = 0; i < 200; ++i)
            CHECK(rig.zoneOf(name).contains(rig.zoneOf(name).pointInside(rng)));
    CHECK(distance(rig.zoneOf("Bad").pointInside(rng), rig.zoneOf("Bad").center()) < 0.001F);
    // Names.
    CHECK(rig.zoneOf("Box").zoneId() == "Box" && rig.zoneOf("Box").label() == "Box");
    rig.zoneOf("Box").id = "lockers";
    rig.zoneOf("Box").name = "Locker room";
    CHECK(rig.zoneOf("Box").zoneId() == "lockers" && rig.zoneOf("Box").label() == "Locker room");
    rig.zoneOf("Box").environment =
        J(R"({"dark": true, "noisy": 0.5, "label": "x", "off": false})");
    CHECK(rig.zoneOf("Box").environmentValue("dark") == 1.0 &&
          rig.zoneOf("Box").environmentValue("off") == 0.0 &&
          rig.zoneOf("Box").environmentValue("noisy") == 0.5 &&
          rig.zoneOf("Box").environmentValue("label", 7.0) == 7.0 &&
          rig.zoneOf("Box").environmentValue("none", 3.0) == 3.0);
    rig.zoneOf("Box").tags = {"private"};
    rig.zoneOf("Box").purposes = {"storage"};
    CHECK(rig.zoneOf("Box").hasTag("private") && !rig.zoneOf("Box").hasTag("public") &&
          rig.zoneOf("Box").hasPurpose("storage"));
}

// ---- Looking zones up
// --------------------------------------------------------------------------------
void lookups() {
    Rig rig;
    Zone &yard = rig.zone("Yard", {0.0F, 0.0F}, {20.0F, 10.0F});
    yard.tags = {"outdoor"};
    yard.purposes = {"exercise"};
    Zone &office = rig.zone("Office", {0.0F, 0.0F}, {4.0F, 4.0F});
    office.room = true;
    office.priority = 5;
    office.id = "office";
    Zone &closet = rig.zone("Closet", {0.0F, 0.0F}, {1.0F, 1.0F});
    closet.room = true; // Same priority as the yard (0), smaller.
    Zone &hall = rig.zone("Hall", {0.0F, 0.0F}, {6.0F, 6.0F});
    hall.room = true; // Priority 0, bigger than the closet.
    Zone &gallery = rig.zone("Gallery", {0.0F, 0.0F}, {10.0F, 10.0F}, "upper");
    gallery.room = true;
    gallery.tags = {"outdoor"};
    Zone &off = rig.zone("Off", {40.0F, 0.0F}, {4.0F, 4.0F});
    off.enabled = false;
    Entity &hidden = rig.scene->createEntity("Hidden");
    hidden.setWorldPosition({50.0F, 0.0F});
    hidden.add<Zone>().size = {4.0F, 4.0F};
    hidden.setActive(false);
    rig.start();
    ZoneService &zones = rig.zones();
    // All that contain the point: the highest priority, then the smaller.
    CHECK((names(zones.at(*rig.runtime, {0.0F, 0.0F}, 0)) ==
           std::vector<std::string>{"office", "Closet", "Hall", "Yard"}));
    CHECK((names(zones.at(*rig.runtime, {2.5F, 0.0F}, 0)) ==
           std::vector<std::string>{"Hall", "Yard"}));
    CHECK((names(zones.at(*rig.runtime, {8.0F, 4.0F}, 0)) == std::vector<std::string>{"Yard"}));
    CHECK(zones.at(*rig.runtime, {30.0F, 0.0F}, 0).empty());
    // Levels keep to themselves.
    CHECK((names(zones.at(*rig.runtime, {0.0F, 0.0F}, 1)) == std::vector<std::string>{"Gallery"}));
    CHECK(zones.zoneAt(*rig.runtime, {0.0F, 0.0F}, 0)->zoneId() == "office" &&
          zones.zoneAt(*rig.runtime, {0.0F, 0.0F}, 1)->zoneId() == "Gallery" &&
          !zones.zoneAt(*rig.runtime, {99.0F, 0.0F}, 0));
    // The room is the highest zone that is one.
    CHECK(zones.roomAt(*rig.runtime, {8.0F, 4.0F}, 0) == nullptr);
    CHECK(zones.roomAt(*rig.runtime, {2.5F, 0.0F}, 0)->zoneId() == "Hall" &&
          zones.roomAt(*rig.runtime, {0.0F, 0.0F}, 0)->zoneId() == "office");
    // Finding by id, tag, purpose; disabled and inactive ones are not there.
    CHECK(zones.find(*rig.runtime, "office") == &rig.zoneOf("Office") &&
          zones.find(*rig.runtime, "Yard") && !zones.find(*rig.runtime, "Off") &&
          !zones.find(*rig.runtime, "Hidden") && !zones.find(*rig.runtime, "nothing"));
    CHECK(zones.all(*rig.runtime).size() == 5);
    CHECK(zones.rooms(*rig.runtime).size() == 4);
    CHECK(zones.withTag(*rig.runtime, "outdoor").size() == 2 &&
          zones.withTag(*rig.runtime, "none").empty());
    CHECK(zones.withPurpose(*rig.runtime, "exercise").size() == 1 &&
          zones.withPurpose(*rig.runtime, "x").empty());
    // Switching one on, and one that is destroyed.
    rig.zoneOf("Off").enabled = true;
    CHECK(zones.find(*rig.runtime, "Off") == nullptr); // Never registered as it did not start.
    rig.runtime->destroyLater(rig.entity("Hall").id());
    rig.step();
    CHECK((names(zones.at(*rig.runtime, {2.5F, 0.0F}, 0)) == std::vector<std::string>{"Yard"}));
    std::vector<std::pair<std::string, std::string>> rows;
    zones.describe(rows);
    CHECK(rows.size() == 2 && rows[0].first == "Zones");
    CHECK(zones.revision() > 0);
}

// ---- Who is where
// ------------------------------------------------------------------------------------
void occupancy() {
    Rig rig;
    rig.zone("Yard", {0.0F, 0.0F}, {20.0F, 10.0F});
    Zone &armory = rig.zone("Armory", {0.0F, 20.0F}, {4.0F, 4.0F});
    armory.room = true;
    armory.id = "armory";
    armory.name = "The Armory";
    armory.allowedFactions = {"guards"};
    armory.enforce = true;
    armory.trespassViolation = "trespass";
    armory.owner = "npc.armorer";
    Zone &lab = rig.zone("Lab", {-20.0F, 0.0F}, {6.0F, 6.0F});
    lab.shape = ZoneShape::Circle;
    lab.access = J(R"({"var": "lab_open"})");
    lab.enforce = true;
    Zone &staff = rig.zone("Staff", {20.0F, 0.0F}, {4.0F, 4.0F});
    staff.allowedRoles = {"medic", "guard"};
    staff.enforce = true;
    Zone &plain = rig.zone("Plain", {0.0F, -20.0F}, {4.0F, 4.0F}); // Anyone; enforce is moot.
    plain.enforce = true;
    Entity &inmate = rig.actor("Inmate", "npc.inmate", "inmates", "inmate", {-50.0F, 0.0F});
    inmate.add<StatSet>();
    inmate.add<StatusEffects>();
    rig.actor("Guard", "npc.guard", "guards", "guard", {-50.0F, 5.0F});
    rig.actor("Armorer", "npc.armorer", "inmates", "inmate", {-50.0F, 10.0F});
    rig.actor("Medic", "npc.medic", "medics", "medic", {-50.0F, 15.0F});
    rig.scene->createEntity("Rock").setWorldPosition({0.0F, 0.0F}); // Not a character: not tracked.
    rig.start();
    ZoneService &zones = rig.zones();
    const EntityId yardId = rig.entity("Yard").id();
    const EntityId inmateId = rig.entity("Inmate").id();
    rig.zones().refresh(*rig.runtime);
    CHECK(zones.occupants(yardId).empty() && zones.zonesOf(*rig.runtime, inmateId).empty());
    // Walking into the yard.
    rig.put("Inmate", {0.0F, 0.0F});
    rig.step();
    CHECK(zones.occupants(yardId) == std::vector<EntityId>{inmateId});
    CHECK((names(zones.zonesOf(*rig.runtime, inmateId)) == std::vector<std::string>{"Yard"}));
    CHECK(rig.count("zone.entered") == 1);
    const GameEvent *entered = rig.last("zone.entered");
    CHECK(entered && entered->source == yardId && entered->other == inmateId &&
          entered->data.get("zone").asString() == "Yard" &&
          entered->data.get("allowed").asBool(false) && !entered->data.get("room").asBool(true) &&
          entered->data.get("actor").asString() == "npc.inmate");
    // Standing still raises nothing more; leaving does.
    rig.zones().refresh(*rig.runtime);
    rig.step(10);
    CHECK(rig.count("zone.entered") == 1 && rig.count("zone.exited") == 0);
    rig.put("Inmate", {40.0F, 0.0F});
    rig.step();
    CHECK(zones.occupants(yardId).empty() && rig.count("zone.exited") == 1 &&
          rig.last("zone.exited")->other == inmateId);

    // A restricted room: the inmate is refused, the guard is not, the owner is let in whoever it
    // is.
    rig.put("Inmate", {0.0F, 20.0F});
    rig.step();
    const GameEvent *refused = rig.last("zone.trespass");
    CHECK(refused && refused->source == rig.entity("Armory").id() && refused->other == inmateId &&
          refused->data.get("violation").asString() == "trespass" &&
          !refused->data.get("allowed").asBool(true) &&
          refused->data.get("name").asString() == "The Armory" &&
          refused->data.get("room").asBool(false));
    CHECK(rig.count("zone.trespass") == 1);
    rig.put("Guard", {0.5F, 20.0F});
    rig.put("Armorer", {-0.5F, 20.0F});
    rig.step();
    CHECK(rig.count("zone.trespass") == 1 &&
          zones.occupants(rig.entity("Armory").id()).size() == 3);
    std::string why;
    CHECK(!rig.zoneOf("Armory").allows(*rig.runtime, rig.entity("Inmate"), &why) &&
          has(why, "only guards may be here"));
    CHECK(rig.zoneOf("Armory").allows(*rig.runtime, rig.entity("Guard")) &&
          rig.zoneOf("Armory").allows(*rig.runtime, rig.entity("Armorer")));
    // A uniform makes the inmate pass the faction check (and not trip the trespass).
    rig.put("Inmate", {40.0F, 0.0F});
    CHECK(rig.entity("Inmate").get<StatusEffects>()->apply(*rig.runtime, "uniform"));
    CHECK(rig.zoneOf("Armory").allows(*rig.runtime, rig.entity("Inmate")));
    rig.put("Inmate", {0.0F, 20.0F});
    rig.step();
    CHECK(rig.count("zone.trespass") == 1);
    rig.zoneOf("Armory").countDisguise = false; // A zone that sees through clothes.
    CHECK(!rig.zoneOf("Armory").allows(*rig.runtime, rig.entity("Inmate")));
    rig.zoneOf("Armory").countDisguise = true;

    // Roles.
    rig.put("Medic", {20.0F, 0.0F});
    rig.put("Inmate", {20.5F, 0.0F});
    rig.step();
    CHECK(rig.zoneOf("Staff").allows(*rig.runtime, rig.entity("Medic")) &&
          !rig.zoneOf("Staff").allows(*rig.runtime, rig.entity("Armorer"), &why) &&
          has(why, "only medic, guard may be here"));
    // The inmate wears the uniform: guards-only is a faction rule, this is a role rule: still no.
    CHECK(rig.count("zone.trespass") == 2);

    // A condition.
    rig.put("Medic", {-20.0F, 0.0F});
    rig.step();
    CHECK(rig.count("zone.trespass") == 3 &&
          !rig.zoneOf("Lab").allows(*rig.runtime, rig.entity("Medic"), &why) &&
          has(why, "access to 'Lab' is not granted"));
    rig.runtime->blackboard().setBool("lab_open", true);
    CHECK(rig.zoneOf("Lab").allows(*rig.runtime, rig.entity("Medic")));
    rig.put("Medic", {-50.0F, 15.0F});
    rig.put("Medic", {-20.0F, 1.0F});
    rig.step();
    CHECK(rig.count("zone.trespass") == 3); // Entered again, this time with access.
    // A zone with nothing to enforce does not trespass.
    rig.put("Medic", {0.0F, -20.0F});
    rig.step();
    CHECK(rig.count("zone.trespass") == 3 &&
          rig.zoneOf("Plain").allows(*rig.runtime, rig.entity("Medic")) &&
          !rig.zoneOf("Plain").restricted() && rig.zoneOf("Lab").restricted() &&
          rig.zoneOf("Armory").restricted());

    // The service looks at a few characters each tick on its own.
    rig.entity("Guard").setWorldPosition({0.0F, 0.0F});
    const int enters = rig.count("zone.entered");
    rig.step(12);
    CHECK(rig.count("zone.entered") > enters);
    CHECK(zones.occupants(yardId).size() >= 1);
    // A character that leaves the world leaves its zones.
    const int exits = rig.count("zone.exited");
    rig.runtime->destroyLater(rig.entity("Guard").id());
    rig.step();
    CHECK(rig.count("zone.exited") > exits && zones.occupants(yardId).empty());
    // Switching a zone off takes it out of the picture.
    rig.zoneOf("Plain").enabled = false;
    CHECK(!zones.find(*rig.runtime, "Plain"));
}

// ---- Destinations
// ------------------------------------------------------------------------------------
void destinations() {
    Rig rig;
    Zone &cafe = rig.zone("Cafeteria", {15.0F, 0.0F}, {6.0F, 4.0F});
    cafe.room = true;
    cafe.id = "cafeteria";
    cafe.purposes = {"dining"};
    cafe.capacity = 2;
    Zone &canteen = rig.zone("Canteen", {15.0F, 20.0F}, {6.0F, 4.0F});
    canteen.room = true;
    canteen.id = "canteen";
    canteen.purposes = {"dining"};
    Zone &upper = rig.zone("Dorm", {0.0F, 0.0F}, {6.0F, 4.0F}, "upper");
    upper.room = true;
    upper.id = "dorm";
    upper.purposes = {"sleep"};
    Zone &yard = rig.zone("Yard", {-30.0F, 0.0F}, {10.0F, 10.0F});
    yard.id = "yard";
    Entity &dave = rig.actor("Dave", "npc.dave", "inmates", "inmate", {0.0F, 0.0F});
    dave.get<Identity>()->data = J(R"({"home": "dorm"})");
    rig.actor("Ann", "npc.ann", "inmates", "inmate", {16.0F, 1.0F});
    rig.actor("Bo", "npc.bo", "inmates", "inmate", {17.0F, -1.0F});
    rig.actor("Cy", "npc.cy", "inmates", "inmate", {-30.0F, 0.0F});
    rig.scene->createEntity("Door").setWorldPosition({7.0F, 7.0F});
    rig.start();
    ZoneService &zones = rig.zones();
    GameContext &game = *rig.runtime;
    rig.zones().refresh(game);
    Rng rng(3);
    const auto place = [&](const char *who, const char *text) {
        ScheduleDestination destination;
        std::string error;
        auto parsed = ScheduleDestination::fromJson(J(text));
        CHECK(parsed);
        if (parsed)
            destination = parsed.value();
        return zones.destinationFor(game, destination, rig.entity(who), rng);
    };
    const auto at = [&](const char *who, const char *text) {
        auto parsed = ScheduleDestination::fromJson(J(text));
        CHECK(parsed);
        return parsed && zones.isAt(game, parsed.value(), rig.entity(who));
    };
    // A zone or a room by id; a room that is not a room, and unknown ones, lead nowhere.
    auto toYard = place("Cy", R"({"zone": "yard"})");
    CHECK(toYard && rig.zoneOf("Yard").contains(toYard->point) &&
          toYard->zone == rig.entity("Yard").id() && toYard->level == 0);
    CHECK(place("Cy", R"({"room": "cafeteria"})") && !place("Cy", R"({"room": "yard"})") &&
          !place("Cy", R"({"zone": "nowhere"})") && !place("Cy", R"({"purpose": "nothing"})"));
    // A purpose goes to a room with space, the nearest one: Ann and Bo fill the cafeteria (two
    // seats).
    CHECK(zones.occupants(rig.entity("Cafeteria").id()).size() == 2);
    auto cyDines = place("Cy", R"({"purpose": "dining"})");
    CHECK(cyDines && cyDines->zone == rig.entity("Canteen").id()); // The cafeteria is full.
    auto annDines = place("Ann", R"({"purpose": "dining"})");
    CHECK(annDines &&
          annDines->zone == rig.entity("Cafeteria").id()); // Her own seat is not counted.
    // Dave lives upstairs: his home is the dorm, on the other level.
    auto home = place("Dave", R"("home")");
    CHECK(home && home->zone == rig.entity("Dorm").id() && home->level == 1);
    // Without a named home, home is where the character started.
    auto cyHome = place("Cy", R"("home")");
    CHECK(cyHome && distance(cyHome->point, {-30.0F, 0.0F}) < 0.001F);
    // A point, an entity.
    auto point = place("Cy", R"({"point": [3, 4]})");
    CHECK(point && point->point == (Vec2{3.0F, 4.0F}) && !point->zone);
    auto door = place("Cy", R"({"entity": "name:Door"})");
    CHECK(door && door->point == (Vec2{7.0F, 7.0F}));
    CHECK(!place("Cy", R"({"entity": "name:Nowhere"})"));
    CHECK(!zones.destinationFor(game, ScheduleDestination{}, rig.entity("Cy"), rng));

    // Being there.
    CHECK(at("Ann", R"({"zone": "cafeteria"})") && at("Ann", R"({"room": "cafeteria"})") &&
          at("Ann", R"({"purpose": "dining"})") && !at("Ann", R"({"purpose": "sleep"})") &&
          !at("Cy", R"({"zone": "cafeteria"})") && at("Cy", R"({"zone": "yard"})") &&
          !at("Cy", R"({"room": "yard"})"));
    CHECK(at("Cy", R"({"point": [-30, 0.5]})") && !at("Cy", R"({"point": [0, 0]})") &&
          at("Dave", R"("home")") ==
              false); // Dave stands at (0,0) on level 0: not in the dorm (level 1).
    CHECK(at("Cy", R"("home")") && at("Cy", R"({"entity": "name:Cy"})") &&
          !at("Cy", R"({"entity": "name:Door"})") && at("Cy", "{\"zone\": \"yard\"}") &&
          at("Cy", R"({"entity": "name:Cy"})"));
    CHECK(zones.isAt(game, ScheduleDestination{},
                     rig.entity("Cy"))); // No destination: nowhere to be.
}

// ---- Rules
// ---------------------------------------------------------------------------------------
void rules() {
    Rig rig;
    Zone &yard = rig.zone("Yard", {0.0F, 0.0F}, {20.0F, 20.0F});
    yard.id = "yard";
    yard.tags = {"outdoor"};
    yard.environment = J(R"({"noisy": 0.5, "dark": false})");
    Zone &cell = rig.zone("Cell", {0.0F, 0.0F}, {4.0F, 4.0F});
    cell.id = "cell_3";
    cell.name = "Cell 3";
    cell.room = true;
    cell.priority = 3;
    cell.purposes = {"sleep"};
    cell.allowedRoles = {"guard"};
    cell.environment = J(R"({"dark": true})");
    Entity &dave = rig.actor("Dave", "npc.dave", "inmates", "inmate", {0.0F, 0.0F});
    rig.actor("Guard", "npc.guard", "guards", "guard", {8.0F, 8.0F});
    rig.actor("Out", "npc.out", "guards", "guard", {100.0F, 100.0F});
    CHECK(dave.add<RuleSet>().setRulesJson(J(R"([
      {"id": "facts", "when": "ask", "then": [
        {"type": "SetVariable", "name": "id", "value": "$self.zone.id"},
        {"type": "SetVariable", "name": "name", "value": "$self.zone.name"},
        {"type": "SetVariable", "name": "room", "value": "$self.zone.room"},
        {"type": "SetVariable", "name": "roomName", "value": "$self.zone.roomName"},
        {"type": "SetVariable", "name": "purpose", "value": "$self.zone.purpose"},
        {"type": "SetVariable", "name": "count", "value": "$self.zone.count"},
        {"type": "SetVariable", "name": "restricted", "value": "$self.zone.restricted"},
        {"type": "SetVariable", "name": "dark", "value": "$self.zone.env.dark"},
        {"type": "SetVariable", "name": "noisy", "value": "$self.zone.env.noisy"},
        {"type": "SetVariable", "name": "windy", "value": "$self.zone.env.windy"}]},
      {"id": "in_cell", "when": "ask", "if": {"type": "InZone", "zone": "cell_3", "entity": "self"},
       "then": {"type": "SetVariable", "name": "in_cell", "value": true}},
      {"id": "in_tag", "when": "ask", "if": {"type": "InZone", "tag": "outdoor", "entity": "self"},
       "then": {"type": "SetVariable", "name": "in_outdoor", "value": true}},
      {"id": "in_purpose", "when": "ask", "if": {"type": "InZone", "purpose": "sleep", "entity": "self"},
       "then": {"type": "SetVariable", "name": "in_sleep", "value": true}},
      {"id": "in_room", "when": "ask", "if": {"type": "InZone", "room": true, "entity": "self"},
       "then": {"type": "SetVariable", "name": "in_room", "value": true}},
      {"id": "in_any", "when": "ask", "if": {"type": "InZone", "entity": "name:Out"},
       "then": {"type": "SetVariable", "name": "out_in_any", "value": true}},
      {"id": "guard_in", "when": "ask", "if": {"type": "InZone", "zone": "yard", "entity": "name:Guard"},
       "then": {"type": "SetVariable", "name": "guard_in_yard", "value": true}},
      {"id": "allows", "when": "ask", "if": {"type": "ZoneAllows", "zone": "cell_3", "entity": "self"},
       "then": {"type": "SetVariable", "name": "allowed", "value": true}},
      {"id": "guard_allows", "when": "ask", "if": {"type": "ZoneAllows", "zone": "cell_3", "entity": "name:Guard"},
       "then": {"type": "SetVariable", "name": "guard_allowed", "value": true}},
      {"id": "occupied", "when": "ask", "if": {"type": "ZoneOccupied", "zone": "yard", "atLeast": 2},
       "then": {"type": "SetVariable", "name": "yard_busy", "value": true}},
      {"id": "occupied_guard", "when": "ask", "if": {"type": "ZoneOccupied", "zone": "yard", "faction": "guards"},
       "then": {"type": "SetVariable", "name": "yard_guarded", "value": true}},
      {"id": "occupied_role", "when": "ask", "if": {"type": "ZoneOccupied", "zone": "yard", "role": "medic"},
       "then": {"type": "SetVariable", "name": "yard_medic", "value": true}},
      {"id": "occupied_none", "when": "ask", "if": {"type": "ZoneOccupied", "zone": "cell_3", "atLeast": 3},
       "then": {"type": "SetVariable", "name": "cell_full", "value": true}},
      {"id": "no_zone", "when": "ask", "if": {"type": "ZoneAllows", "zone": "nowhere", "entity": "self"},
       "then": {"type": "SetVariable", "name": "nowhere", "value": true}}
    ])")));
    rig.start();
    rig.zones().refresh(*rig.runtime);
    GameContext &game = *rig.runtime;
    game.events().emit(GameEvent("ask", rig.entity("Dave").id()));
    rig.step();
    const Blackboard &board = game.blackboard();
    CHECK(board.text("id") == "cell_3" && board.text("name") == "Cell 3" &&
          board.text("room") == "cell_3" && board.text("roomName") == "Cell 3" &&
          board.text("purpose") == "sleep" && board.number("count") == 2.0 &&
          board.flag("restricted"));
    CHECK(board.number("dark") == 1.0 && board.number("noisy") == 0.5 &&
          board.number("windy") == 0.0);
    CHECK(board.flag("in_cell") && board.flag("in_outdoor") && board.flag("in_sleep") &&
          board.flag("in_room") && !board.flag("out_in_any") && board.flag("guard_in_yard") &&
          !board.flag("allowed") && board.flag("guard_allowed") && !board.flag("nowhere"));
    CHECK(board.flag("yard_busy") && board.flag("yard_guarded") && !board.flag("yard_medic") &&
          !board.flag("cell_full"));
}

// ---- What the validator says
// -----------------------------------------------------------------------
void checks() {
    Rig rig;
    rig.zone("Z", {0.0F, 0.0F}, {2.0F, 2.0F});
    rig.start();
    const ComponentType *type = rig.registry.find("Zone");
    CHECK(type && type->check);
    if (!type || !type->check)
        return;
    GameData data;
    std::vector<DataProblem> problems;
    data.add(J(dataText), "d.ykdata", problems);
    const auto problemsOf = [&](const std::function<void(Zone &)> &set) {
        Zone zone;
        set(zone);
        CheckContext context;
        context.known = [&](std::string_view kind, std::string_view id) {
            return data.known(kind, id);
        };
        std::vector<std::string> found;
        type->check(rig.entity("Z"), zone, context, found);
        return found;
    };
    CHECK(problemsOf([](Zone &) {}).empty());
    auto flat = problemsOf([](Zone &z) { z.size = {0.0F, 2.0F}; });
    CHECK(flat.size() == 1 && has(flat[0], "has no area"));
    auto poly = problemsOf([](Zone &z) { z.shape = ZoneShape::Polygon; });
    CHECK(poly.size() == 1 && has(poly[0], "polygon without at least three valid corners"));
    CHECK(problemsOf([](Zone &z) {
              z.shape = ZoneShape::Circle;
              z.size = {3.0F, 0.0F};
          }).empty());
    auto factions = problemsOf([](Zone &z) { z.allowedFactions = {"guards", "wardens"}; });
    CHECK(factions.size() == 1 &&
          has(factions[0], "allows the faction 'wardens', which is not defined"));
    auto condition = problemsOf([](Zone &z) { z.access = J(R"({"type": "Explode"})"); });
    CHECK(condition.size() == 1 && has(condition[0], "unknown condition 'Explode'"));
    auto brokenCondition = problemsOf([](Zone &z) { z.access = J(R"({"all": 3})"); });
    CHECK(brokenCondition.size() == 1 &&
          has(brokenCondition[0], "'access' is not a valid condition"));
    auto environment = problemsOf([](Zone &z) { z.environment = J("[1]"); });
    CHECK(environment.size() == 1 && has(environment[0], "'environment' must be an object"));
    auto idle = problemsOf([](Zone &z) { z.enforce = true; });
    CHECK(idle.size() == 1 &&
          has(idle[0], "enforces access but has no faction, role or condition"));
    CHECK(problemsOf([](Zone &z) {
              z.enforce = true;
              z.allowedRoles = {"guard"};
          }).empty());
    auto seats = problemsOf([](Zone &z) { z.capacity = 4; });
    CHECK(seats.size() == 1 && has(seats[0], "capacity but no purpose"));
    CHECK(problemsOf([](Zone &z) {
              z.capacity = 4;
              z.room = true;
          }).empty());
}

void project() {
    using Severity = ProjectIssue::Severity;
    const std::filesystem::path root = std::filesystem::current_path() / "zones-test-project";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scenes");
    Project project = Project::create(root, "Zones");
    project.startScene = "scenes/main.ykscene";
    ComponentRegistry registry;
    registerEngineComponents(registry);
    Scene scene(registry, 21);
    scene.createEntity("Yard").add<Zone>().id = "yard";
    scene.createEntity("Other Yard").add<Zone>().id = "yard"; // The same id.
    scene.createEntity("Hall").add<Zone>();                   // Named after its entity.
    scene.createEntity("Hall 2").add<Zone>().id = "Hall";     // And that clashes with it.
    scene.createEntity("Fine").add<Zone>().id = "fine";
    CHECK(saveScene(scene, root / "scenes/main.ykscene"));
    setLogStderrEnabled(false);
    const auto issues = validateProject(project, registry);
    setLogStderrEnabled(true);
    const auto about = [&](const char *part) {
        return std::any_of(issues.begin(), issues.end(), [&](const ProjectIssue &issue) {
            return issue.severity == Severity::Error && issue.path == "scenes/main.ykscene" &&
                   issue.message.find(part) != std::string::npos;
        });
    };
    CHECK(about("'Other Yard' Zone: the id 'yard' is also used by 'Yard'"));
    CHECK(about("'Hall 2' Zone: the id 'Hall' is also used by 'Hall'"));
    CHECK(!about("'Fine'"));
    std::filesystem::remove_all(root);
}

// ---- Routines that need places
// ----------------------------------------------------------------------
void routines() {
    Rig rig;
    rig.clock(1.0F, "06:55"); // A minute of the world per second.
    Zone &yard = rig.zone("Yard", {0.0F, 0.0F}, {20.0F, 10.0F});
    yard.id = "yard";
    Zone &shop = rig.zone("Shop", {30.0F, 0.0F}, {6.0F, 6.0F});
    shop.id = "shop";
    shop.purposes = {"work"};
    Zone &gym = rig.zone("Gym", {0.0F, 20.0F}, {6.0F, 6.0F});
    gym.id = "gym";
    gym.room = true;
    Zone &cell = rig.zone("Cell", {0.0F, 40.0F}, {3.0F, 3.0F});
    cell.id = "cell";
    cell.room = true;
    cell.purposes = {"sleep"};
    Entity &player = rig.actor("Player", "player.1", "inmates", "inmate", {-50.0F, 0.0F});
    auto &agent = player.add<ScheduleAgent>();
    agent.schedule = "routine";
    agent.enforce = true;
    Entity &npc = rig.actor("Npc", "npc.1", "inmates", "inmate", {-50.0F, 5.0F});
    npc.add<ScheduleAgent>().schedule = "routine"; // Does not enforce: no requirement events.
    Entity &blind = rig.actor("Blind", "npc.2", "inmates", "inmate", {0.0F, 0.0F});
    auto &blindAgent = blind.add<ScheduleAgent>();
    blindAgent.schedule = "routine";
    blindAgent.detectArrival = false;
    rig.start();
    GameContext &game = *rig.runtime;
    const auto events = [&](const char *name, const char *who) {
        int n = 0;
        for (const GameEvent &event : rig.heard)
            n += event.name == name && event.source == rig.entity(who).id() ? 1 : 0;
        return n;
    };
    ScheduleAgent &me = *rig.entity("Player").get<ScheduleAgent>();
    ScheduleAgent &other = *rig.entity("Npc").get<ScheduleAgent>();

    // 07:00: assembly in the yard. Not there: not arrived, and late after five minutes.
    rig.until("07:00");
    CHECK(me.current() && me.current()->id == "assembly" && !me.arrived() && !me.requirementMet());
    rig.step(60 * 3);
    CHECK(!me.late() && me.requirementProgress() == 0.0);
    // Walking into the yard: it notices (the zone service looks a few times a second).
    rig.entity("Player").setWorldPosition({0.0F, 0.0F});
    rig.step(30);
    CHECK(me.arrived() && events("schedule.arrived", "Player") == 1);
    CHECK(rig.last("schedule.arrived")->data.get("destination").asString() == "zone:yard");
    // It has to stay five seconds, without a break: leaving starts it over.
    rig.step(120);
    CHECK(!me.requirementMet() && me.requirementProgress() > 0.3 && me.requirementProgress() < 0.7);
    rig.entity("Player").setWorldPosition({-50.0F, 0.0F});
    rig.step(30);
    CHECK(me.requirementProgress() == 0.0);
    rig.entity("Player").setWorldPosition({0.0F, 0.0F});
    rig.step(200);
    CHECK(!me.requirementMet());
    rig.step(150);
    CHECK(me.requirementMet() && me.requirementProgress() == 1.0 &&
          events("schedule.requirement_met", "Player") == 1);
    CHECK(rig.last("schedule.requirement_met")->data.get("block").asString() == "assembly");
    // The other characters that are not enforced said nothing of requirements; the one that does
    // not look for arrival never arrives by itself but is where it should be (and says nothing).
    CHECK(events("schedule.requirement_met", "Npc") == 0 &&
          events("schedule.requirement_missed", "Npc") == 0 &&
          !rig.entity("Blind").get<ScheduleAgent>()->arrived() &&
          events("schedule.arrived", "Blind") == 0);
    CHECK(!other.arrived() && other.late()); // The npc is far from the yard, and late.

    // Chores at 08:00: the action must be done during the block. Not done: missed at 09:00.
    rig.until("08:00");
    CHECK(me.current()->id == "chores" && !me.requirementMet());
    CHECK(events("schedule.requirement_missed", "Player") == 0);
    rig.until("09:00");
    CHECK(events("schedule.requirement_missed", "Player") == 1);
    const GameEvent *missed = rig.last("schedule.requirement_missed");
    CHECK(missed && missed->data.get("block").asString() == "chores" &&
          !missed->data.get("action").asBool(true) &&
          missed->data.get("activity").asString() == "Chores");
    CHECK(events("schedule.requirement_missed", "Npc") == 0);

    // Gym at 09:00: a room to enter, and no time to stay. Entering is enough.
    CHECK(me.current()->id == "gym" && !me.requirementMet());
    rig.entity("Player").setWorldPosition({0.0F, 20.0F});
    rig.step(30);
    CHECK(me.requirementMet() && me.arrived() && events("schedule.requirement_met", "Player") == 2);
    rig.until("10:00");
    CHECK(events("schedule.requirement_missed", "Player") == 1); // Met: not missed.
    // Visit at 10:00: a point, no requirement.
    CHECK(me.current()->id == "visit" && !me.requirementMet() && me.requirementProgress() == 0.0);
    rig.until("11:00");
    CHECK(events("schedule.requirement_missed", "Player") == 1);

    // Another day's chores: this time the character does it (any event of that name raised by it).
    rig.runtime->services().get<WorldClock>().set(game, 2, 8 * 60);
    rig.step(2);
    CHECK(me.current()->id == "chores");
    game.events().emit(GameEvent("chore_done", rig.entity("Npc").id())); // Somebody else's: no.
    game.events().emit(GameEvent("sweep", me.entity().id()));            // Another action: no.
    rig.step(2);
    CHECK(!me.requirementMet());
    game.events().emit(GameEvent("chore_done", me.entity().id()));
    rig.step(2);
    CHECK(me.requirementMet() && events("schedule.requirement_met", "Player") == 3);
    rig.until("09:00");
    CHECK(events("schedule.requirement_missed", "Player") == 1); // Done: not missed.

    // Being excused: whatever is missed while excused is forgiven.
    me.excuse(game, "fight", true);
    rig.runtime->services().get<WorldClock>().set(game, 2, 7 * 60);
    rig.step(2);
    CHECK(me.current()->id == "assembly");
    me.excuse(game, "fight", false);
    rig.until("08:00");
    CHECK(events("schedule.requirement_missed", "Player") == 1);

    // The night block names a room and a home; the cell is the player's home by purpose.
    rig.runtime->services().get<WorldClock>().set(game, 2, 22 * 60);
    rig.step(2);
    CHECK(me.current()->id == "lights" && !me.requirementMet());
    rig.entity("Player").setWorldPosition({0.0F, 40.0F});
    rig.step(30);
    CHECK(me.requirementMet() && !me.arrived()); // Home is where it started, not the cell.
    // Saved state keeps the progress of a block.
    const Json saved = me.saveState();
    CHECK(saved.get("requirementMet").asBool(false) && saved.get("entered").asBool(false));
    Json fresh = Json::object();
    fresh.set("block", "lights");
    fresh.set("requirementMet", false);
    fresh.set("entered", true);
    fresh.set("stayed", 2.5);
    fresh.set("actionDone", true);
    fresh.set("excused", true);
    CHECK(me.loadState(game, fresh));
    CHECK(!me.requirementMet() && me.requirementProgress() == 0.5);
    // Facts.
    Entity &reader = rig.entity("Player");
    RuleContext rc(game);
    rc.self = reader.id();
    rc.actor = reader.id();
    CHECK(rc.fact("self.schedule.requirementMet").has_value() &&
          rc.fact("self.schedule.requirementProgress").has_value());
}
} // namespace

int main() {
    setLogStderrEnabled(false);
    shapes();
    lookups();
    occupancy();
    destinations();
    rules();
    routines();
    checks();
    project();
    return yk::test::finish("zones");
}
