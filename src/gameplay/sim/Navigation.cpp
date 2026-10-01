#include "yk/gameplay/Navigation.hpp"
#include "yk/core/Log.hpp"
#include "yk/gameplay/Exploration.hpp"
#include "yk/sim/Zones.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace yk {
namespace {
std::uint16_t fixedCost(float multiplier) {
    return static_cast<std::uint16_t>(std::clamp(std::lround(multiplier * 16.0F), 16L, 4095L));
}
// Seconds a trip over a link takes when the component does not say.
float defaultTraverse(nav::LinkKind kind) {
    switch (kind) {
    case nav::LinkKind::Stairs:
        return 0.5F;
    case nav::LinkKind::Ladder:
        return 1.2F;
    case nav::LinkKind::Vent:
        return 1.0F;
    case nav::LinkKind::Hole:
        return 0.6F;
    case nav::LinkKind::Elevator:
        return 1.5F;
    case nav::LinkKind::Drop:
        return 0.5F;
    case nav::LinkKind::Climb:
        return 1.0F;
    case nav::LinkKind::Teleport:
        return 0.0F;
    }
    return 0.5F;
}
Vec2 rightOf(Vec2 heading) {
    return {-heading.y, heading.x}; // Clockwise on a screen whose y points down.
}
} // namespace

const std::vector<std::string> &doorSourceNames() {
    static const std::vector<std::string> names{"StateGate", "Mechanism", "Manual"};
    return names;
}
const std::vector<std::string> &doorStateNames() {
    static const std::vector<std::string> names{"Open", "Closed", "Locked", "Sealed"};
    return names;
}
const char *navStatusName(NavStatus status) {
    switch (status) {
    case NavStatus::Idle:
        return "Idle";
    case NavStatus::Searching:
        return "Searching";
    case NavStatus::Moving:
        return "Moving";
    case NavStatus::Arrived:
        return "Arrived";
    case NavStatus::Failed:
        return "Failed";
    }
    return "Idle";
}

// ---- Settings, obstacles, doors, links ---------------------------------------------------------
void NavigationSettings::describe(TypeBuilder<NavigationSettings> &type) {
    type.category("Navigation")
        .description("Scene-wide navigation settings. Optional: without it the grid covers every "
                     "tile map at a third of its tile size.");
    type.field("cellSize", &NavigationSettings::cellSize)
        .range(0, 4, 0.05)
        .tooltip("Meters per navigation cell; 0 derives it (a third of the tile size). Corridors "
                 "and doors must be at least 2r+1 cells wide for an agent of r cells of radius.");
    type.field("boundsMin", &NavigationSettings::boundsMin).range(-100000, 100000);
    type.field("boundsMax", &NavigationSettings::boundsMax).range(-100000, 100000);
    type.field("padding", &NavigationSettings::padding).range(0, 100, 0.5);
    type.field("expansionsPerTick", &NavigationSettings::expansionsPerTick)
        .range(100, 1000000, 100)
        .tooltip("Path search budget for all agents together, per fixed tick.");
    type.field("areas", &NavigationSettings::areas)
        .tooltip("Navigation areas to register first, in order (area 1 is the first name).");
}

void NavigationObstacle::describe(TypeBuilder<NavigationObstacle> &type) {
    type.category("Navigation")
        .description("A footprint on the navigation grid that walkers cannot cross and/or sight "
                     "cannot pass: a crate, a counter, a closed shutter.");
    type.field("size", &NavigationObstacle::size).size().range(0, 1000, 0.05);
    type.field("offset", &NavigationObstacle::offset).offset();
    type.field("blocksMovement", &NavigationObstacle::blocksMovement);
    type.field("blocksSight", &NavigationObstacle::blocksSight);
    type.field("dynamic", &NavigationObstacle::dynamic)
        .tooltip("Moves around (a pushed crate): its footprint follows the entity every tick.");
}
void NavigationObstacle::onStart(GameContext &context) {
    auto &service = context.services().get<NavigationService>();
    service.obstacles_.push_back({entity().id(), {}, false, false, false});
}
void NavigationObstacle::onDestroy(GameContext &context) {
    if (auto *service = context.services().find<NavigationService>()) {
        for (auto &record : service->obstacles_)
            if (record.entity == entity().id() && record.applied) {
                if (record.movement)
                    service->world_.grid().addBlockers(record.cells, -1);
                if (record.sight)
                    service->world_.grid().addSightBlockers(record.cells, -1);
                record.applied = false;
            }
        std::erase_if(service->obstacles_,
                      [&](const auto &record) { return record.entity == entity().id(); });
    }
}

