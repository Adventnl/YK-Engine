#include "yk/navigation/Navigation.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace yk::nav {
namespace {
constexpr float sqrt2 = 1.41421356F;
constexpr std::uint32_t reverseBit = 0x80000000U;
const std::string noName;

bool closedFlag(const std::uint32_t stamp) {
    return (stamp & 1U) != 0;
}
} // namespace

const char *linkKindName(LinkKind kind) {
    switch (kind) {
    case LinkKind::Stairs:
        return "stairs";
    case LinkKind::Ladder:
        return "ladder";
    case LinkKind::Vent:
        return "vent";
    case LinkKind::Hole:
        return "hole";
    case LinkKind::Elevator:
        return "elevator";
    case LinkKind::Drop:
        return "drop";
    case LinkKind::Climb:
        return "climb";
    case LinkKind::Teleport:
        return "teleport";
    }
    return "link";
}
const char *pathStatusName(PathStatus status) {
    switch (status) {
    case PathStatus::Found:
        return "found";
    case PathStatus::Partial:
        return "partial";
    case PathStatus::Unreachable:
        return "unreachable";
    case PathStatus::Invalid:
        return "invalid";
    }
    return "invalid";
}
const char *pathFailureName(PathFailure failure) {
    switch (failure) {
    case PathFailure::None:
        return "none";
    case PathFailure::BudgetExceeded:
        return "search budget exceeded";
    case PathFailure::NoRoute:
        return "no route";
    case PathFailure::GoalBlocked:
        return "goal blocked";
    case PathFailure::StartBlocked:
        return "start blocked";
    case PathFailure::BadInput:
        return "bad input";
    case PathFailure::Stuck:
        return "stuck";
    }
    return "none";
}

std::uint64_t AgentProfile::hash() const {
    std::uint64_t h = 1469598103934665603ULL;
    const auto mix = [&](std::uint64_t value) {
        h ^= value;
        h *= 1099511628211ULL;
    };
    mix(static_cast<std::uint64_t>(radiusCells));
    mix(areaMask);
    mix(capabilities);
    mix(access);
    for (const float cost : areaCost) {
        std::uint32_t bits;
        std::memcpy(&bits, &cost, sizeof bits);
        mix(bits);
    }
    return h;
}

NavigationWorld::NavigationWorld() {
    areaNames_ = {"walkable"};
    capabilityNames_ = {"walk", "doors", "climb", "crawl", "drop"};
}

void NavigationWorld::configure(const GridSpec &spec) {
    grid_.configure(spec);
    const std::size_t count = grid_.cellCount();
    clearance_.assign(count, static_cast<std::uint8_t>(maxClearance));
    nodes_.assign(count, Node{});
    searchNumber_ = 0;
    doors_.clear();
    nextDoor_ = 1;
    search_ = Search{};
    // The whole grid is new: derive clearance for all of it at the next refresh.
    for (int level = 0; level < grid_.spec().levels; ++level)
        grid_.markDirty({level, 0, 0, grid_.spec().width - 1, grid_.spec().height - 1});
    linkIndexDirty_ = true;
    ++revision_;
}

// ---- Vocabulary -------------------------------------------------------------------------------
int NavigationWorld::areaId(const std::string &name) {
    if (const int existing = findArea(name); existing >= 0)
        return existing;
    if (areaNames_.size() >= static_cast<std::size_t>(maxAreas))
        return -1;
    areaNames_.push_back(name);
    return static_cast<int>(areaNames_.size()) - 1;
}
int NavigationWorld::findArea(const std::string &name) const {
    for (std::size_t i = 0; i < areaNames_.size(); ++i)
        if (areaNames_[i] == name)
            return static_cast<int>(i);
    return -1;
}
const std::string &NavigationWorld::areaName(int id) const {
    return id >= 0 && static_cast<std::size_t>(id) < areaNames_.size()
               ? areaNames_[static_cast<std::size_t>(id)]
               : noName;
}
int NavigationWorld::capabilityId(const std::string &name) {
    for (std::size_t i = 0; i < capabilityNames_.size(); ++i)
        if (capabilityNames_[i] == name)
            return static_cast<int>(i);
    if (capabilityNames_.size() >= 32)
        return -1;
    capabilityNames_.push_back(name);
    return static_cast<int>(capabilityNames_.size()) - 1;
}
std::uint32_t NavigationWorld::capabilityMask(const std::vector<std::string> &names) {
    std::uint32_t mask = 0;
    for (const std::string &name : names)
        if (const int id = capabilityId(name); id >= 0)
            mask |= 1U << static_cast<unsigned>(id);
    return mask;
}

