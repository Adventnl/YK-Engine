#include "yk/sim/Zones.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/runtime/Random.hpp"
#include "yk/sim/Identity.hpp"
#include "yk/world/SpatialIndex.hpp"
#include "yk/world/WorldLevels.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
const std::vector<std::string> &zoneShapeNames() {
    static const std::vector<std::string> names = {"box", "circle", "polygon"};
    return names;
}

namespace {
constexpr float lookInterval = 0.1F; // Seconds between looks at one character.

bool pointInPolygon(const std::vector<Vec2> &corners, Vec2 p) {
    bool inside = false;
    for (std::size_t i = 0, j = corners.size() - 1; i < corners.size(); j = i++) {
        const Vec2 a = corners[i], b = corners[j];
        if (((a.y > p.y) != (b.y > p.y)) && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
            inside = !inside;
    }
    return inside;
}

std::string joinWords(const std::vector<std::string> &words) {
    std::string text;
    for (const std::string &word : words)
        text += (text.empty() ? "" : ", ") + word;
    return text;
}
} // namespace

// ---- The component
// -------------------------------------------------------------------------------------
void Zone::describe(TypeBuilder<Zone> &type) {
    type.category("Simulation")
        .description("An area of the world with a name, tags, a purpose and a say in who may be "
                     "there. A room is a zone that is also a destination, an owner's, searchable. "
                     "Characters entering and leaving raise zone.entered / zone.exited.");
    type.field("shape", &Zone::shape).options({"Box", "Circle", "Polygon"});
    type.field("size", &Zone::size)
        .size()
        .tooltip("Box: the extent; circle: the width is the "
                 "diameter.");
    type.field("offset", &Zone::offset).offset();
    type.field("polygon", &Zone::polygon)
        .tooltip("Polygon corners as [[x, y], ...] in the entity's own space (at least three).");
    type.field("id", &Zone::id)
        .tooltip("What schedules and rules call it (\"cafeteria\"); empty: the entity's name.");
    type.field("name", &Zone::name).tooltip("Shown on the map and in messages.");
    type.field("room", &Zone::room).tooltip("A logical space a character can be sent to.");
    type.field("tags", &Zone::tags).tooltip("restricted, private, cell...");
    type.field("purposes", &Zone::purposes)
        .tooltip("What it is for: dining, sleep, work. A schedule can send a character to "
                 "\"purpose:dining\".");
    type.field("priority", &Zone::priority)
        .range(-100, 100, 1)
        .tooltip("Where zones overlap the highest is the zone there.");
    type.field("capacity", &Zone::capacity)
        .range(0, 1000, 1)
        .tooltip("How many may be sent here at once (seats, bunks); 0: any number.");
    type.field("allowedFactions", &Zone::allowedFactions).ref("faction");
    type.field("allowedRoles", &Zone::allowedRoles);
    type.field("access", &Zone::access)
        .tooltip("A condition that must hold for whoever is here ({\"type\": \"HasItem\", ...}); "
                 "empty: none.");
    type.field("countDisguise", &Zone::countDisguise)
        .tooltip("Faction checks go by what the character looks like (a uniform opens a staff "
                 "area), not what they are.");
    type.field("owner", &Zone::owner)
        .tooltip("Persistent id of who it belongs to; always allowed.");
    type.field("enforce", &Zone::enforce).tooltip("Entering without access raises zone.trespass.");
    type.field("trespassViolation", &Zone::trespassViolation)
        .tooltip("The violation a trespass is (for the violation system).");
    type.field("environment", &Zone::environment)
        .tooltip("Flags other systems read: {\"dark\": true, \"noisy\": 0.5}.");
    type.field("navigationArea", &Zone::navigationArea)
        .tooltip(
            "The navigation area its ground belongs to (\"restricted\"): agents may avoid it.");
    type.field("navigationCost", &Zone::navigationCost)
        .range(1, 20, 0.1)
        .tooltip("How much dearer it is to walk here (a multiplier on the path cost).");
    type.check([](const Entity &entity, const Zone &zone, const CheckContext &context,
                  std::vector<std::string> &problems) {
        if (zone.shape == ZoneShape::Polygon) {
            if (zone.corners().size() < 3)
                problems.push_back("is a polygon without at least three valid corners "
                                   "([[x, y], ...])");
        } else if (!(zone.size.x > 0.0F) ||
                   (zone.shape == ZoneShape::Box && !(zone.size.y > 0.0F))) {
            problems.push_back("has no area (size must be above zero)");
        }
        for (const std::string &faction : zone.allowedFactions)
            if (context.known && !context.known("faction", faction))
                problems.push_back("allows the faction '" + faction + "', which is not defined");
        if (zone.hasCondition()) {
            auto condition = Condition::fromJson(zone.access);
            if (!condition) {
                problems.push_back("'access' is not a valid condition: " + condition.error());
            } else if (const RuleCatalog *catalog =
                           entity.scene().registry().extension<RuleCatalog>()) {
                ComponentRuleReport report(problems, context);
                catalog->check(condition.value(), report);
            }
        }
        if (!zone.environment.isObject() && !zone.environment.isNull())
            problems.push_back("'environment' must be an object of flags");
        if (zone.capacity < 0)
            problems.push_back("'capacity' cannot be negative");
        if (zone.enforce && !zone.restricted())
            problems.push_back("enforces access but has no faction, role or condition to enforce");
        if (!zone.room && zone.capacity > 0 && zone.purposes.empty())
            problems.push_back("has a capacity but no purpose to be sent to for");
    });
}

const std::string &Zone::zoneId() const {
    return id.empty() ? entity().name() : id;
}
const std::string &Zone::label() const {
    return name.empty() ? zoneId() : name;
}
bool Zone::hasTag(std::string_view tag) const {
    return data::has(tags, tag);
}
bool Zone::hasPurpose(std::string_view purpose) const {
    return data::has(purposes, purpose);
}
double Zone::environmentValue(std::string_view key, double fallback) const {
    const Json *value = environment.isObject() ? environment.find(key) : nullptr;
    if (!value)
        return fallback;
    if (value->isBool())
        return value->asBool() ? 1.0 : 0.0;
    return value->isNumber() ? value->asNumber() : fallback;
}
int Zone::level() const {
    return std::max(levelOf(entity()), 0);
}

const std::vector<Vec2> &Zone::corners() const {
    if (cornersSource_ == polygon)
        return corners_;
    cornersSource_ = polygon;
    corners_.clear();
    if (!polygon.isArray())
        return corners_;
    for (const Json &corner : polygon.items()) {
        if (!corner.isArray() || corner.size() != 2 || !corner.at(0).isNumber() ||
            !corner.at(1).isNumber()) {
            corners_.clear();
            return corners_;
        }
        corners_.push_back({static_cast<float>(corner.at(0).asNumber()),
                            static_cast<float>(corner.at(1).asNumber())});
    }
    return corners_;
}

bool Zone::contains(Vec2 world) const {
    const Vec2 inEntity = inverseTransformPoint(entity().worldTransform(), world);
    const Vec2 local = inEntity - offset;
    switch (shape) {
    case ZoneShape::Box:
        return std::fabs(local.x) <= size.x * 0.5F && std::fabs(local.y) <= size.y * 0.5F;
    case ZoneShape::Circle:
        return lengthSquared(local) <= size.x * size.x * 0.25F;
    case ZoneShape::Polygon: {
        const auto &points = corners();
        return points.size() >= 3 && pointInPolygon(points, inEntity); // Corners are not offset.
    }
    }
    return false;
}

std::vector<Vec2> Zone::outline() const {
    const Transform2D world = entity().worldTransform();
    std::vector<Vec2> points;
    switch (shape) {
    case ZoneShape::Box: {
        const Vec2 h = size * 0.5F;
        for (const Vec2 corner :
             {Vec2{-h.x, -h.y}, Vec2{h.x, -h.y}, Vec2{h.x, h.y}, Vec2{-h.x, h.y}})
            points.push_back(transformPoint(world, corner + offset));
        break;
    }
    case ZoneShape::Circle:
        for (int i = 0; i < 24; ++i) {
            const float angle = static_cast<float>(i) * 6.2831853F / 24.0F;
            points.push_back(transformPoint(world, offset + Vec2{std::cos(angle), std::sin(angle)} *
                                                                (size.x * 0.5F)));
        }
        break;
    case ZoneShape::Polygon:
        for (const Vec2 corner : corners())
            points.push_back(transformPoint(world, corner));
        break;
    }
    return points;
}

Rect Zone::bounds() const {
    const auto points = outline();
    if (points.empty())
        return {entity().worldPosition(), {0.0F, 0.0F}};
    Vec2 low = points.front(), high = points.front();
    for (const Vec2 point : points) {
        low = {std::min(low.x, point.x), std::min(low.y, point.y)};
        high = {std::max(high.x, point.x), std::max(high.y, point.y)};
    }
    return {low, high - low};
}

Vec2 Zone::center() const {
    const Rect box = bounds();
    if (shape == ZoneShape::Polygon && !corners().empty())
        return yk::center(box); // The middle of the box (not always inside a concave polygon).
    return transformPoint(entity().worldTransform(), offset);
}

float Zone::boundingRadius() const {
    float most = 0.0F;
    for (const Vec2 point : outline())
        most = std::max(most, distance(point, entity().worldPosition()));
    return most + 0.01F;
}

Vec2 Zone::pointInside(Rng &rng) const {
    const Rect box = bounds();
    for (int attempt = 0; attempt < 24; ++attempt) {
        const Vec2 point{box.position.x + static_cast<float>(rng.uniform()) * box.size.x,
                         box.position.y + static_cast<float>(rng.uniform()) * box.size.y};
        if (contains(point))
            return point;
    }
    return center();
}

bool Zone::allows(GameContext &context, const Entity &actor, std::string *why) const {
    const auto *identity = actor.get<Identity>();
    if (!owner.empty() && identity && identity->id == owner)
        return true;
    if (!allowedFactions.empty()) {
        const std::string faction = countDisguise ? perceivedFaction(actor)
                                                  : (identity ? identity->faction : std::string());
        if (!data::has(allowedFactions, faction)) {
            if (why)
                *why = "only " + joinWords(allowedFactions) + " may be here";
            return false;
        }
    }
    if (!allowedRoles.empty()) {
        const std::string role = identity ? identity->role : std::string();
        if (!data::has(allowedRoles, role)) {
            if (why)
                *why = "only " + joinWords(allowedRoles) + " may be here";
            return false;
        }
    }
    if (hasCondition()) {
        if (!(conditionSource_ == access)) {
            conditionSource_ = access;
            auto parsed = Condition::fromJson(access);
            conditionOk_ = static_cast<bool>(parsed);
            condition_ = parsed ? parsed.value() : Condition{};
            if (!parsed)
                log(LogLevel::Warning, "zones",
                    "The access condition of '" + zoneId() + "' is not valid: " + parsed.error());
        }
        if (conditionOk_) {
            RuleContext rc(context);
            rc.self = entity().id();
            rc.actor = actor.id();
            rc.target = entity().id();
            rc.origin = "zone '" + zoneId() + "'";
            if (!evaluate(condition_, rc)) {
                if (why)
                    *why = "access to '" + label() + "' is not granted";
                return false;
            }
        }
    }
    return true;
}

void Zone::onStart(GameContext &context) {
    context.services().get<ZoneService>().add(context, *this);
}
void Zone::onDestroy(GameContext &context) {
    if (auto *service = context.services().find<ZoneService>())
        service->remove(context, entity().id());
}

// ---- The service
// -----------------------------------------------------------------------------------------
void ZoneService::add(GameContext &context, const Zone &zone) {
    records_.erase(
        std::remove_if(records_.begin(), records_.end(),
                       [&](const Record &record) { return record.id == zone.entity().id(); }),
        records_.end());
    records_.push_back({zone.entity().id(), zone.zoneId()});
    ++revision_;
    context.services().get<SpatialIndexService>().track(zone.entity(), "zone",
                                                        zone.boundingRadius(), false);
}

void ZoneService::remove(GameContext &context, EntityId zone) {
    records_.erase(std::remove_if(records_.begin(), records_.end(),
                                  [&](const Record &record) { return record.id == zone; }),
                   records_.end());
    ++revision_;
    occupants_.erase(zone);
    for (auto &[actor, zones] : inside_) {
        (void)actor;
        zones.erase(std::remove(zones.begin(), zones.end(), zone), zones.end());
    }
    if (auto *spatial = context.services().find<SpatialIndexService>())
        spatial->untrack(zone, "zone");
}

void ZoneService::onShutdown(GameContext &) {
    records_.clear();
    occupants_.clear();
    inside_.clear();
}

std::vector<Zone *> ZoneService::at(GameContext &context, Vec2 point, int level) const {
    std::vector<Zone *> found;
    for (const SpatialHash::Hit &hit :
         context.services().get<SpatialIndexService>().near("zone", point, 0.0F, level)) {
        Entity *entity = context.scene().find(hit.id);
        Zone *zone = entity ? entity->get<Zone>() : nullptr;
        if (!zone || !zone->enabled || !entity->activeInHierarchy() || !zone->contains(point))
            continue;
        found.push_back(zone);
    }
    std::stable_sort(found.begin(), found.end(), [](const Zone *a, const Zone *b) {
        if (a->priority != b->priority)
            return a->priority > b->priority;
        return a->boundingRadius() < b->boundingRadius();
    });
    return found;
}

Zone *ZoneService::zoneAt(GameContext &context, Vec2 point, int level) const {
    const auto zones = at(context, point, level);
    return zones.empty() ? nullptr : zones.front();
}

Zone *ZoneService::roomAt(GameContext &context, Vec2 point, int level) const {
    for (Zone *zone : at(context, point, level))
        if (zone->room)
            return zone;
    return nullptr;
}

std::vector<Zone *> ZoneService::all(GameContext &context) const {
    std::vector<Zone *> list;
    for (const Record &record : records_) {
        Entity *entity = context.scene().find(record.id);
        Zone *zone = entity ? entity->get<Zone>() : nullptr;
        if (zone && zone->enabled && entity->activeInHierarchy())
            list.push_back(zone);
    }
    return list;
}

Zone *ZoneService::find(GameContext &context, std::string_view id) const {
    for (Zone *zone : all(context))
        if (zone->zoneId() == id)
            return zone;
    return nullptr;
}

std::vector<Zone *> ZoneService::rooms(GameContext &context) const {
    std::vector<Zone *> list;
    for (Zone *zone : all(context))
        if (zone->room)
            list.push_back(zone);
    return list;
}
std::vector<Zone *> ZoneService::withTag(GameContext &context, std::string_view tag) const {
    std::vector<Zone *> list;
    for (Zone *zone : all(context))
        if (zone->hasTag(tag))
            list.push_back(zone);
    return list;
}
std::vector<Zone *> ZoneService::withPurpose(GameContext &context, std::string_view purpose) const {
    std::vector<Zone *> list;
    for (Zone *zone : all(context))
        if (zone->hasPurpose(purpose))
            list.push_back(zone);
    return list;
}

std::vector<EntityId> ZoneService::occupants(EntityId zone) const {
    const auto found = occupants_.find(zone);
    return found == occupants_.end() ? std::vector<EntityId>{} : found->second;
}

std::vector<Zone *> ZoneService::zonesOf(GameContext &context, EntityId actor) const {
    std::vector<Zone *> list;
    const auto found = inside_.find(actor);
    if (found == inside_.end())
        return list;
    for (const EntityId id : found->second)
        if (Entity *entity = context.scene().find(id))
            if (Zone *zone = entity->get<Zone>())
                list.push_back(zone);
    return list;
}

void ZoneService::look(GameContext &context, EntityId actorId) {
    Entity *actor = context.scene().find(actorId);
    std::vector<EntityId> now;
    if (actor && actor->activeInHierarchy())
        for (const Zone *zone : at(context, actor->worldPosition(), std::max(levelOf(*actor), 0)))
            now.push_back(zone->entity().id());
    std::vector<EntityId> &before = inside_[actorId];
    const std::string who =
        actor && actor->get<Identity>() ? actor->get<Identity>()->id : std::string();
    const auto announce = [&](const char *event, EntityId zoneId, bool allowed) {
        Entity *zoneEntity = context.scene().find(zoneId);
        const Zone *zone = zoneEntity ? zoneEntity->get<Zone>() : nullptr;
        if (!zone)
            return;
        Json data = Json::object();
        data.set("zone", zone->zoneId());
        data.set("name", zone->label());
        data.set("room", zone->room);
        data.set("allowed", allowed);
        data.set("actor", who);
        if (std::string(event) == "zone.trespass" && !zone->trespassViolation.empty())
            data.set("violation", zone->trespassViolation);
        context.events().emit(GameEvent(event, zoneId, actorId, std::move(data)));
    };
    for (const EntityId zoneId : before)
        if (std::find(now.begin(), now.end(), zoneId) == now.end()) {
            auto &list = occupants_[zoneId];
            list.erase(std::remove(list.begin(), list.end(), actorId), list.end());
            announce("zone.exited", zoneId, true);
        }
    for (const EntityId zoneId : now)
        if (std::find(before.begin(), before.end(), zoneId) == before.end()) {
            occupants_[zoneId].push_back(actorId);
            bool allowed = true;
            Entity *zoneEntity = context.scene().find(zoneId);
            const Zone *zone = zoneEntity ? zoneEntity->get<Zone>() : nullptr;
            if (zone && actor)
                allowed = zone->allows(context, *actor);
            announce("zone.entered", zoneId, allowed);
            if (!allowed && zone && zone->enforce)
                announce("zone.trespass", zoneId, false);
        }
    before = std::move(now);
    if (before.empty() && !actor)
        inside_.erase(actorId);
}

void ZoneService::forget(GameContext &context, EntityId actor) {
    const auto found = inside_.find(actor);
    if (found == inside_.end())
        return;
    const std::vector<EntityId> zones = found->second;
    inside_.erase(found);
    for (const EntityId zoneId : zones) {
        auto &list = occupants_[zoneId];
        list.erase(std::remove(list.begin(), list.end(), actor), list.end());
        if (Entity *zoneEntity = context.scene().find(zoneId))
            if (const Zone *zone = zoneEntity->get<Zone>()) {
                Json data = Json::object();
                data.set("zone", zone->zoneId());
                data.set("name", zone->label());
                data.set("room", zone->room);
                data.set("allowed", true);
                context.events().emit(GameEvent("zone.exited", zoneId, actor, std::move(data)));
            }
    }
}

void ZoneService::refresh(GameContext &context) {
    for (const EntityId actor : context.services().get<ActorService>().all())
        look(context, actor);
}

void ZoneService::onFixedUpdate(GameContext &context, float seconds) {
    const auto &actors = context.services().get<ActorService>().all();
    if (actors.empty() || records_.empty())
        return;
    carry_ += seconds * static_cast<float>(actors.size()) / lookInterval;
    std::size_t count = static_cast<std::size_t>(carry_);
    carry_ -= static_cast<float>(count);
    count = std::min(count, actors.size());
    for (std::size_t i = 0; i < count; ++i)
        look(context, actors[(cursor_ + i) % actors.size()]);
    cursor_ = (cursor_ + count) % std::max<std::size_t>(actors.size(), 1);
}

void ZoneService::describe(std::vector<std::pair<std::string, std::string>> &rows) const {
    rows.push_back({"Zones", std::to_string(records_.size())});
    rows.push_back({"Characters placed", std::to_string(inside_.size())});
}

// ---- Destinations
// ------------------------------------------------------------------------------------
std::optional<ZoneService::Place>
ZoneService::destinationFor(GameContext &context, const ScheduleDestination &destination,
                            const Entity &actor, Rng &rng) const {
    using Kind = ScheduleDestination::Kind;
    const int actorLevel = std::max(levelOf(actor), 0);
    const auto inZone = [&](const Zone &zone) {
        return Place{zone.pointInside(rng), zone.level(), zone.entity().id()};
    };
    switch (destination.kind) {
    case Kind::None:
        return std::nullopt;
    case Kind::Point:
        return Place{destination.point, actorLevel, {}};
    case Kind::Entity: {
        RuleContext rc(context);
        rc.self = actor.id();
        rc.actor = actor.id();
        const Entity *target = rc.resolveOne(destination.name);
        if (!target)
            return std::nullopt;
        return Place{target->worldPosition(), std::max(levelOf(*target), 0), {}};
    }
    case Kind::Zone:
    case Kind::Room: {
        const Zone *zone = find(context, destination.name);
        if (!zone || (destination.kind == Kind::Room && !zone->room))
            return std::nullopt;
        return inZone(*zone);
    }
    case Kind::Purpose: {
        const Zone *best = nullptr;
        float bestDistance = 0.0F;
        bool bestFree = false;
        for (const Zone *zone : withPurpose(context, destination.name)) {
            const std::vector<EntityId> inside = occupants(zone->entity().id());
            const int others = static_cast<int>(std::count_if(
                inside.begin(), inside.end(), [&](const EntityId id) { return id != actor.id(); }));
            const bool free = zone->capacity <= 0 || others < zone->capacity;
            const float away = distance(zone->center(), actor.worldPosition()) +
                               100.0F * static_cast<float>(std::abs(zone->level() - actorLevel));
            if (!best || (free && !bestFree) || (free == bestFree && away < bestDistance)) {
                best = zone;
                bestDistance = away;
                bestFree = free;
            }
        }
        if (!best)
            return std::nullopt;
        return inZone(*best);
    }
    case Kind::Home: {
        const auto *who = actor.get<Identity>();
        const std::string home = who ? who->data.get("home").asString() : std::string();
        if (!home.empty())
            if (const Zone *zone = find(context, home))
                return inZone(*zone);
        if (who)
            return Place{who->homePosition(), actorLevel, {}};
        return std::nullopt;
    }
    }
    return std::nullopt;
}

bool ZoneService::isAt(GameContext &context, const ScheduleDestination &destination,
                       const Entity &actor) const {
    using Kind = ScheduleDestination::Kind;
    const auto zones = zonesOf(context, actor.id());
    const auto near = [&](Vec2 point, float radius) {
        return distance(point, actor.worldPosition()) <= radius;
    };
    switch (destination.kind) {
    case Kind::None:
        return true;
    case Kind::Point:
        return near(destination.point, 1.0F);
    case Kind::Entity: {
        RuleContext rc(context);
        rc.self = actor.id();
        rc.actor = actor.id();
        const Entity *target = rc.resolveOne(destination.name);
        return target && near(target->worldPosition(), 1.5F);
    }
    case Kind::Zone:
    case Kind::Room:
        return std::any_of(zones.begin(), zones.end(), [&](const Zone *zone) {
            return zone->zoneId() == destination.name &&
                   (destination.kind == Kind::Zone || zone->room);
        });
    case Kind::Purpose:
        return std::any_of(zones.begin(), zones.end(),
                           [&](const Zone *zone) { return zone->hasPurpose(destination.name); });
    case Kind::Home: {
        const auto *who = actor.get<Identity>();
        const std::string home = who ? who->data.get("home").asString() : std::string();
        if (!home.empty())
            return std::any_of(zones.begin(), zones.end(),
                               [&](const Zone *zone) { return zone->zoneId() == home; });
        return who && near(who->homePosition(), 1.0F);
    }
    }
    return false;
}

// ---- Rules
// ---------------------------------------------------------------------------------------
namespace {
std::optional<Value> zoneFact(RuleContext &context, Entity *subject, std::string_view rest) {
    if (!subject)
        return std::nullopt;
    const ZoneService &service = context.game.services().get<ZoneService>();
    const auto zones = service.zonesOf(context.game, subject->id());
    std::vector<const Zone *> sorted(zones.begin(), zones.end());
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const Zone *a, const Zone *b) { return a->priority > b->priority; });
    const Zone *top = sorted.empty() ? nullptr : sorted.front();
    if (rest == "id")
        return Value{top ? top->zoneId() : std::string()};
    if (rest == "name")
        return Value{top ? top->label() : std::string()};
    if (rest == "count")
        return Value{static_cast<std::int64_t>(zones.size())};
    if (rest == "room" || rest == "roomName") {
        for (const Zone *zone : sorted)
            if (zone->room)
                return Value{rest == "room" ? zone->zoneId() : zone->label()};
        return Value{std::string()};
    }
    if (rest == "purpose")
        return Value{top && !top->purposes.empty() ? top->purposes.front() : std::string()};
    if (rest == "restricted") {
        for (const Zone *zone : sorted)
            if (zone->restricted() && !zone->allows(context.game, *subject))
                return Value{true};
        return Value{false};
    }
    if (rest.starts_with("env.")) {
        for (const Zone *zone : sorted)
            if (zone->environment.isObject() && zone->environment.contains(rest.substr(4))) {
                return Value{zone->environmentValue(rest.substr(4))};
            }
        return Value{0.0};
    }
    return std::nullopt;
}
} // namespace