void NavigationDoor::describe(TypeBuilder<NavigationDoor> &type) {
    type.category("Navigation")
        .description("A door as agents see it: whether it is open, closed, locked or sealed, which "
                     "access classes pass it, and what waiting for it costs a path. Agents that "
                     "may use it open it on the way through.");
    type.field("size", &NavigationDoor::size).size().range(0, 1000, 0.05);
    type.field("offset", &NavigationDoor::offset).offset();
    type.field("source", &NavigationDoor::source)
        .options(doorSourceNames())
        .tooltip("Where the door's state comes from: its StateGate, its Door mechanism, or this "
                 "component (set by scripts, rules and security).");
    type.field("manualState", &NavigationDoor::manualState)
        .options(doorStateNames())
        .tooltip("The state when the source is Manual.");
    type.field("access", &NavigationDoor::access)
        .ref("access")
        .tooltip("Access classes (all of them) an agent must hold to pass; none: anyone.");
    type.field("openPenalty", &NavigationDoor::openPenalty).range(0, 100, 0.1);
    type.field("blocksSightWhenClosed", &NavigationDoor::blocksSightWhenClosed);
    type.field("autoCloseSeconds", &NavigationDoor::autoCloseSeconds)
        .range(0, 60, 0.1)
        .tooltip("A door an agent opened closes again after this long (0: stays open).");
    type.check([](const Entity &entity, const NavigationDoor &door, const CheckContext &,
                  std::vector<std::string> &problems) {
        if (door.source == DoorSource::StateGate && !entity.has<StateGate>())
            problems.push_back("takes its state from a StateGate, but this entity has none");
        if (door.source == DoorSource::Mechanism && !entity.has<Door>())
            problems.push_back("takes its state from a Door mechanism, but this entity has none");
    });
}
nav::DoorState NavigationDoor::state(GameContext &context) const {
    if (source == DoorSource::StateGate)
        if (const auto *gate = entity().get<StateGate>())
            return gate->open()
                       ? nav::DoorState::Open
                       : (gate->locked(context) ? nav::DoorState::Locked : nav::DoorState::Closed);
    if (source == DoorSource::Mechanism)
        if (const auto *door = entity().get<Door>())
            return door->openAmount() > 0.5F ? nav::DoorState::Open : nav::DoorState::Closed;
    return manualState;
}
bool NavigationDoor::fullyOpen(GameContext &context) const {
    if (state(context) != nav::DoorState::Open)
        return false;
    // A gate or a sliding door is open for the grid the moment it is asked, and for walking only
    // once its collider has moved out of the way.
    for (const Collider *collider : entity().getAll<Collider>())
        if (!collider->isTrigger && collider->enabled)
            return source == DoorSource::Manual;
    if (const auto *door = entity().get<Door>(); door && source == DoorSource::Mechanism)
        return door->openAmount() > 0.9F;
    return true;
}
bool NavigationDoor::open(GameContext &context, EntityId agent, std::uint64_t accessHeld,
                          std::uint64_t accessRequired) {
    (void)agent;
    const nav::DoorState now = state(context);
    if (now == nav::DoorState::Open) {
        openFor_ = std::max(openFor_, autoCloseSeconds);
        return true;
    }
    // A key unlocks a door this component holds the state of.
    if (now == nav::DoorState::Locked && source == DoorSource::Manual && accessRequired != 0 &&
        (accessHeld & accessRequired) == accessRequired) {
        manualState = nav::DoorState::Open;
        openedByAgent_ = true;
        openFor_ = autoCloseSeconds;
        return true;
    }
    if (now != nav::DoorState::Closed)
        return false;
    if (source == DoorSource::StateGate) {
        if (auto *gate = entity().get<StateGate>()) {
            gate->toggle(context);
            openedByAgent_ = true;
            openFor_ = autoCloseSeconds;
            return gate->open();
        }
    } else if (source == DoorSource::Manual) {
        manualState = nav::DoorState::Open;
        openedByAgent_ = true;
        openFor_ = autoCloseSeconds;
        return true;
    }
    return false; // A mechanism opens by its own signals.
}
void NavigationDoor::onStart(GameContext &context) {
    auto &service = context.services().get<NavigationService>();
    service.doors_.push_back({entity().id(), 0, nav::DoorState::Closed, false, {}});
}
void NavigationDoor::onDestroy(GameContext &context) {
    if (auto *service = context.services().find<NavigationService>()) {
        for (auto &record : service->doors_)
            if (record.entity == entity().id()) {
                if (record.sightApplied)
                    service->world_.grid().addSightBlockers(record.cells, -1);
                if (record.navId != 0)
                    service->world_.removeDoor(record.navId);
            }
        std::erase_if(service->doors_,
                      [&](const auto &record) { return record.entity == entity().id(); });
    }
}
void NavigationDoor::onFixedUpdate(GameContext &context, float seconds) {
    if (!openedByAgent_ || autoCloseSeconds <= 0.0F)
        return;
    // Held open while somebody is in the doorway.
    const auto *service = context.services().find<NavigationService>();
    if (service && service->doorUse_.contains(navId_))
        openFor_ = std::max(openFor_, 0.4F);
    openFor_ -= seconds;
    if (openFor_ > 0.0F)
        return;
    openedByAgent_ = false;
    if (source == DoorSource::StateGate) {
        if (auto *gate = entity().get<StateGate>())
            if (gate->open())
                gate->toggle(context);
    } else if (source == DoorSource::Manual && manualState == nav::DoorState::Open) {
        manualState = nav::DoorState::Closed;
    }
}

void NavigationLink::describe(TypeBuilder<NavigationLink> &type) {
    type.category("Navigation")
        .description("One end of a way between two places, usually on different levels: stairs, a "
                     "ladder, a vent, a hole, a drop, a lift. `target` names the other end.");
    type.field("kind", &NavigationLink::kind)
        .options({"Stairs", "Ladder", "Vent", "Hole", "Elevator", "Drop", "Climb", "Teleport"});
    type.field("target", &NavigationLink::target);
    type.field("bidirectional", &NavigationLink::bidirectional);
    type.field("cost", &NavigationLink::cost).range(0, 1000, 0.1);
    type.field("traverseSeconds", &NavigationLink::traverseSeconds)
        .range(0, 60, 0.1)
        .tooltip("How long the trip takes; 0 uses a time that fits the kind.");
    type.field("capabilities", &NavigationLink::capabilities)
        .ref("capability")
        .tooltip("What an agent must be able to do: climb, crawl, ...");
    type.field("access", &NavigationLink::access).ref("access");
    type.field("open", &NavigationLink::open)
        .tooltip("A closed link (a lift that is off, a hatch that is shut) cannot be used.");
    type.field("capacity", &NavigationLink::capacity).range(1, 32, 1);
    type.check([](const Entity &entity, const NavigationLink &link, const CheckContext &context,
                  std::vector<std::string> &problems) {
        if (context.prefab)
            return;
        const Entity *other = entity.scene().find(link.target);
        if (!link.target)
            problems.push_back("has no target: a link needs both ends");
        else if (!other)
            problems.push_back("its target is not in this scene");
        else if (other == &entity)
            problems.push_back("its target is itself");
    });
}
float NavigationLink::seconds() const {
    return traverseSeconds > 0.0F ? traverseSeconds : defaultTraverse(kind);
}
void NavigationLink::onStart(GameContext &context) {
    auto &service = context.services().get<NavigationService>();
    service.links_.push_back({entity().id(), 0});
}
void NavigationLink::onDestroy(GameContext &context) {
    if (auto *service = context.services().find<NavigationService>()) {
        for (const auto &record : service->links_)
            if (record.entity == entity().id() && record.navId != 0)
                service->world_.removeLink(record.navId);
        std::erase_if(service->links_,
                      [&](const auto &record) { return record.entity == entity().id(); });
    }
}

// ---- The service ------------------------------------------------------------------------------
std::uint64_t NavigationService::accessMask(const std::vector<std::string> &names) {
    std::uint64_t mask = 0;
    for (const std::string &name : names)
        if (const int bit = accessBit(name); bit >= 0)
            mask |= std::uint64_t{1} << static_cast<unsigned>(bit);
    return mask;
}
int NavigationService::accessBit(const std::string &name) {
    for (std::size_t i = 0; i < accessNames_.size(); ++i)
        if (accessNames_[i] == name)
            return static_cast<int>(i);
    if (accessNames_.size() >= 64) {
        log(LogLevel::Warning, "navigation", "More than 64 access classes; '" + name + "' ignored");
        return -1;
    }
    accessNames_.push_back(name);
    return static_cast<int>(accessNames_.size()) - 1;
}

