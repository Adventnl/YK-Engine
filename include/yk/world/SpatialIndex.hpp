#pragma once
#include "yk/runtime/Services.hpp"
#include "yk/world/SpatialHash.hpp"
#include <map>
#include <string>
#include <string_view>

namespace yk {
class Entity;

// "What is near here" for the whole running game: the pickups an actor could take, the characters
// a noise reaches, the doors a guard passes. Components that want to be found register their
// entity under a kind ("pickup", "actor", "door"); the service keeps one hash per kind and
// refreshes the positions of moving entities after each physics step, so a query visits only the
// buckets around the place it asks about instead of every entity in the scene.
class SpatialIndexService final : public Service {
  public:
    const char *name() const override {
        return "spatial";
    }
    UpdatePhase phase() const override {
        return UpdatePhase::PostSimulation;
    }
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onShutdown(GameContext &context) override;
    void describe(std::vector<std::pair<std::string, std::string>> &rows) const override;

    // Starts keeping track of the entity under `kind`. A `moving` entity is refreshed every tick;
    // one that is not stays where it was when tracked (call refresh() after moving it).
    void track(const Entity &entity, const std::string &kind, float radius = 0.0F,
               bool moving = true);
    void untrack(EntityId id, const std::string &kind);
    void refresh(const Entity &entity, const std::string &kind);

    // Tracked entities of the kind whose circle meets the query circle on the level (every level
    // with SpatialHash::anyLevel), nearest first.
    std::vector<SpatialHash::Hit> near(std::string_view kind, Vec2 center, float radius,
                                       int level = SpatialHash::anyLevel) const;
    EntityId nearest(std::string_view kind, Vec2 center, float radius, int level,
                     const std::function<bool(EntityId)> &accept = {}) const;
    std::size_t count(std::string_view kind) const;
    const SpatialHash *hash(std::string_view kind) const;

  private:
    struct Kind {
        SpatialHash hash{4.0F};
        std::map<EntityId, float> moving; // Entities refreshed every tick, with their radius.
    };
    std::map<std::string, Kind, std::less<>> kinds_;
};
} // namespace yk