void registerZoneRules(RuleCatalog &catalog) {
    using Kind = ParamSpec::Kind;
    const auto param = [](const char *name, Kind kind, bool required = false,
                          const char *description = "") {
        return ParamSpec::make(name, kind, required, description);
    };
    catalog.addFacts("zone", zoneFact, true);
    catalog.addPredicate(
        {"InZone",
         "Zones",
         "True when the entity is inside a zone with this id, tag or purpose (or any zone, or a "
         "room, when none is given).",
         {param("zone", Kind::String), param("tag", Kind::String), param("purpose", Kind::String),
          param("room", Kind::Bool, false, "inside a room"),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             const Entity *who = context.resolveOne(
                 args.contains("entity") ? args.get("entity").asString() : "actor");
             if (!who)
                 return false;
             for (const Zone *zone :
                  context.game.services().get<ZoneService>().zonesOf(context.game, who->id())) {
                 if (args.contains("zone") && zone->zoneId() != args.get("zone").asString())
                     continue;
                 if (args.contains("tag") && !zone->hasTag(args.get("tag").asString()))
                     continue;
                 if (args.contains("purpose") && !zone->hasPurpose(args.get("purpose").asString()))
                     continue;
                 if (args.get("room").asBool(false) && !zone->room)
                     continue;
                 return true;
             }
             return false;
         },
         nullptr});
    catalog.addPredicate(
        {"ZoneAllows",
         "Zones",
         "True when the zone lets the entity be there (its factions, roles and access condition).",
         {param("zone", Kind::String, true),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             const Entity *who = context.resolveOne(
                 args.contains("entity") ? args.get("entity").asString() : "actor");
             const Zone *zone = context.game.services().get<ZoneService>().find(
                 context.game, args.get("zone").asString());
             return who && zone && zone->allows(context.game, *who);
         },
         nullptr});
    catalog.addPredicate(
        {"ZoneOccupied",
         "Zones",
         "True when at least `atLeast` characters (of the faction or role, if given) are in the "
         "zone.",
         {param("zone", Kind::String, true), param("atLeast", Kind::Int, false, "default 1"),
          param("faction", Kind::Ref, false), param("role", Kind::String)},
         [](const Json &args, RuleContext &context) {
             const ZoneService &service = context.game.services().get<ZoneService>();
             const Zone *zone = service.find(context.game, args.get("zone").asString());
             if (!zone)
                 return false;
             int count = 0;
             for (const EntityId id : service.occupants(zone->entity().id())) {
                 const Entity *who = context.game.scene().find(id);
                 const auto *identity = who ? who->get<Identity>() : nullptr;
                 if (!who)
                     continue;
                 if (args.contains("faction") &&
                     (!identity || identity->faction != args.get("faction").asString()))
                     continue;
                 if (args.contains("role") &&
                     (!identity || identity->role != args.get("role").asString()))
                     continue;
                 ++count;
             }
             return count >= static_cast<int>(args.get("atLeast").asInt(1));
         },
         nullptr});
}

void registerZoneComponents(ComponentRegistry &registry) {
    registerZoneRules(registry.extend<RuleCatalog>());
    registry.add<Zone>("Zone");
}
} // namespace yk