CellRect NavigationService::footprint(const Entity &entity, Vec2 size, Vec2 offset) const {
    const Transform2D world = entity.worldTransform();
    const Vec2 center = transformPoint(world, offset);
    const Vec2 extent = hadamard(size, {std::fabs(world.scale.x), std::fabs(world.scale.y)}) * 0.5F;
    const float angle = degreesToRadians(world.rotationDegrees);
    // The axis-aligned box around the (possibly turned) rectangle.
    const Vec2 corners[4] = {
        rotated({-extent.x, -extent.y}, angle), rotated({extent.x, -extent.y}, angle),
        rotated({extent.x, extent.y}, angle), rotated({-extent.x, extent.y}, angle)};
    Vec2 low = center + corners[0], high = low;
    for (const Vec2 corner : corners) {
        low = {std::min(low.x, center.x + corner.x), std::min(low.y, center.y + corner.y)};
        high = {std::max(high.x, center.x + corner.x), std::max(high.y, center.y + corner.y)};
    }
    const WorldGrid &grid = world_.grid();
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    const float epsilon = 0.002F;
    grid.worldToCell(low + Vec2{epsilon, epsilon}, x0, y0);
    grid.worldToCell(high - Vec2{epsilon, epsilon}, x1, y1);
    int level = levelOf(entity);
    if (level < 0)
        level = 0;
    return {level, x0, y0, std::max(x0, x1), std::max(y0, y1)};
}

void NavigationService::recomputeCells(GameContext &context, int level, int minX, int minY,
                                       int maxX, int maxY) {
    WorldGrid &grid = world_.grid();
    const GridSpec &spec = grid.spec();
    minX = std::max(minX, 0);
    minY = std::max(minY, 0);
    maxX = std::min(maxX, spec.width - 1);
    maxY = std::min(maxY, spec.height - 1);
    if (level < 0 || level >= spec.levels || minX > maxX || minY > maxY)
        return;
    const WorldLevelSet &levels = context.scene().settings.levels;
    struct Source {
        Tilemap *map;
        std::shared_ptr<const Tileset> tiles;
    };
    std::vector<Source> sources;
    for (const TilemapRecord &record : tilemaps_)
        if (Entity *entity = context.scene().find(record.entity))
            if (auto *map = entity->get<Tilemap>(); map && !map->tileset.path.empty())
                if (auto tiles = context.tileset(map->tileset.path))
                    sources.push_back({map, tiles});
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x) {
            GridCell &cell = grid.at(level, x, y);
            cell.flags = 0;
            cell.area = 0;
            cell.cost = 16;
            cell.noiseDamping = 0;
            const Rect rect = grid.cellRect(x, y);
            const Vec2 cellMiddle = yk::center(rect);
            for (const Source &source : sources) {
                const Tilemap &map = *source.map;
                int tx = 0, ty = 0;
                map.worldToCell(cellMiddle, tx, ty);
                for (const TileLayer &layer : map.layers) {
                    const int layerLevel = levels.indexOf(layer.level);
                    if (layerLevel != everyLevel &&
                        (layerLevel == unknownLevel ? 0 : layerLevel) != level)
                        continue;
                    const std::int32_t value = layer.cell(tx, ty);
                    if (tile::empty(value))
                        continue;
                    const TileProperties &props = source.tiles->properties(tile::index(value));
                    bool covers = true;
                    if (props.collider) {
                        // A post or a thin wall blocks the nav cells it overlaps by a quarter or
                        // more.
                        const Vec2 corner = map.cellToWorld(tx, ty);
                        const Vec2 size = map.cellSizeInWorld();
                        const Rect box{corner + hadamard(props.collider->position, size),
                                       hadamard(props.collider->size, size)};
                        const float overlapX =
                            std::min(box.position.x + box.size.x, rect.position.x + rect.size.x) -
                            std::max(box.position.x, rect.position.x);
                        const float overlapY =
                            std::min(box.position.y + box.size.y, rect.position.y + rect.size.y) -
                            std::max(box.position.y, rect.position.y);
                        covers = overlapX > 0.0F && overlapY > 0.0F &&
                                 overlapX * overlapY >= 0.25F * rect.size.x * rect.size.y;
                    }
                    if (props.solid && layer.solid && covers)
                        cell.flags |= cellflag::solid;
                    if (props.opaque)
                        cell.flags |= cellflag::opaque;
                    if (!props.area.empty())
                        if (const int area = world_.areaId(props.area); area >= 0)
                            cell.area = static_cast<std::uint8_t>(area);
                    if (props.cost > 1.0F)
                        cell.cost = std::max(cell.cost, fixedCost(props.cost));
                    cell.noiseDamping =
                        std::max(cell.noiseDamping,
                                 static_cast<std::uint8_t>(
                                     std::clamp(props.noiseDamping, 0.0F, 1.0F) * 255.0F));
                }
            }
        }
    // Zones can make ground dearer or put it in a navigation area (a restricted yard that guards'
    // routes may avoid).
    for (const Zone *zone : context.services().get<ZoneService>().all(context)) {
        if (zone->level() != level ||
            (zone->navigationArea.empty() && !(zone->navigationCost > 1.0F)))
            continue;
        const int area = zone->navigationArea.empty() ? -1 : world_.areaId(zone->navigationArea);
        const Rect box = zone->bounds();
        int zx0 = 0, zy0 = 0, zx1 = 0, zy1 = 0;
        grid.worldToCell(box.position, zx0, zy0);
        grid.worldToCell(box.position + box.size, zx1, zy1);
        for (int y = std::max(zy0, minY); y <= std::min(zy1, maxY); ++y)
            for (int x = std::max(zx0, minX); x <= std::min(zx1, maxX); ++x) {
                if (!zone->contains(yk::center(grid.cellRect(x, y))))
                    continue;
                GridCell &cell = grid.at(level, x, y);
                if (area >= 0)
                    cell.area = static_cast<std::uint8_t>(area);
                if (zone->navigationCost > 1.0F)
                    cell.cost = std::max(cell.cost, fixedCost(zone->navigationCost));
            }
    }
    grid.markDirty({level, minX, minY, maxX, maxY});
}

