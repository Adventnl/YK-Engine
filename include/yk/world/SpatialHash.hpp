#pragma once
#include "yk/core/Math.hpp"
#include "yk/scene/EntityId.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

namespace yk {
// A uniform grid of buckets over the plane, one grid per world level, for "what is near here"
// questions that must not scan every entity: who can this guard see, which object can the player
// use, who hears this noise. An item is a point with a radius (zero for a person, a few meters for
// a noise source or a zone) on one level, or on every level (`anyLevel`).
//
// The hash only narrows candidates to the buckets a query touches; queries return the items whose
// own circle really intersects the query circle, ordered by distance. Moving an item inside the
// bucket it already occupies costs one comparison.
class SpatialHash {
  public:
    static constexpr int anyLevel = -1;
    explicit SpatialHash(float cellSize = 4.0F);

    void insert(EntityId id, Vec2 position, float radius = 0.0F, int level = 0);
    // Inserts the item when it is not there yet.
    void update(EntityId id, Vec2 position, float radius, int level);
    bool remove(EntityId id);
    void clear();
    bool contains(EntityId id) const {
        return items_.contains(id);
    }
    std::size_t size() const {
        return items_.size();
    }
    float cellSize() const {
        return cell_;
    }
    // Position, radius and level of an item (false when it is not in the hash).
    bool find(EntityId id, Vec2 &position, float &radius, int &level) const;

    struct Hit {
        EntityId id;
        float distance; // Between the query center and the item's center.
    };
    // Items whose circle intersects the circle (center, radius) and that are on `level` (or are on
    // every level; with anyLevel as the query level, items of every level match). Nearest first.
    std::vector<Hit> queryCircle(Vec2 center, float radius, int level = anyLevel) const;
    // Same, without sorting or distances: for large sweeps that sort later themselves.
    void forEachInCircle(Vec2 center, float radius, int level,
                         const std::function<void(EntityId, Vec2, float)> &visit) const;
    std::vector<EntityId> queryRect(Rect area, int level = anyLevel) const;
    // The closest item for which `accept` returns true, within `radius`; none: a null id.
    EntityId nearest(Vec2 center, float radius, int level,
                     const std::function<bool(EntityId)> &accept = {}) const;

    // Total bucket visits made by queries since creation (a measure for the profiler and tests).
    std::uint64_t bucketVisits() const {
        return bucketVisits_;
    }

  private:
    struct Item {
        Vec2 position;
        float radius{};
        int level{};
        int minX{}, minY{}, maxX{}, maxY{}; // The bucket range it is registered in.
    };
    static std::uint64_t key(int x, int y, int level);
    void place(EntityId id, Item &item);
    void unplace(EntityId id, const Item &item);
    int bucket(float coordinate) const;

    float cell_;
    float inverse_;
    std::unordered_map<EntityId, Item> items_;
    std::unordered_map<std::uint64_t, std::vector<EntityId>> buckets_;
    std::map<int, std::size_t> levelCounts_; // Levels that hold items, for queries on every level.
    mutable std::uint64_t bucketVisits_{};
};
} // namespace yk