// ---- Doors and links --------------------------------------------------------------------------
std::uint16_t NavigationWorld::addDoor(const DoorDef &door, const CellRect &area) {
    const std::uint16_t id = nextDoor_++;
    doors_[id] = door;
    const int minX = std::max(area.minX, 0), minY = std::max(area.minY, 0);
    const int maxX = std::min(area.maxX, grid_.spec().width - 1);
    const int maxY = std::min(area.maxY, grid_.spec().height - 1);
    if (area.level >= 0 && area.level < grid_.spec().levels)
        for (int y = minY; y <= maxY; ++y)
            for (int x = minX; x <= maxX; ++x)
                grid_.at(area.level, x, y).door = id;
    ++revision_;
    return id;
}
bool NavigationWorld::setDoorState(std::uint16_t id, DoorState state) {
    const auto found = doors_.find(id);
    if (found == doors_.end())
        return false;
    if (found->second.state != state) {
        found->second.state = state;
        ++revision_;
    }
    return true;
}
bool NavigationWorld::setDoorAccess(std::uint16_t id, std::uint64_t access) {
    const auto found = doors_.find(id);
    if (found == doors_.end())
        return false;
    if (found->second.access != access) {
        found->second.access = access;
        ++revision_;
    }
    return true;
}
const DoorDef *NavigationWorld::door(std::uint16_t id) const {
    const auto found = doors_.find(id);
    return found == doors_.end() ? nullptr : &found->second;
}
void NavigationWorld::removeDoor(std::uint16_t id) {
    if (doors_.erase(id) == 0)
        return;
    // The cells stop being a door.
    for (int level = 0; level < grid_.spec().levels; ++level)
        for (int y = 0; y < grid_.spec().height; ++y)
            for (int x = 0; x < grid_.spec().width; ++x)
                if (grid_.at(level, x, y).door == id)
                    grid_.at(level, x, y).door = 0;
    ++revision_;
}

void NavigationWorld::linkCell(int level, Vec2 point, int &x, int &y) const {
    (void)level;
    grid_.worldToCell(point, x, y);
}
std::uint32_t NavigationWorld::addLink(const LinkDef &link) {
    const std::uint32_t id = nextLink_++;
    links_[id] = link;
    linkIndexDirty_ = true;
    ++revision_;
    return id;
}
bool NavigationWorld::setLinkOpen(std::uint32_t id, bool open) {
    const auto found = links_.find(id);
    if (found == links_.end())
        return false;
    if (found->second.open != open) {
        found->second.open = open;
        ++revision_;
    }
    return true;
}
bool NavigationWorld::replaceLink(std::uint32_t id, const LinkDef &link) {
    const auto found = links_.find(id);
    if (found == links_.end())
        return false;
    if (!(found->second.kind == link.kind && found->second.fromLevel == link.fromLevel &&
          found->second.from == link.from && found->second.toLevel == link.toLevel &&
          found->second.to == link.to && found->second.bidirectional == link.bidirectional))
        linkIndexDirty_ = true;
    found->second = link;
    ++revision_;
    return true;
}
void NavigationWorld::removeLink(std::uint32_t id) {
    if (links_.erase(id) > 0) {
        linkIndexDirty_ = true;
        ++revision_;
    }
}
const LinkDef *NavigationWorld::link(std::uint32_t id) const {
    const auto found = links_.find(id);
    return found == links_.end() ? nullptr : &found->second;
}
void NavigationWorld::rebuildLinkIndex() {
    linkIndex_.clear();
    for (const auto &[id, link] : links_) {
        int x = 0, y = 0;
        if (link.fromLevel >= 0 && link.fromLevel < grid_.spec().levels) {
            grid_.worldToCell(link.from, x, y);
            if (grid_.inside(link.fromLevel, x, y))
                linkIndex_[nodeIndex(link.fromLevel, x, y)].emplace_back(id, true);
        }
        if (link.bidirectional && link.toLevel >= 0 && link.toLevel < grid_.spec().levels) {
            grid_.worldToCell(link.to, x, y);
            if (grid_.inside(link.toLevel, x, y))
                linkIndex_[nodeIndex(link.toLevel, x, y)].emplace_back(id, false);
        }
    }
    linkIndexDirty_ = false;
}