void NavigationService::rebuildStatic(GameContext &context) {
    Scene &scene = context.scene();
    const NavigationSettings *settings = nullptr;
    tilemaps_.clear();
    float smallestTile = 0.0F;
    bool any = false;
    Vec2 low{}, high{};
    const auto include = [&](Vec2 point) {
        low = any ? Vec2{std::min(low.x, point.x), std::min(low.y, point.y)} : point;
        high = any ? Vec2{std::max(high.x, point.x), std::max(high.y, point.y)} : point;
        any = true;
    };
    for (Entity *entity : scene.orderedEntities()) {
        if (!entity->activeInHierarchy())
            continue;
        if (!settings)
            settings = entity->get<NavigationSettings>();
        if (auto *map = entity->get<Tilemap>()) {
            tilemaps_.push_back({entity->id(), map->sequence()});
            const Vec2 size = map->cellSizeInWorld();
            smallestTile = smallestTile > 0.0F ? std::min(smallestTile, std::min(size.x, size.y))
                                               : std::min(size.x, size.y);
            for (const TileLayer &layer : map->layers) {
                int minX = 0, minY = 0, maxX = 0, maxY = 0;
                if (layer.bounds(minX, minY, maxX, maxY)) {
                    include(map->cellToWorld(minX, minY));
                    include(map->cellToWorld(maxX + 1, maxY + 1));
                }
            }
        }
        for (const NavigationObstacle *obstacle : entity->getAll<NavigationObstacle>()) {
            (void)obstacle;
            include(entity->worldPosition());
        }
        if (entity->has<NavigationDoor>() || entity->has<NavigationLink>())
            include(entity->worldPosition());
    }
    float cell = settings && settings->cellSize > 0.0F ? settings->cellSize
                 : smallestTile > 0.0F                 ? smallestTile / 3.0F
                                                       : 0.5F;
    cell = std::clamp(cell, 0.05F, 4.0F);
    if (settings && settings->boundsMin != settings->boundsMax) {
        include(settings->boundsMin);
        include(settings->boundsMax);
    }
    if (!any) {
        low = {-16.0F, -16.0F};
        high = {16.0F, 16.0F};
    }
    const float padding = settings ? settings->padding : 2.0F;
    low -= Vec2{padding, padding};
    high += Vec2{padding, padding};
    const int levels = scene.settings.levels.count();
    // Keep the grid within a size that fits in memory (about 64 million cells over all levels).
    constexpr double maxCells = 64.0e6;
    while ((static_cast<double>(high.x - low.x) / static_cast<double>(cell)) *
               (static_cast<double>(high.y - low.y) / static_cast<double>(cell)) * levels >
           maxCells) {
        cell *= 1.5F;
        log(LogLevel::Warning, "navigation",
            "The navigation grid is too large; cell size raised to " + std::to_string(cell) + " m");
    }
    GridSpec spec;
    spec.origin = low;
    spec.cellSize = cell;
    spec.width = static_cast<int>(std::ceil((high.x - low.x) / cell));
    spec.height = static_cast<int>(std::ceil((high.y - low.y) / cell));
    spec.levels = levels;
    world_.configure(spec);
    expansionsPerTick_ = settings ? std::max(100, settings->expansionsPerTick) : 6000;
    if (settings)
        for (const std::string &name : settings->areas)
            world_.areaId(name);
    zoneRevision_ = context.services().get<ZoneService>().revision();
    for (int level = 0; level < spec.levels; ++level)
        recomputeCells(context, level, 0, 0, spec.width - 1, spec.height - 1);
    // Everything that lived in the old grid is applied again.
    for (ObstacleRecord &record : obstacles_)
        record.applied = false;
    for (DoorRecord &record : doors_) {
        record.navId = 0;
        record.sightApplied = false;
    }
    for (LinkRecord &record : links_)
        record.navId = 0;
    cache_.clear();
    finished_.clear();
    built_ = true;
}

void NavigationService::rebuild(GameContext &context) {
    rebuildStatic(context);
    syncObstacles(context);
    syncDoors(context);
    syncLinks(context);
    world_.refresh();
}
void NavigationService::ensureBuilt(GameContext &context) {
    if (!built_)
        rebuild(context);
}

void NavigationService::syncTilemaps(GameContext &context) {
    for (TilemapRecord &record : tilemaps_) {
        Entity *entity = context.scene().find(record.entity);
        auto *map = entity ? entity->get<Tilemap>() : nullptr;
        if (!map || map->sequence() == record.sequence)
            continue;
        bool complete = false;
        const auto changes = map->changesSince(record.sequence, complete);
        const WorldLevelSet &levels = context.scene().settings.levels;
        if (!complete) {
            for (int level = 0; level < world_.grid().spec().levels; ++level)
                recomputeCells(context, level, 0, 0, world_.grid().spec().width - 1,
                               world_.grid().spec().height - 1);
        } else {
            for (const TileChange &change : changes) {
                if (change.layer < 0 ||
                    static_cast<std::size_t>(change.layer) >= map->layers.size())
                    continue;
                const int layerLevel =
                    levels.indexOf(map->layers[static_cast<std::size_t>(change.layer)].level);
                const Vec2 corner = map->cellToWorld(change.x, change.y);
                const Vec2 size = map->cellSizeInWorld();
                int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                world_.grid().worldToCell(corner + Vec2{0.002F, 0.002F}, x0, y0);
                world_.grid().worldToCell(corner + size - Vec2{0.002F, 0.002F}, x1, y1);
                for (int level = 0; level < world_.grid().spec().levels; ++level)
                    if (layerLevel == everyLevel ||
                        (layerLevel == unknownLevel ? 0 : layerLevel) == level)
                        recomputeCells(context, level, x0, y0, x1, y1);
            }
        }
        record.sequence = map->sequence();
    }
}

void NavigationService::syncObstacles(GameContext &context) {
    for (ObstacleRecord &record : obstacles_) {
        Entity *entity = context.scene().find(record.entity);
        const auto *obstacle = entity ? entity->get<NavigationObstacle>() : nullptr;
        const bool live = obstacle && obstacle->enabled && entity->activeInHierarchy();
        const bool wantMove = live && obstacle->blocksMovement;
        const bool wantSight = live && obstacle->blocksSight;
        CellRect rect = record.cells;
        if (obstacle && (obstacle->dynamic || !record.applied)) {
            rect = footprint(*entity, obstacle->size, obstacle->offset);
            if (obstacle->dynamic && record.applied && rect.minX == record.cells.minX &&
                rect.minY == record.cells.minY && rect.maxX == record.cells.maxX &&
                rect.maxY == record.cells.maxY && rect.level == record.cells.level)
                rect = record.cells;
        }
        const bool sameRect = record.applied && rect.level == record.cells.level &&
                              rect.minX == record.cells.minX && rect.minY == record.cells.minY &&
                              rect.maxX == record.cells.maxX && rect.maxY == record.cells.maxY;
        if (record.applied && sameRect && wantMove == record.movement && wantSight == record.sight)
            continue;
        if (record.applied) {
            if (record.movement)
                world_.grid().addBlockers(record.cells, -1);
            if (record.sight)
                world_.grid().addSightBlockers(record.cells, -1);
            record.applied = false;
        }
        if (!obstacle)
            continue;
        record.cells = rect;
        if (wantMove)
            world_.grid().addBlockers(rect, +1);
        if (wantSight)
            world_.grid().addSightBlockers(rect, +1);
        record.movement = wantMove;
        record.sight = wantSight;
        record.applied = true;
    }
}

