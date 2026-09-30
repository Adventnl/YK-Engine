#pragma once
#include "yk/core/Math.hpp"

namespace yk {
class Entity;
class Scene;
class GameContext;
struct ComponentType;

struct CollisionInfo {
    Vec2 point{};
    Vec2 normal{}; // Points from the component's entity toward the other entity.
    float approachSpeed{};
};

// Behaviour and data attached to an entity. Components are plain classes: their editable state is
// public members described to the ComponentRegistry (see Registry.hpp), so the same declaration
// drives serialization, the inspector, prefabs and undo. Constructors must not touch the scene.
//
// Runtime hooks are called only by GameRuntime, never by the editor's edit mode, and only for
// enabled components on entities that are active in the hierarchy.
class Component {
  public:
    Component() = default;
    Component(const Component &) = delete;
    Component &operator=(const Component &) = delete;
    virtual ~Component() = default;

    Entity &entity() const {
        return *entity_;
    }
    Scene &scene() const;
    const ComponentType &type() const {
        return *type_;
    }
    bool enabled{true};

    // Once, before the first fixed update, after every entity exists and physics bodies are built.
    virtual void onStart(GameContext &) {}
    virtual void onFixedUpdate(GameContext &, float /*seconds*/) {}
    virtual void onUpdate(GameContext &, float /*seconds*/) {}
    virtual void onLateUpdate(GameContext &, float /*seconds*/) {} // After every onUpdate.
    virtual void onTriggerEnter(GameContext &, Entity & /*other*/) {}
    virtual void onTriggerExit(GameContext &, Entity & /*other*/) {}
    virtual void onCollisionEnter(GameContext &, Entity & /*other*/, const CollisionInfo &) {}
    // The solid contact with `other` ended (it moved away, or one of them was disabled). The point
    // and normal are empty: the contact no longer exists.
    virtual void onCollisionExit(GameContext &, Entity & /*other*/, const CollisionInfo &) {}
    // Immediately before the runtime removes the entity or shuts down.
    virtual void onDestroy(GameContext &) {}

  private:
    friend class Entity;
    Entity *entity_{};
    const ComponentType *type_{};
};
} // namespace yk
