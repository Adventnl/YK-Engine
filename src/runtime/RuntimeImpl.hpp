#pragma once
#include "yk/components/Components.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/runtime/Services.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <array>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace yk {
// Re-times frame input for fixed ticks: held keys and buttons reach every tick; press/release edges
// reach exactly one tick, even when a frame produces several ticks or none.
class InputTracker {
  public:
    void feed(const InputFrame &frame);
    void fill(InputFrame &tick); // Consumes pending edges.

  private:
    struct PadTrack {
        bool connected{};
        std::array<bool, gamepadButtonCount> held{}, pressed{}, released{};
        std::array<float, gamepadAxisCount> axes{};
    };
    std::array<bool, keyCount> held_{}, pressed_{}, released_{};
    std::array<PadTrack, maxGamepads> pads_{};
};

struct GameRuntime::Impl {
    explicit Impl(GameRuntime &owner) : self(owner), services(owner) {}

    GameRuntime &self;
    RuntimeOptions options;
    std::unique_ptr<Scene> scene;
    Json startState;
    std::unique_ptr<physics::World> world;
    Blackboard blackboard;
    EventBus events;
    // Declared after everything a service may use in its shutdown, so it is destroyed first.
    Services services;
    NullAudio nullAudio;
    InputFrame tickInput;
    ActionInput actions;
    InputTracker input;
    double time{};
    std::uint64_t ticks{};
    double accumulator{};
    bool paused{};
    bool restartWanted{};
    std::string sceneChange;
    // A restart or scene change fades the screen out, does its work while it is covered, and fades
    // back in. It outlives rebuild(): the restart happens in the middle of it.
    enum class Phase { Idle, Out, Covered, In };
    Phase phase{Phase::Idle};
    float phaseTime{};
    float fade{};
    bool restartAfterFade{};
    std::string sceneAfterFade;
    std::set<std::string> inputLocks; // Reasons the game is locked (see GameContext::lockInput).
    bool announced{};                 // "scene_started" has been raised.
    // The order components run in: every component of the scene, and the same split by update
    // phase, rebuilt when the scene's structure changes. Pointers stay valid because entities are
    // only destroyed between passes (destroyLater).
    struct ComponentSchedule {
        std::uint64_t revision{~std::uint64_t{0}};
        std::vector<Component *> all;
        std::array<std::vector<Component *>, updatePhaseCount> byPhase;
    } schedule;
    float alpha{1.0F}; // How far between the last two ticks the picture is (render interpolation).
    Vec2 viewport{1280, 720};
    std::vector<EntityId> destroyQueue;
    std::unordered_set<const Component *> started;
    // Assets shared by every entity; kept across restarts (the files do not change while running).
    std::map<std::string, std::shared_ptr<const AnimationSet>> animationSets;
    std::map<std::string, std::shared_ptr<const AnimationController>> animationControllers;
    std::map<std::string, Json> prefabDocuments;
    std::unordered_set<std::string> failedPrefabs; // Reported once, then quietly refused.

    // ---- Physics binding (PhysicsBinding.cpp) ----
    struct BodyRecord {
        physics::BodyHandle body;
        EntityId entity;
        physics::BodyType type{physics::BodyType::Static};
        bool implicit{}; // Created for colliders that have no RigidBody in their hierarchy.
        bool enabled{true};
    };
    struct ShapeRecord {
        physics::ShapeHandle handle;
        EntityId entity;     // Entity owning the Collider component.
        EntityId bodyEntity; // Entity owning the body the shape is attached to.
        const Collider *collider{};
        physics::CollisionFilter filter;
        bool trigger{};
        bool enabled{true};
    };
    std::unordered_map<EntityId, BodyRecord> bodies;
    std::unordered_map<std::uint64_t, EntityId> entityByBody; // Body serial -> owning entity.
    std::unordered_map<std::uint64_t, ShapeRecord> shapes;    // Shape serial -> record.
    std::unordered_map<EntityId, std::vector<std::uint64_t>> shapesByEntity;
    std::vector<std::uint64_t> triggerShapes;           // Creation order, for stable callbacks.
    std::map<EntityId, std::vector<EntityId>> overlaps; // Trigger entity -> visitors.
    std::vector<EntityId> noOverlaps;
    // The immovable body a HingeJoint with no connected entity is pinned to (owned by the runtime,
    // destroyed with the hinge's entity).
    std::unordered_map<EntityId, physics::BodyHandle> hingeAnchors;

    Status buildWorld();
    void bindEntities(const std::vector<EntityId> &ids);
    void unbindEntities(const std::vector<EntityId> &ids);
    BodyRecord *ensureBody(Entity &owner, bool implicit);
    void bindHinge(Entity &entity, const HingeJoint &hinge);
    void syncActivation();
    void syncTransforms();
    void updateTriggers();
    void dispatchCollisions();

    // ---- Loop (GameRuntime.cpp) ----
    Status rebuild(std::unique_ptr<Scene> fresh);
    void shutdown();
    void startPending();
    void fixedTick();
    void runPhase(UpdatePhase which, float step);
    const ComponentSchedule &componentSchedule();
    void captureInterpolation();
    void advanceTransition(float seconds);
    void beginTransition(bool restart, std::string nextScene);
    void variableUpdate(float seconds);
    void flushDestroys();
    void finishFrame();
    void notifyTrigger(bool enter, Entity &owner, Entity &visitor);

    // Calls `visit` for every enabled component of every entity that is active in the hierarchy, in
    // hierarchy order. Hooks may create entities (their components join the next pass) but must not
    // remove any.
    template <class Visit> void forEachComponent(Visit &&visit) {
        for (Component *component : std::vector<Component *>(componentSchedule().all)) {
            if (component->enabled && component->entity().activeInHierarchy())
                visit(*component);
        }
    }
};
} // namespace yk