void NavigationService::syncDoors(GameContext &context) {
    for (DoorRecord &record : doors_) {
        Entity *entity = context.scene().find(record.entity);
        auto *door = entity ? entity->get<NavigationDoor>() : nullptr;
        if (!door)
            continue;
        const nav::DoorState state = door->state(context);
        if (record.navId == 0) {
            record.cells = footprint(*entity, door->size, door->offset);
            nav::DoorDef def;
            def.state = state;
            def.access = accessMask(door->access);
            def.penalty = door->openPenalty;
            def.owner = entity->id();
            record.navId = world_.addDoor(def, record.cells);
            record.applied = state;
            door->navId_ = record.navId;
            if (door->blocksSightWhenClosed && state != nav::DoorState::Open) {
                world_.grid().addSightBlockers(record.cells, +1);
                record.sightApplied = true;
            }
            continue;
        }
        if (state != record.applied) {
            world_.setDoorState(record.navId, state);
            record.applied = state;
        }
        world_.setDoorAccess(record.navId, accessMask(door->access));
        const bool wantSight = door->blocksSightWhenClosed && state != nav::DoorState::Open;
        if (wantSight != record.sightApplied) {
            world_.grid().addSightBlockers(record.cells, wantSight ? +1 : -1);
            record.sightApplied = wantSight;
        }
    }
}

void NavigationService::syncLinks(GameContext &context) {
    for (LinkRecord &record : links_) {
        Entity *entity = context.scene().find(record.entity);
        auto *link = entity ? entity->get<NavigationLink>() : nullptr;
        const Entity *other = link ? context.scene().find(link->target) : nullptr;
        if (!link || !other)
            continue;
        nav::LinkDef def;
        def.kind = link->kind;
        def.fromLevel = std::max(levelOf(*entity), 0);
        def.from = entity->worldPosition();
        def.toLevel = std::max(levelOf(*other), 0);
        def.to = other->worldPosition();
        def.bidirectional = link->bidirectional;
        def.cost = link->cost;
        def.capabilities = world_.capabilityMask(link->capabilities);
        def.access = accessMask(link->access);
        def.open = link->open;
        def.capacity = link->capacity;
        def.owner = entity->id();
        if (record.navId == 0) {
            record.navId = world_.addLink(def);
            link->navId_ = record.navId;
        } else if (const nav::LinkDef *current = world_.link(record.navId);
                   current &&
                   !(current->kind == def.kind && current->fromLevel == def.fromLevel &&
                     current->from == def.from && current->toLevel == def.toLevel &&
                     current->to == def.to && current->open == def.open &&
                     current->cost == def.cost && current->capacity == def.capacity &&
                     current->capabilities == def.capabilities && current->access == def.access &&
                     current->bidirectional == def.bidirectional)) {
            world_.replaceLink(record.navId, def);
        }
    }
}

std::uint64_t NavigationService::cacheKey(const nav::PathQuery &query) const {
    const WorldGrid &grid = world_.grid();
    int sx = 0, sy = 0, gx = 0, gy = 0;
    grid.worldToCell(query.start, sx, sy);
    grid.worldToCell(query.goal, gx, gy);
    std::uint64_t h = query.profile.hash();
    const auto mix = [&](std::uint64_t value) {
        h ^= value + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };
    mix(static_cast<std::uint64_t>(query.startLevel) * 7919U +
        static_cast<std::uint64_t>(sx) * 104729U + static_cast<std::uint64_t>(sy) * 1299709U);
    mix(static_cast<std::uint64_t>(query.goalLevel) * 7919U +
        static_cast<std::uint64_t>(gx) * 104729U + static_cast<std::uint64_t>(gy) * 1299709U);
    mix(static_cast<std::uint64_t>(std::lround(query.tolerance * 100.0F)));
    return h;
}

NavigationService::RequestId NavigationService::requestPath(EntityId requester,
                                                            const nav::PathQuery &query) {
    ++counters_.requests;
    Request request;
    request.id = nextRequest_++;
    request.requester = requester;
    request.query = query;
    queue_.push_back(std::move(request));
    counters_.queueHighWater = std::max(counters_.queueHighWater, static_cast<int>(queue_.size()));
    return queue_.back().id;
}
bool NavigationService::take(RequestId id, nav::NavPath &path) {
    const auto found = finished_.find(id);
    if (found == finished_.end())
        return false;
    path = std::move(found->second);
    finished_.erase(found);
    return true;
}
void NavigationService::cancel(RequestId id) {
    for (Request &request : queue_)
        if (request.id == id)
            request.cancelled = true;
    finished_.erase(id);
}

void NavigationService::onFixedUpdate(GameContext &context, float seconds) {
    clock_ += static_cast<double>(seconds);
    ensureBuilt(context);
    if (zoneRevision_ != context.services().get<ZoneService>().revision())
        rebuild(context); // Zones came or went since the grid was made.
    syncTilemaps(context);
    syncObstacles(context);
    syncDoors(context);
    syncLinks(context);
    world_.refresh();
    if (cacheRevision_ != world_.revision()) { // The world changed: remembered paths may be wrong.
        cache_.clear();
        cacheRevision_ = world_.revision();
    }
    // Spend this tick's budget on the oldest requests. A search that does not finish carries on
    // next tick; nothing ever runs a whole large search inside one tick.
    int budget = expansionsPerTick_;
    counters_.expansionsLastTick = 0;
    while (budget > 0 && (world_.searching() || !queue_.empty())) {
        if (!world_.searching()) {
            Request request = std::move(queue_.front());
            queue_.pop_front();
            if (request.cancelled)
                continue;
            request.cacheKey = cacheKey(request.query); // The grid exists by now.
            if (const auto hit = cache_.find(request.cacheKey); hit != cache_.end()) {
                nav::NavPath path = hit->second;
                if (!path.points.empty()) {
                    path.points.front().position = request.query.start;
                    path.points.front().level = request.query.startLevel;
                }
                finished_[request.id] = std::move(path);
                ++counters_.cacheHits;
                continue;
            }
            world_.beginSearch(request.query);
            activeRequest_ = std::move(request);
            ++counters_.searches;
        }
        const int before = world_.activeExpansions();
        const bool done = world_.stepSearch(budget);
        const int spent = std::max(world_.activeExpansions() - before, 1);
        budget -= spent;
        counters_.expansionsLastTick += spent;
        if (done) {
            nav::NavPath path = world_.finishSearch();
            if (path.status == nav::PathStatus::Found ||
                path.status == nav::PathStatus::Unreachable)
                cache_[activeRequest_.cacheKey] = path;
            if (cache_.size() > 2048)
                cache_.clear();
            if (!activeRequest_.cancelled)
                finished_[activeRequest_.id] = std::move(path);
            activeRequest_ = Request{};
        }
    }
    // Results nobody collected (the agent was destroyed) do not pile up.
    if (finished_.size() > 4096)
        finished_.clear();
}

