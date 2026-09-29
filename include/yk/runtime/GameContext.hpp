#pragma once
#include "yk/assets/AssetSource.hpp"
#include "yk/audio/Audio.hpp"
#include "yk/input/Input.hpp"
#include "yk/physics/World.hpp"
#include "yk/runtime/Blackboard.hpp"
#include "yk/runtime/EventBus.hpp"
#include "yk/scene/Scene.hpp"
#include <optional>

namespace yk {
// Everything a running component may use. It is an interface so components can be exercised against
// a stub, and so the scene/gameplay code never depends on the concrete runtime.
//
// Rules for hooks: never destroy entities or remove components directly (use destroyLater);
// creating entities is allowed. Physics bodies exist for the whole run: deactivate an entity to
// take its bodies out of the simulation.
class GameContext {
  public:
    virtual ~GameContext() = default;

    virtual Scene &scene() = 0;
    virtual physics::World &physics() = 0;
    // Input as seen by the current fixed tick: edges (pressed/released) appear on exactly one tick.
    virtual const Keyboard &keyboard() const = 0;
    virtual AudioSink &audio() = 0;
    virtual const AssetSource *assets() const = 0;
    virtual Blackboard &blackboard() = 0;
    virtual EventBus &events() = 0;
    virtual const LayerConfig &layers() const = 0;
    virtual float fixedDelta() const = 0;
    virtual double time() const = 0;        // Simulated seconds since start.
    virtual std::uint64_t tick() const = 0; // Fixed ticks executed.
    virtual Vec2 viewportSize() const = 0;  // Pixels of the view the game is shown in.

    // Physics <-> entity mapping (bodies exist for every entity with a RigidBody or Collider).
    virtual std::optional<physics::BodyHandle> bodyOf(EntityId entity) const = 0;
    virtual Entity *entityOfBody(physics::BodyHandle body) const = 0;
    virtual Entity *entityOfShape(physics::ShapeHandle shape) const = 0;
    // Entities currently overlapping any trigger collider of `trigger` (updated after each tick).
    virtual const std::vector<EntityId> &overlapping(EntityId trigger) const = 0;

    // Moves an entity and its body together and clears its velocity.
    virtual void teleport(Entity &entity, Vec2 worldPosition) = 0;
    virtual void destroyLater(EntityId entity) = 0;
    virtual Result<EntityId> spawn(const Json &prefab, Vec2 worldPosition,
                                   EntityId parent = {}) = 0;
    virtual void requestRestart() = 0;
    virtual void requestSceneChange(std::string projectRelativePath) = 0;

    void emit(std::string name, EntityId source = {}, EntityId other = {}) {
        events().emit({std::move(name), source, other});
    }
};
} // namespace yk
