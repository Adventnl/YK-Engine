#pragma once
#include "yk/core/Color.hpp"
#include "yk/core/Result.hpp"
#include "yk/scene/Entity.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

namespace yk {
struct SceneSettings {
    std::string name{"Untitled"};
    Vec2 gravity{0.0F, 9.81F}; // +Y down, meters per second squared.
    Color background{28, 32, 44, 255};
};

// Owns a hierarchy of entities. A scene is plain data: it can be edited, saved and cloned without a
// running game. GameRuntime executes a scene; the editor manipulates one.
class Scene {
  public:
    // idSeed zero draws entity ids from the OS; tests pass a seed for reproducible ids.
    explicit Scene(const ComponentRegistry &registry, std::uint64_t idSeed = 0);
    Scene(const Scene &) = delete;
    Scene &operator=(const Scene &) = delete;
    ~Scene();

    const ComponentRegistry &registry() const {
        return *registry_;
    }
    SceneSettings settings;

    // Creates an entity under `parent` (a root when null or unknown) with a fresh id.
    Entity &createEntity(std::string name = "Entity", EntityId parent = {});
    // For loading: fails on a null or already used id.
    Result<Entity *> createEntityWithId(EntityId id, std::string name, EntityId parent = {});
    // Destroys the entity and all descendants. False for an unknown id.
    bool destroy(EntityId id);

    Entity *find(EntityId id);
    const Entity *find(EntityId id) const;
    Entity *findByName(std::string_view name) const; // First in hierarchy order.
    std::size_t size() const {
        return entities_.size();
    }
    const std::vector<EntityId> &roots() const {
        return roots_;
    }
    // Reparents `child` (appending, or inserting at `index` among the new siblings). Rejects
    // cycles. keepWorldTransform recomputes the local transform so the entity does not move.
    Status setParent(EntityId child, EntityId parent, std::optional<std::size_t> index = {},
                     bool keepWorldTransform = false);
    // Moves an entity among its current siblings.
    Status setSiblingIndex(EntityId id, std::size_t index);

    // Depth-first, parents before children, siblings in order. The list is a snapshot, so callbacks
    // may create or destroy entities; destroyed entities are skipped.
    std::vector<EntityId> hierarchyOrder() const;
    void forEach(const std::function<void(Entity &)> &visit);
    void forEach(const std::function<void(const Entity &)> &visit) const;
    // The entity and all descendants, depth-first.
    std::vector<EntityId> subtree(EntityId root) const;
    bool isAncestor(EntityId ancestor, EntityId descendant) const;

    // Fresh id unused by this scene.
    EntityId newId();
    // Incremented by every structural change (entities, hierarchy, components) for cache users.
    std::uint64_t revision() const {
        return revision_;
    }
    void touch() {
        ++revision_;
    }

  private:
    friend class Entity;
    const ComponentRegistry *registry_;
    std::mt19937_64 random_;
    std::unordered_map<EntityId, std::unique_ptr<Entity>> entities_;
    std::vector<EntityId> roots_;
    std::uint64_t revision_{};
};
} // namespace yk
