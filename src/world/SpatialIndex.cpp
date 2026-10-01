#include "yk/world/SpatialIndex.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/world/WorldLevels.hpp"
#include <vector>

namespace yk {
void SpatialIndexService::track(const Entity &entity, const std::string &kind, float radius,
                                bool moving) {
    Kind &entry = kinds_[kind];
    entry.hash.update(entity.id(), entity.worldPosition(), radius, levelOf(entity));
    if (moving)
        entry.moving[entity.id()] = radius;
}

void SpatialIndexService::untrack(EntityId id, const std::string &kind) {
    const auto found = kinds_.find(kind);
    if (found == kinds_.end())
        return;
    found->second.hash.remove(id);
    found->second.moving.erase(id);
}

void SpatialIndexService::refresh(const Entity &entity, const std::string &kind) {
    const auto found = kinds_.find(kind);
    if (found == kinds_.end() || !found->second.hash.contains(entity.id()))
        return;
    Vec2 position;
    float radius = 0.0F;
    int level = 0;
    found->second.hash.find(entity.id(), position, radius, level);
    found->second.hash.update(entity.id(), entity.worldPosition(), radius, levelOf(entity));
}

void SpatialIndexService::onFixedUpdate(GameContext &context, float) {
    for (auto &[name, kind] : kinds_) {
        (void)name;
        std::vector<EntityId> gone;
        for (const auto &[id, radius] : kind.moving) {
            const Entity *entity = context.scene().find(id);
            if (!entity) {
                gone.push_back(id);
                continue;
            }
            kind.hash.update(id, entity->worldPosition(), radius, levelOf(*entity));
        }
        for (const EntityId id : gone) {
            kind.hash.remove(id);
            kind.moving.erase(id);
        }
    }
}

void SpatialIndexService::onShutdown(GameContext &) {
    kinds_.clear();
}

std::vector<SpatialHash::Hit> SpatialIndexService::near(std::string_view kind, Vec2 center,
                                                        float radius, int level) const {
    const auto found = kinds_.find(kind);
    return found == kinds_.end() ? std::vector<SpatialHash::Hit>{}
                                 : found->second.hash.queryCircle(center, radius, level);
}

EntityId SpatialIndexService::nearest(std::string_view kind, Vec2 center, float radius, int level,
                                      const std::function<bool(EntityId)> &accept) const {
    const auto found = kinds_.find(kind);
    return found == kinds_.end() ? EntityId{}
                                 : found->second.hash.nearest(center, radius, level, accept);
}

std::size_t SpatialIndexService::count(std::string_view kind) const {
    const auto found = kinds_.find(kind);
    return found == kinds_.end() ? 0 : found->second.hash.size();
}

const SpatialHash *SpatialIndexService::hash(std::string_view kind) const {
    const auto found = kinds_.find(kind);
    return found == kinds_.end() ? nullptr : &found->second.hash;
}

void SpatialIndexService::describe(std::vector<std::pair<std::string, std::string>> &rows) const {
    for (const auto &[name, kind] : kinds_)
        rows.push_back({"Tracked " + name, std::to_string(kind.hash.size()) + " (" +
                                               std::to_string(kind.moving.size()) + " moving)"});
}
} // namespace yk