// ---- Clearance --------------------------------------------------------------------------------
void NavigationWorld::rebuildClearance(int level, int minX, int minY, int maxX, int maxY) {
    const GridSpec &spec = grid_.spec();
    minX = std::max(minX, 0);
    minY = std::max(minY, 0);
    maxX = std::min(maxX, spec.width - 1);
    maxY = std::min(maxY, spec.height - 1);
    const auto isBlocked = [&](int x, int y) {
        const GridCell &cell = grid_.at(level, x, y);
        return (cell.flags & cellflag::solid) != 0 || cell.blockers > 0;
    };
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x) {
            int distance = maxClearance;
            if (isBlocked(x, y)) {
                distance = 0;
            } else {
                for (int ring = 1; ring < maxClearance && distance == maxClearance; ++ring) {
                    for (int dy = -ring; dy <= ring && distance == maxClearance; ++dy)
                        for (int dx = -ring; dx <= ring; ++dx) {
                            if (std::max(std::abs(dx), std::abs(dy)) != ring)
                                continue;
                            const int nx = x + dx, ny = y + dy;
                            if (nx < 0 || ny < 0 || nx >= spec.width || ny >= spec.height)
                                continue; // Beyond the grid is not an obstacle.
                            if (isBlocked(nx, ny)) {
                                distance = ring;
                                break;
                            }
                        }
                }
            }
            clearance_[grid_.index(level, x, y)] = static_cast<std::uint8_t>(distance);
        }
}

bool NavigationWorld::refresh() {
    bool changed = false;
    if (linkIndexDirty_) {
        rebuildLinkIndex();
        changed = true;
    }
    const auto dirty = grid_.takeDirty();
    for (const CellRect &rect : dirty) {
        rebuildClearance(rect.level, rect.minX - maxClearance, rect.minY - maxClearance,
                         rect.maxX + maxClearance, rect.maxY + maxClearance);
        changed = true;
    }
    if (changed)
        ++revision_;
    return changed;
}

// ---- Questions --------------------------------------------------------------------------------
bool NavigationWorld::doorAllows(const DoorDef &door, const AgentProfile &profile) const {
    switch (door.state) {
    case DoorState::Open:
        return true;
    case DoorState::Sealed:
        return false;
    case DoorState::Closed:
        return (profile.capabilities & capability::doors) != 0 &&
               (door.access == 0 || (profile.access & door.access) == door.access);
    case DoorState::Locked:
        return (profile.capabilities & capability::doors) != 0 && door.access != 0 &&
               (profile.access & door.access) == door.access;
    }
    return false;
}

bool NavigationWorld::passable(int level, int x, int y, const AgentProfile &profile) const {
    if (!grid_.inside(level, x, y))
        return false;
    const std::size_t index = grid_.index(level, x, y);
    const GridCell &cell = grid_.at(level, x, y);
    if ((cell.flags & cellflag::solid) != 0 || cell.blockers > 0)
        return false;
    if (clearance_[index] <= profile.radiusCells)
        return false;
    if (((profile.areaMask >> cell.area) & 1U) == 0)
        return false;
    if (cell.door != 0) {
        const auto found = doors_.find(cell.door);
        if (found != doors_.end() && !doorAllows(found->second, profile))
            return false;
    }
    return true;
}