void NavigationService::describe(std::vector<std::pair<std::string, std::string>> &rows) const {
    const WorldGrid &grid = world_.grid();
    rows.push_back({"Grid", std::to_string(grid.spec().width) + " x " +
                                std::to_string(grid.spec().height) + " x " +
                                std::to_string(grid.spec().levels) + " levels, " +
                                std::to_string(grid.spec().cellSize) + " m cells"});
    rows.push_back(
        {"Doors / links", std::to_string(doors_.size()) + " / " + std::to_string(links_.size())});
    rows.push_back({"Requests", std::to_string(counters_.requests) + " (" +
                                    std::to_string(counters_.cacheHits) + " from cache), queued " +
                                    std::to_string(queue_.size())});
    rows.push_back(
        {"Search", std::to_string(counters_.expansionsLastTick) + " expansions last tick"});
}

// ---- Agents' view of each other, doors and links
// -------------------------------------------------
void NavigationService::publishAgent(EntityId id, Vec2 position, Vec2 heading, int level) {
    agents_.update(id, position, 0.0F, level);
    agentState_[id] = {position, heading};
}
void NavigationService::forgetAgent(EntityId id) {
    agents_.remove(id);
    agentState_.erase(id);
}
std::vector<NavigationService::Neighbor>
NavigationService::neighbors(Vec2 position, float radius, int level, EntityId except) const {
    std::vector<Neighbor> result;
    for (const auto &hit : agents_.queryCircle(position, radius, level)) {
        if (hit.id == except)
            continue;
        const auto state = agentState_.find(hit.id);
        if (state != agentState_.end())
            result.push_back({hit.id, state->second.first, state->second.second});
    }
    return result;
}

NavigationDoor *NavigationService::doorById(GameContext &context, std::uint16_t navId) {
    for (const DoorRecord &record : doors_)
        if (record.navId == navId)
            if (Entity *entity = context.scene().find(record.entity))
                return entity->get<NavigationDoor>();
    return nullptr;
}
bool NavigationService::claimDoor(std::uint16_t navId, EntityId agent, Vec2 direction, double now) {
    const auto use = doorUse_.find(navId);
    if (use == doorUse_.end() || use->second.agent == agent || now - use->second.since > 4.0) {
        doorUse_[navId] = {agent, direction, now};
        return true;
    }
    // Following somebody through in the same direction is fine; meeting them is not.
    return dot(use->second.direction, direction) > 0.0F && now - use->second.since < 1.5;
}
void NavigationService::releaseDoor(std::uint16_t navId, EntityId agent) {
    const auto use = doorUse_.find(navId);
    if (use != doorUse_.end() && use->second.agent == agent)
        doorUse_.erase(use);
}
bool NavigationService::claimLink(std::uint32_t linkId, EntityId agent) {
    auto &users = linkUse_[linkId];
    if (std::find(users.begin(), users.end(), agent) != users.end())
        return true;
    const nav::LinkDef *link = world_.link(linkId);
    const std::size_t capacity = link ? static_cast<std::size_t>(std::max(link->capacity, 1)) : 1;
    if (users.size() >= capacity)
        return false;
    users.push_back(agent);
    return true;
}
void NavigationService::releaseLink(std::uint32_t linkId, EntityId agent) {
    if (auto use = linkUse_.find(linkId); use != linkUse_.end())
        std::erase(use->second, agent);
}

// ---- NavigationAgent --------------------------------------------------------------------------
void NavigationAgent::describe(TypeBuilder<NavigationAgent> &type) {
    type.category("Navigation")
        .description("Walks an entity to a destination: asks the navigation service for a path, "
                     "follows it, opens doors, takes stairs and vents, steers around other agents "
                     "and reports arrival or why it could not. Steers through CharacterMotor.")
        .updatePhase(UpdatePhase::Steering)
        .dependsOn("CharacterMotor")
        .dependsOn("WorldLayer");
    type.field("radius", &NavigationAgent::radius)
        .range(0.05, 3, 0.01)
        .tooltip("Body radius: decides which gaps it fits through.");
    type.field("footOffset", &NavigationAgent::footOffset)
        .offset()
        .tooltip("Where its feet are, from the entity's position.");
    type.field("arriveDistance", &NavigationAgent::arriveDistance).range(0.02, 5, 0.01);
    type.field("stopDistance", &NavigationAgent::stopDistance).range(0.02, 10, 0.01);
    type.field("repathInterval", &NavigationAgent::repathInterval).range(0.05, 10, 0.05);
    type.field("avoidanceRadius", &NavigationAgent::avoidanceRadius).range(0, 10, 0.05);
    type.field("avoidanceStrength", &NavigationAgent::avoidanceStrength).range(0, 5, 0.05);
    type.field("run", &NavigationAgent::run);
    type.field("capabilities", &NavigationAgent::capabilities)
        .ref("capability")
        .tooltip("What it can do: walk, doors, climb, drop, crawl, ...");
    type.field("forbiddenAreas", &NavigationAgent::forbiddenAreas)
        .ref("navarea")
        .tooltip("Navigation areas it will not enter.");
    type.field("areaCosts", &NavigationAgent::areaCosts)
        .tooltip("name=multiplier entries (mud=2): ground it finds slower or avoids.");
    type.field("accessTokens", &NavigationAgent::accessTokens)
        .ref("access")
        .tooltip("Access classes it holds, so it can use the doors and links that need them.");
}

Vec2 NavigationAgent::navPosition() const {
    return entity().worldPosition() + footOffset;
}

nav::AgentProfile NavigationAgent::profile(GameContext &context) const {
    auto &service = context.services().get<NavigationService>();
    service.ensureBuilt(context);
    nav::NavigationWorld &world = service.world();
    nav::AgentProfile result;
    const float cell = world.grid().spec().cellSize;
    result.radiusCells =
        std::max(0, static_cast<int>(std::ceil((radius - cell * 0.5F) / cell - 1e-3F)));
    result.capabilities = world.capabilityMask(capabilities);
    result.access = service.accessMask(accessTokens);
    for (const std::string &name : forbiddenAreas)
        if (const int area = world.findArea(name); area >= 0)
            result.areaMask &= ~(1U << static_cast<unsigned>(area));
    for (const std::string &entry : areaCosts) {
        const auto equals = entry.find('=');
        if (equals == std::string::npos)
            continue;
        const int area = world.findArea(entry.substr(0, equals));
        char *end = nullptr;
        const float cost = std::strtof(entry.c_str() + equals + 1, &end);
        if (area >= 0 && end != entry.c_str() + equals + 1 && cost >= 1.0F)
            result.areaCost[static_cast<std::size_t>(area)] = cost;
    }
    return result;
}

