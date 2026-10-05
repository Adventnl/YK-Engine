#include "yk/world/SpatialHash.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace yk {
namespace {
constexpr int levelBias = 2; // anyLevel (-1) is stored as level slot 1; real levels start at 2.
// Items bigger than this many buckets across are clamped so a runaway radius cannot register one
// entity in millions of buckets.
constexpr int maxBucketSpan = 64;
} // namespace

SpatialHash::SpatialHash(float cellSize)
    : cell_(cellSize > 0.1F ? cellSize : 0.1F), inverse_(1.0F / cell_) {}

int SpatialHash::bucket(float coordinate) const {
    const float scaled = std::floor(coordinate * inverse_);
    return static_cast<int>(std::clamp(scaled, -1.0e6F, 1.0e6F));
}
std::uint64_t SpatialHash::key(int x, int y, int level) {
    // 21 bits per axis (buckets are clamped to +-1e6) and 16 for the level: no two buckets share a
    // key, so a query never sees another level's items.
    constexpr std::int64_t bias = 1 << 20;
    const auto ux = static_cast<std::uint64_t>(static_cast<std::int64_t>(x) + bias);
    const auto uy = static_cast<std::uint64_t>(static_cast<std::int64_t>(y) + bias);
    const auto ul = static_cast<std::uint64_t>(level + levelBias) & 0xFFFFU;
    return (ux << 37) | (uy << 16) | ul;
}

void SpatialHash::place(EntityId id, Item &item) {
    item.minX = bucket(item.position.x - item.radius);
    item.maxX = bucket(item.position.x + item.radius);
    item.minY = bucket(item.position.y - item.radius);
    item.maxY = bucket(item.position.y + item.radius);
    item.maxX = std::min(item.maxX, item.minX + maxBucketSpan);
    item.maxY = std::min(item.maxY, item.minY + maxBucketSpan);
    for (int y = item.minY; y <= item.maxY; ++y)
        for (int x = item.minX; x <= item.maxX; ++x)
            buckets_[key(x, y, item.level)].push_back(id);
    ++levelCounts_[item.level];
}
void SpatialHash::unplace(EntityId id, const Item &item) {
    for (int y = item.minY; y <= item.maxY; ++y)
        for (int x = item.minX; x <= item.maxX; ++x) {
            const auto found = buckets_.find(key(x, y, item.level));
            if (found == buckets_.end())
                continue;
            auto &list = found->second;
            const auto at = std::find(list.begin(), list.end(), id);
            if (at != list.end()) {
                *at = list.back();
                list.pop_back();
            }
            if (list.empty())
                buckets_.erase(found);
        }
    if (const auto count = levelCounts_.find(item.level); count != levelCounts_.end())
        if (--count->second == 0)
            levelCounts_.erase(count);
}

void SpatialHash::insert(EntityId id, Vec2 position, float radius, int level) {
    if (!finite(position) || !(radius >= 0.0F) || !std::isfinite(radius))
        return;
    remove(id);
    Item item;
    item.position = position;
    item.radius = radius;
    item.level = std::max(level, anyLevel);
    place(id, item);
    items_.emplace(id, item);
}
void SpatialHash::update(EntityId id, Vec2 position, float radius, int level) {
    const auto found = items_.find(id);
    if (found == items_.end()) {
        insert(id, position, radius, level);
        return;
    }
    Item &item = found->second;
    if (!finite(position) || !(radius >= 0.0F) || !std::isfinite(radius))
        return;
    level = std::max(level, anyLevel);
    const int minX = bucket(position.x - radius), maxX = bucket(position.x + radius);
    const int minY = bucket(position.y - radius), maxY = bucket(position.y + radius);
    const bool sameBuckets = minX == item.minX && minY == item.minY &&
                             std::min(maxX, minX + maxBucketSpan) == item.maxX &&
                             std::min(maxY, minY + maxBucketSpan) == item.maxY &&
                             level == item.level;
    if (sameBuckets) { // The common case for a walking character: no bucket changes.
        item.position = position;
        item.radius = radius;
        return;
    }
    unplace(id, item);
    item.position = position;
    item.radius = radius;
    item.level = level;
    place(id, item);
}
bool SpatialHash::remove(EntityId id) {
    const auto found = items_.find(id);
    if (found == items_.end())
        return false;
    unplace(id, found->second);
    items_.erase(found);
    return true;
}
void SpatialHash::clear() {
    items_.clear();
    buckets_.clear();
    levelCounts_.clear();
}
bool SpatialHash::find(EntityId id, Vec2 &position, float &radius, int &level) const {
    const auto found = items_.find(id);
    if (found == items_.end())
        return false;
    position = found->second.position;
    radius = found->second.radius;
    level = found->second.level;
    return true;
}