bool NavigationWorld::nearestPassable(int level, Vec2 around, int maxRing,
                                      const AgentProfile &profile, Vec2 &found) const {
    int cx = 0, cy = 0;
    grid_.worldToCell(around, cx, cy);
    float best = std::numeric_limits<float>::max();
    bool any = false;
    for (int ring = 0;
         ring <= maxRing && !(any && static_cast<float>(ring - 1) * grid_.spec().cellSize > best);
         ++ring)
        for (int dy = -ring; dy <= ring; ++dy)
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != ring)
                    continue;
                if (!passable(level, cx + dx, cy + dy, profile))
                    continue;
                const Vec2 center = grid_.cellCenter(cx + dx, cy + dy);
                const float d = distance(center, around);
                if (d < best) {
                    best = d;
                    found = center;
                    any = true;
                }
            }
    return any;
}

bool NavigationWorld::segmentWalkable(int level, Vec2 from, Vec2 to,
                                      const AgentProfile &profile) const {
    int sx = 0, sy = 0;
    grid_.worldToCell(from, sx, sy);
    if (!grid_.inside(level, sx, sy))
        return false;
    const GridCell &first = grid_.at(level, sx, sy);
    for (const auto &[x, y] : grid_.lineCells(from, to)) {
        if (!passable(level, x, y, profile))
            return false;
        const GridCell &cell = grid_.at(level, x, y);
        // Do not cut across ground of a different kind or cost: walk the weighted route instead.
        if (cell.area != first.area || cell.cost != first.cost || cell.door != 0)
            return false;
    }
    return true;
}

bool NavigationWorld::linkUsable(const LinkDef &link, const AgentProfile &profile) const {
    return link.open && (profile.capabilities & link.capabilities) == link.capabilities &&
           (link.access == 0 || (profile.access & link.access) == link.access);
}

float NavigationWorld::cellCost(const GridCell &cell, const AgentProfile &profile) const {
    return (static_cast<float>(cell.cost) / 16.0F) * profile.costOf(cell.area);
}

void NavigationWorld::decode(std::uint32_t node, int &level, int &x, int &y) const {
    const auto width = static_cast<std::uint32_t>(grid_.spec().width);
    const auto height = static_cast<std::uint32_t>(grid_.spec().height);
    x = static_cast<int>(node % width);
    const std::uint32_t rest = node / width;
    y = static_cast<int>(rest % height);
    level = static_cast<int>(rest / height);
}

float NavigationWorld::heuristic(const Search &search, int level, int x, int y) const {
    (void)level;
    const Vec2 center = grid_.cellCenter(x, y);
    const float dx = std::fabs(center.x - search.query.goal.x);
    const float dy = std::fabs(center.y - search.query.goal.y);
    const float octile = std::max(dx, dy) + (sqrt2 - 1.0F) * std::min(dx, dy);
    return std::max(0.0F, octile - search.query.tolerance);
}