void NavigationAgent::onStart(GameContext &context) {
    context.services().get<NavigationService>();
    progressPoint_ = navPosition();
}
void NavigationAgent::onDestroy(GameContext &context) {
    if (auto *service = context.services().find<NavigationService>()) {
        service->forgetAgent(entity().id());
        if (request_ != 0)
            service->cancel(request_);
        if (linkInUse_ != 0)
            service->releaseLink(linkInUse_, entity().id());
    }
}

bool NavigationAgent::moveTo(GameContext &context, Vec2 goal, int level, float tolerance) {
    if (!finite(goal))
        return false;
    goal_ = goal;
    goalLevel_ = level >= 0 ? level : std::max(levelOf(entity()), 0);
    tolerance_ = tolerance > 0.0F ? tolerance : stopDistance;
    followTarget_ = {};
    status_ = NavStatus::Searching;
    failure_ = nav::PathFailure::None;
    waited_ = 0.0F;
    repaths_ = 0;
    stuck_ = 0;
    path_ = {};
    requestPath(context);
    return true;
}
bool NavigationAgent::moveToEntity(GameContext &context, EntityId target, float tolerance) {
    const Entity *other = context.scene().find(target);
    if (!other)
        return false;
    if (!moveTo(context, other->worldPosition(), std::max(levelOf(*other), 0), tolerance))
        return false;
    followTarget_ = target;
    return true;
}
void NavigationAgent::stop(GameContext &context) {
    if (auto *service = context.services().find<NavigationService>()) {
        if (request_ != 0)
            service->cancel(request_);
        if (linkInUse_ != 0)
            service->releaseLink(linkInUse_, entity().id());
    }
    request_ = 0;
    linkInUse_ = 0;
    linkLeft_ = 0.0F;
    followTarget_ = {};
    status_ = NavStatus::Idle;
    path_ = {};
}

void NavigationAgent::requestPath(GameContext &context) {
    auto &service = context.services().get<NavigationService>();
    if (request_ != 0)
        service.cancel(request_);
    nav::PathQuery query;
    query.startLevel = std::max(levelOf(entity()), 0);
    query.start = navPosition();
    query.goalLevel = goalLevel_;
    query.goal = goal_;
    query.tolerance = std::max(tolerance_, 0.1F);
    query.profile = profile(context);
    request_ = service.requestPath(entity().id(), query);
    sinceRequest_ = 0.0F;
    ++repaths_;
}

void NavigationAgent::fail(GameContext &context, nav::PathFailure reason) {
    status_ = NavStatus::Failed;
    failure_ = reason;
    if (auto *service = context.services().find<NavigationService>())
        if (linkInUse_ != 0)
            service->releaseLink(linkInUse_, entity().id());
    linkInUse_ = 0;
    Json data = Json::object();
    data.set("reason", nav::pathFailureName(reason));
    context.events().emit(GameEvent("nav.failed", entity().id(), {}, std::move(data)));
}
void NavigationAgent::finish(GameContext &context) {
    status_ = NavStatus::Arrived;
    failure_ = nav::PathFailure::None;
    context.events().emit(GameEvent("nav.arrived", entity().id()));
}

Vec2 NavigationAgent::avoidance(GameContext &context, Vec2 position, Vec2 heading) const {
    if (avoidanceRadius <= 0.0F || avoidanceStrength <= 0.0F)
        return {};
    auto &service = context.services().get<NavigationService>();
    Vec2 push{};
    const int level = std::max(levelOf(entity()), 0);
    for (const auto &neighbor :
         service.neighbors(position, avoidanceRadius, level, entity().id())) {
        Vec2 away = position - neighbor.position;
        float gap = length(away);
        if (gap <
            1e-3F) { // On top of each other: split by id so they part in different directions.
            const float angle =
                static_cast<float>((entity().id().value ^ neighbor.id.value) % 360U);
            away = {std::cos(degreesToRadians(angle)), std::sin(degreesToRadians(angle))};
            gap = 1e-3F;
        }
        const float weight = 1.0F - std::min(gap / avoidanceRadius, 1.0F);
        push += normalized(away) * weight;
        // Meeting head on: both step to their right and pass.
        if (lengthSquared(heading) > 0.01F && dot(neighbor.heading, heading) < -0.3F)
            push += rightOf(heading) * (weight * 0.8F);
    }
    return push * avoidanceStrength;
}