void SpatialHash::forEachInCircle(Vec2 center, float radius, int level,
                                  const std::function<void(EntityId, Vec2, float)> &visit) const {
    if (!finite(center) || !(radius >= 0.0F) || !std::isfinite(radius))
        return;
    const int minX = bucket(center.x - radius), maxX = bucket(center.x + radius);
    const int minY = bucket(center.y - radius), maxY = bucket(center.y + radius);
    // Items that span several buckets are listed in each; report every item once.
    std::vector<EntityId> seen;
    const bool multiple = (maxX > minX) || (maxY > minY);
    const auto consider = [&](int slot, int x, int y) {
        ++bucketVisits_;
        const auto found = buckets_.find(key(x, y, slot));
        if (found == buckets_.end())
            return;
        for (const EntityId id : found->second) {
            const Item &item = items_.at(id);
            const float reach = radius + item.radius;
            if (lengthSquared(item.position - center) > reach * reach)
                continue;
            if (multiple) {
                if (std::find(seen.begin(), seen.end(), id) != seen.end())
                    continue;
                seen.push_back(id);
            }
            visit(id, item.position, item.radius);
        }
    };
    // A level query sees its own level and the items on every level; anyLevel sees all levels.
    std::vector<int> slots;
    if (level == anyLevel) {
        for (const auto &[inUse, count] : levelCounts_) {
            (void)count;
            slots.push_back(inUse);
        }
    } else {
        slots = {level, anyLevel};
    }
    for (const int slot : slots)
        for (int y = minY; y <= maxY; ++y)
            for (int x = minX; x <= maxX; ++x)
                consider(slot, x, y);
}

std::vector<SpatialHash::Hit> SpatialHash::queryCircle(Vec2 center, float radius, int level) const {
    std::vector<Hit> hits;
    forEachInCircle(center, radius, level, [&](EntityId id, Vec2 position, float) {
        hits.push_back({id, distance(center, position)});
    });
    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        return a.distance != b.distance ? a.distance < b.distance : a.id < b.id;
    });
    return hits;
}
std::vector<EntityId> SpatialHash::queryRect(Rect area, int level) const {
    std::vector<EntityId> result;
    const Vec2 middle = center(area);
    const float reach = length(area.size) * 0.5F;
    forEachInCircle(middle, reach, level, [&](EntityId id, Vec2 position, float radius) {
        const Rect expanded{{area.position.x - radius, area.position.y - radius},
                            {area.size.x + 2 * radius, area.size.y + 2 * radius}};
        if (yk::contains(expanded, position))
            result.push_back(id);
    });
    std::sort(result.begin(), result.end());
    return result;
}
EntityId SpatialHash::nearest(Vec2 center, float radius, int level,
                              const std::function<bool(EntityId)> &accept) const {
    EntityId best{};
    float bestDistance = std::numeric_limits<float>::max();
    forEachInCircle(center, radius, level, [&](EntityId id, Vec2 position, float) {
        const float d = distance(center, position);
        if (d < bestDistance && (!accept || accept(id))) {
            best = id;
            bestDistance = d;
        }
    });
    return best;
}
} // namespace yk