// ---- Search -----------------------------------------------------------------------------------
bool NavigationWorld::beginSearch(const PathQuery &query) {
    if (search_.active)
        return false;
    search_ = Search{};
    search_.active = true;
    search_.query = query;
    ++stats_.searches;
    if (linkIndexDirty_ || grid_.hasDirty()) {
        // Callers refresh once a tick; a direct caller may not have: make sure the derived data is
        // current before relying on it.
        refresh();
    }
    NavPath &result = search_.result;
    const GridSpec &spec = grid_.spec();
    int sx = 0, sy = 0, gx = 0, gy = 0;
    grid_.worldToCell(query.start, sx, sy);
    grid_.worldToCell(query.goal, gx, gy);
    if (!grid_.configured() || !finite(query.start) || !finite(query.goal) ||
        !grid_.inside(query.startLevel, sx, sy) || query.goalLevel < 0 ||
        query.goalLevel >= spec.levels || !grid_.inside(query.goalLevel, gx, gy)) {
        result.status = PathStatus::Invalid;
        result.failure = PathFailure::BadInput;
        search_.done = true;
        return true;
    }
    AgentProfile profile = query.profile;
    profile.radiusCells = std::clamp(profile.radiusCells, 0, maxClearance - 1);
    search_.query.profile = profile;
    // Start: where the agent stands, or the closest cell it could stand in (it may be touching a
    // wall or standing in a doorway).
    Vec2 startCenter = grid_.cellCenter(sx, sy);
    if (!passable(query.startLevel, sx, sy, profile)) {
        if (!nearestPassable(query.startLevel, query.start, 3, profile, startCenter)) {
            result.status = PathStatus::Unreachable;
            result.failure = PathFailure::StartBlocked;
            search_.done = true;
            return true;
        }
        grid_.worldToCell(startCenter, sx, sy);
    }
    // Goal: the cell it names, or the closest walkable one.
    search_.haveGoalNode = false;
    if (passable(query.goalLevel, gx, gy, profile)) {
        search_.goalNode = nodeIndex(query.goalLevel, gx, gy);
        search_.haveGoalNode = true;
    } else if (Vec2 adjusted; nearestPassable(query.goalLevel, query.goal, 8, profile, adjusted)) {
        int ax = 0, ay = 0;
        grid_.worldToCell(adjusted, ax, ay);
        search_.goalNode = nodeIndex(query.goalLevel, ax, ay);
        search_.haveGoalNode = true;
        search_.goalAdjusted = true;
    }
    search_.start = nodeIndex(query.startLevel, sx, sy);
    ++searchNumber_;
    if (searchNumber_ >= 0x7FFFFFFFU) { // Wrapped: forget every node.
        for (Node &node : nodes_)
            node.stamp = 0;
        searchNumber_ = 1;
    }
    Node &start = nodes_[search_.start];
    start = Node{0.0F, search_.start, 0U, searchNumber_ << 1};
    search_.best = search_.start;
    search_.bestH = heuristic(search_, query.startLevel, sx, sy) +
                    (query.startLevel == query.goalLevel ? 0.0F : 1.0e5F);
    search_.heap.push_back({search_.bestH, 0.0F, search_.start});
    return true;
}

