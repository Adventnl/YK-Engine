#pragma once
#include "yk/core/Math.hpp"
#include <cstddef>
#include <cstdint>

namespace yk {
class Entity;
class Scene;
class GameContext;
struct ComponentType;

// When, within one fixed tick, a component type's onFixedUpdate runs (and, for services, when they
// do). The order is fixed and documented in ARCHITECTURE.md ("Update order"): the phases up to
// Steering run before the physics step, Perception and PostSimulation after it (positions, contacts
// and triggers are up to date). Within a phase components run in hierarchy order. Every component
// that existed before phases did is Gameplay, so its behaviour is unchanged.
enum class UpdatePhase : std::uint8_t {
    Clock,          // world time and calendars: before anything reads the time of day
    PreUpdate,      // scripts and rules that react to what happened last tick
    Decision,       // AI and schedule agents choose goals and targets
    Gameplay,       // the default: controllers, mechanisms, movement intent
    Steering,       // navigation agents and avoidance turn goals into movement intents
    Motor,          // character motors turn intents into velocities, just before the step
    Perception,     // after physics: sight, hearing and awareness see where everything ended up
    PostSimulation, // stat regeneration, status expiry, security timers
};
inline constexpr std::size_t updatePhaseCount = 8;
inline constexpr bool runsBeforePhysics(UpdatePhase phase) {
    return phase <= UpdatePhase::Motor;
}
const char *updatePhaseName(UpdatePhase phase);

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