void NavigationAgent::onFixedUpdate(GameContext &context, float seconds) {
    auto &service = context.services().get<NavigationService>();
    CharacterMotor *motor = entity().get<CharacterMotor>();
    const Vec2 position = navPosition();
    const int level = std::max(levelOf(entity()), 0);
    const Vec2 velocity = motor ? motor->velocity() : Vec2{};
    service.publishAgent(entity().id(), position, normalized(velocity), level);
    sinceRequest_ += seconds;
    if (status_ == NavStatus::Idle || status_ == NavStatus::Arrived || status_ == NavStatus::Failed)
        return;
    const auto stand = [&] {
        if (motor)
            motor->setIntent({}, 0.0F);
    };
    // A trip over a link: the agent is busy for its duration and then appears at the other end.
    if (linkInUse_ != 0) {
        stand();
        linkLeft_ -= seconds;
        waited_ += 0.0F;
        if (linkLeft_ > 0.0F)
            return;
        service.releaseLink(linkInUse_, entity().id());
        linkInUse_ = 0;
        context.teleport(entity(), linkExit_ - footOffset);
        if (auto *layer = entity().get<WorldLayer>(); layer && linkExitLevel_ != level) {
            const std::string id = context.scene().settings.levels.idOf(linkExitLevel_);
            if (!id.empty())
                layer->moveTo(context, id);
        }
        ++index_; // Past the exit point.
        progressPoint_ = linkExit_;
        progressTimer_ = 0.0F;
        return;
    }
    if (status_ == NavStatus::Searching) {
        stand();
        if (request_ != 0 && service.take(request_, path_)) {
            request_ = 0;
            pathRevision_ = service.world().revision();
            index_ = 1;
            progressPoint_ = position;
            progressTimer_ = 0.0F;
            if (path_.status == nav::PathStatus::Found ||
                path_.status == nav::PathStatus::Partial) {
                status_ = NavStatus::Moving;
                if (path_.points.size() <= 1 && path_.status == nav::PathStatus::Found)
                    finish(context);
            } else {
                fail(context, path_.failure == nav::PathFailure::None ? nav::PathFailure::NoRoute
                                                                      : path_.failure);
            }
        } else {
            waited_ += seconds;
        }
        return;
    }
    // Following something that moves: head for where it is now.
    if (followTarget_) {
        if (const Entity *target = context.scene().find(followTarget_)) {
            if (distance(target->worldPosition(), goal_) > 1.0F &&
                sinceRequest_ >= repathInterval) {
                goal_ = target->worldPosition();
                goalLevel_ = std::max(levelOf(*target), 0);
                requestPath(context);
                status_ = NavStatus::Searching;
                return;
            }
        }
    }
    // Is the way ahead still open? A door that locked or a crate that landed in a corridor makes
    // the path stale; one look ahead is enough to know whether to ask again.
    if (pathRevision_ != service.world().revision() && sinceRequest_ >= repathInterval) {
        const nav::AgentProfile mine = profile(context);
        bool open = true;
        for (std::size_t i = index_, looked = 0; i < path_.points.size() && looked < 6 && open;
             ++i, ++looked) {
            const nav::PathPoint &from = path_.points[i - 1 < path_.points.size() ? i - 1 : 0];
            const nav::PathPoint &to = path_.points[i];
            if (to.viaLink != 0) {
                const nav::LinkDef *link = service.world().link(to.viaLink);
                open = link && service.world().linkUsable(*link, mine);
                continue;
            }
            for (const auto &[x, y] : service.world().grid().lineCells(from.position, to.position))
                if (!service.world().passable(to.level, x, y, mine)) {
                    open = false;
                    break;
                }
        }
        pathRevision_ = service.world().revision();
        if (!open) {
            requestPath(context);
            status_ = NavStatus::Searching;
            stand();
            return;
        }
    }
    if (index_ >= path_.points.size()) { // Reached the end of the path.
        stand();
        if (path_.status == nav::PathStatus::Found) {
            finish(context);
        } else if (path_.failure == nav::PathFailure::BudgetExceeded) {
            requestPath(context); // The search ran out of room: carry on from here.
            status_ = NavStatus::Searching;
        } else {
            fail(context, path_.failure);
        }
        return;
    }
    const nav::PathPoint &waypoint = path_.points[index_];
    // Doors: open it for the way through, and wait for it, and for anyone coming the other way.
    if (waypoint.door != 0) {
        const nav::DoorDef *def = service.world().door(waypoint.door);
        const Vec2 toDoor = waypoint.position - position;
        if (def && length(toDoor) < 1.4F) {
            if (!service.claimDoor(waypoint.door, entity().id(), normalized(toDoor),
                                   context.time())) {
                stand(); // Somebody is coming the other way: wait.
                waited_ += seconds;
                waitingOnDoor_ += seconds;
                if (waitingOnDoor_ > 3.0F) { // Never wait forever: take another look.
                    waitingOnDoor_ = 0.0F;
                    requestPath(context);
                    status_ = NavStatus::Searching;
                }
                return;
            }
            if (def->state != nav::DoorState::Open) {
                NavigationDoor *door = service.doorById(context, waypoint.door);
                if (!door || !door->open(context, entity().id(), service.accessMask(accessTokens),
                                         def->access)) {
                    stand();
                    waited_ += seconds;
                    waitingOnDoor_ += seconds;
                    if (waitingOnDoor_ > 3.0F) {
                        waitingOnDoor_ = 0.0F;
                        requestPath(context);
                        status_ = NavStatus::Searching;
                    }
                    return;
                }
                // Opening takes a moment (a gate animates); the next tick sees it open.
                stand();
                waited_ += seconds;
                return;
            }
            if (NavigationDoor *door = service.doorById(context, waypoint.door);
                door && !door->fullyOpen(context)) {
                stand(); // Open for the grid, still swinging for the body: wait for it.
                waited_ += seconds;
                return;
            }
        }
    } else {
        waitingOnDoor_ = 0.0F;
    }
    const float reach = (index_ + 1 == path_.points.size())
                            ? std::max(arriveDistance, tolerance_ * 0.8F)
                            : arriveDistance;
    Vec2 toWaypoint = waypoint.position - position;
    // Release a door once past it.
    if (index_ > 0 && path_.points[index_ - 1].door != 0 && waypoint.door == 0)
        service.releaseDoor(path_.points[index_ - 1].door, entity().id());
    if (length(toWaypoint) <= reach) {
        if (waypoint.door != 0)
            service.releaseDoor(waypoint.door, entity().id());
        ++index_;
        // The next point is reached through a link: this one is its entry. Take it.
        if (index_ < path_.points.size() && path_.points[index_].viaLink != 0) {
            const std::uint32_t id = path_.points[index_].viaLink;
            if (!service.claimLink(id, entity().id())) {
                --index_; // Busy: wait at the entry and try again next tick.
                stand();
                waited_ += seconds;
                return;
            }
            const nav::LinkDef *link = service.world().link(id);
            linkInUse_ = id;
            linkExit_ = path_.points[index_].position;
            linkExitLevel_ = path_.points[index_].level;
            float traverse = defaultTraverse(link ? link->kind : nav::LinkKind::Stairs);
            if (link && link->owner)
                if (const Entity *owner = context.scene().find(link->owner))
                    if (const auto *component = owner->get<NavigationLink>())
                        traverse = component->seconds();
            linkLeft_ = traverse;
        }
        return;
    }
    const bool lastPoint = index_ + 1 == path_.points.size();
    Vec2 heading = normalized(toWaypoint);
    float strength = 1.0F;
    if (lastPoint)
        strength = std::clamp(length(toWaypoint) / 0.9F, 0.35F, 1.0F);
    const Vec2 push = avoidance(context, position, heading);
    Vec2 steer = heading + push;
    if (lengthSquared(steer) < 1e-4F)
        steer = heading;
    steer = normalized(steer);
    if (motor) {
        motor->setIntent(steer, strength, run);
    } else if (const auto body = context.bodyOf(entity().id())) {
        context.physics().setVelocity(*body, steer * (2.5F * strength));
    }
    // Stuck: no progress for a while means the path is wrong (or a neighbor is in the way).
    progressTimer_ += seconds;
    if (progressTimer_ >= 1.2F) {
        if (distance(position, progressPoint_) < 0.12F) {
            ++stuck_;
            if (stuck_ >= 4) {
                fail(context, nav::PathFailure::Stuck);
                stand();
                return;
            }
            requestPath(context);
            status_ = NavStatus::Searching;
        } else {
            stuck_ = 0;
        }
        progressPoint_ = position;
        progressTimer_ = 0.0F;
    }
}

void registerNavigationComponents(ComponentRegistry &registry) {
    registry.add<NavigationSettings>("NavigationSettings");
    registry.add<NavigationObstacle>("NavigationObstacle").allowMultiple();
    registry.add<NavigationDoor>("NavigationDoor");
    registry.add<NavigationLink>("NavigationLink");
    registry.add<NavigationAgent>("NavigationAgent");
}
} // namespace yk