bool NavigationWorld::stepSearch(int budget) {
    if (!search_.active)
        return true;
    if (search_.done)
        return true;
    Search &s = search_;
    const AgentProfile &profile = s.query.profile;
    const GridSpec &spec = grid_.spec();
    const std::uint32_t current = searchNumber_;
    const auto before = [](const HeapEntry &a, const HeapEntry &b) { return a.f > b.f; };
    int spent = 0;
    while (!s.heap.empty() && spent < budget) {
        if (s.expansions >= s.query.maxExpansions) {
            s.budgetExceeded = true;
            break;
        }
        std::pop_heap(s.heap.begin(), s.heap.end(), before);
        const HeapEntry entry = s.heap.back();
        s.heap.pop_back();
        Node &node = nodes_[entry.node];
        if ((node.stamp >> 1) != current || closedFlag(node.stamp) || entry.g > node.g + 1e-4F)
            continue; // A stale queue entry: this node was reached cheaper since.
        node.stamp |= 1U;
        ++s.expansions;
        ++spent;
        int level = 0, x = 0, y = 0;
        decode(entry.node, level, x, y);
        // The best node so far is the closest to the goal; on the wrong level it is far (a floor
        // below the goal is not "near" it however close the plan view looks).
        const float h = heuristic(s, level, x, y) + (level == s.query.goalLevel ? 0.0F : 1.0e5F);
        if (h < s.bestH) {
            s.bestH = h;
            s.best = entry.node;
        }
        const Vec2 center = grid_.cellCenter(x, y);
        if (level == s.query.goalLevel && ((s.haveGoalNode && entry.node == s.goalNode) ||
                                           distance(center, s.query.goal) <= s.query.tolerance)) {
            s.reached = true;
            s.terminal = entry.node;
            break;
        }
        const auto relax = [&](std::uint32_t next, float cost, std::uint32_t link, int nl, int nx,
                               int ny) {
            const float g = node.g + cost;
            Node &target = nodes_[next];
            if ((target.stamp >> 1) == current) {
                if (closedFlag(target.stamp) || g >= target.g)
                    return;
            }
            target.g = g;
            target.parent = entry.node;
            target.link = link;
            target.stamp = current << 1;
            s.heap.push_back({g + heuristic(s, nl, nx, ny), g, next});
            std::push_heap(s.heap.begin(), s.heap.end(), before);
        };
        static constexpr int steps[8][2] = {{1, 0}, {-1, 0}, {0, 1},  {0, -1},
                                            {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
        for (const auto &step : steps) {
            const int nx = x + step[0], ny = y + step[1];
            if (!passable(level, nx, ny, profile))
                continue;
            const bool diagonal = step[0] != 0 && step[1] != 0;
            if (diagonal && (!passable(level, x + step[0], y, profile) ||
                             !passable(level, x, y + step[1], profile)))
                continue; // No cutting a corner.
            const GridCell &cell = grid_.at(level, nx, ny);
            float cost = (diagonal ? sqrt2 : 1.0F) * spec.cellSize * cellCost(cell, profile);
            if (cell.door != 0)
                if (const auto found = doors_.find(cell.door);
                    found != doors_.end() && found->second.state != DoorState::Open)
                    cost += found->second.penalty;
            relax(nodeIndex(level, nx, ny), cost, 0U, level, nx, ny);
        }
        if (const auto at = linkIndex_.find(entry.node); at != linkIndex_.end())
            for (const auto &[linkId, fromEnd] : at->second) {
                const LinkDef &link = links_.at(linkId);
                if (!linkUsable(link, profile))
                    continue;
                const int targetLevel = fromEnd ? link.toLevel : link.fromLevel;
                const Vec2 targetPoint = fromEnd ? link.to : link.from;
                int tx = 0, ty = 0;
                grid_.worldToCell(targetPoint, tx, ty);
                if (!passable(targetLevel, tx, ty, profile))
                    continue;
                const Vec2 entryPoint = fromEnd ? link.from : link.to;
                const float cost =
                    link.cost + distance(center, entryPoint) + distance(entryPoint, targetPoint);
                relax(nodeIndex(targetLevel, tx, ty), cost, linkId | (fromEnd ? 0U : reverseBit),
                      targetLevel, tx, ty);
            }
    }
    if (s.reached || s.heap.empty() || s.budgetExceeded) {
        s.done = true;
        stats_.expansions += static_cast<std::uint64_t>(s.expansions);
        stats_.lastExpansions = s.expansions;
        if (s.reached) {
            s.result = reconstruct(s, s.terminal, true);
            s.result.goalAdjusted = s.goalAdjusted;
        } else {
            NavPath partial = reconstruct(s, s.best, false);
            partial.goalAdjusted = s.goalAdjusted;
            if (s.budgetExceeded) {
                partial.failure = PathFailure::BudgetExceeded;
            } else {
                partial.failure = s.haveGoalNode ? PathFailure::NoRoute : PathFailure::GoalBlocked;
            }
            if (partial.points.size() <= 1 && !s.budgetExceeded) {
                partial.status = PathStatus::Unreachable;
                partial.points.clear();
            }
            s.result = std::move(partial);
        }
        s.result.expansions = s.expansions;
        return true;
    }
    return false;
}

NavPath NavigationWorld::reconstruct(const Search &search, std::uint32_t terminal, bool complete) {
    NavPath path;
    path.status = complete ? PathStatus::Found : PathStatus::Partial;
    std::vector<std::uint32_t> chain;
    for (std::uint32_t node = terminal;; node = nodes_[node].parent) {
        chain.push_back(node);
        if (node == search.start || chain.size() > nodes_.size())
            break;
    }
    std::reverse(chain.begin(), chain.end());
    path.cost = nodes_[terminal].g;
    for (std::size_t i = 0; i < chain.size(); ++i) {
        int level = 0, x = 0, y = 0;
        decode(chain[i], level, x, y);
        PathPoint point;
        point.position = grid_.cellCenter(x, y);
        point.level = level;
        point.door = grid_.at(level, x, y).door;
        if (i > 0 && nodes_[chain[i]].link != 0) {
            const std::uint32_t raw = nodes_[chain[i]].link;
            const bool reversed = (raw & reverseBit) != 0;
            const std::uint32_t id = raw & ~reverseBit;
            if (const LinkDef *link = this->link(id)) {
                point.viaLink = id;
                point.position = reversed ? link->from : link->to;
                point.level = reversed ? link->fromLevel : link->toLevel;
                // The previous point is the link's entry, exactly.
                path.points.back().position = reversed ? link->to : link->from;
            }
        }
        path.points.push_back(point);
    }
    if (!path.points.empty()) {
        path.points.front().position = search.query.start;
        path.points.front().door = 0;
    }
    // Finish on the exact goal when the last cell can walk straight to it.
    if (complete && path.points.size() >= 1) {
        PathPoint &last = path.points.back();
        if (last.level == search.query.goalLevel && !search.goalAdjusted &&
            segmentWalkable(last.level, last.position, search.query.goal, search.query.profile)) {
            if (distance(last.position, search.query.goal) > 1e-3F) {
                PathPoint goal;
                goal.position = search.query.goal;
                goal.level = last.level;
                path.points.push_back(goal);
            }
        }
    }
    smooth(search.query.profile, path.points);
    for (std::size_t i = 1; i < path.points.size(); ++i)
        if (path.points[i].viaLink == 0 && path.points[i].level == path.points[i - 1].level)
            path.length += distance(path.points[i].position, path.points[i - 1].position);
    return path;
}

// String pulling: drop the points a straight, equally weighted walk makes unnecessary. Link ends,
// door cells and the first and last points stay.
void NavigationWorld::smooth(const AgentProfile &profile, std::vector<PathPoint> &points) const {
    if (points.size() < 3)
        return;
    // A point that cannot be skipped: a door cell, a link exit, or the entry of a link.
    std::vector<bool> special(points.size(), false);
    for (std::size_t i = 0; i < points.size(); ++i)
        special[i] = points[i].door != 0 || points[i].viaLink != 0 ||
                     (i + 1 < points.size() && points[i + 1].viaLink != 0);
    std::vector<PathPoint> result;
    result.push_back(points.front());
    std::size_t anchor = 0;
    while (anchor + 1 < points.size()) {
        std::size_t farthest = anchor + 1;
        for (std::size_t candidate = anchor + 2; candidate < points.size(); ++candidate) {
            if (special[candidate - 1] || points[candidate].level != points[anchor].level)
                break;
            if (!segmentWalkable(points[anchor].level, points[anchor].position,
                                 points[candidate].position, profile))
                break;
            farthest = candidate;
        }
        result.push_back(points[farthest]);
        anchor = farthest;
    }
    points = std::move(result);
}

NavPath NavigationWorld::finishSearch() {
    NavPath result = std::move(search_.result);
    switch (result.status) {
    case PathStatus::Found:
        ++stats_.found;
        break;
    case PathStatus::Partial:
        ++stats_.partial;
        break;
    case PathStatus::Unreachable:
    case PathStatus::Invalid:
        ++stats_.unreachable;
        break;
    }
    search_ = Search{};
    return result;
}

NavPath NavigationWorld::findPath(const PathQuery &query) {
    if (search_.active) {
        NavPath busy;
        busy.status = PathStatus::Invalid;
        busy.failure = PathFailure::BadInput;
        return busy;
    }
    beginSearch(query);
    while (!stepSearch(1 << 20)) {
    }
    return finishSearch();
}
} // namespace yk::nav
