#pragma once
#include "yk/runtime/GameContext.hpp"
#include <memory>
#include <string>

namespace yk {
struct RuntimeOptions {
    double fixedSeconds{1.0 / 60.0};
    unsigned maxStepsPerFrame{8}; // Beyond this, simulated time is dropped rather than spiraling.
    Vec2 viewportSize{1280, 720};
    LayerConfig layers{LayerConfig::defaults()};
    InputMap inputMap{InputMap::standard()};
    AudioSink *audio{nullptr};          // Borrowed; nullptr discards sound requests.
    const AssetSource *assets{nullptr}; // Borrowed; nullptr means no asset access.
};

// Executes a scene: builds the physics world from RigidBody/Collider components, then runs fixed
// ticks (component fixed updates, physics, transform write-back, trigger/collision callbacks,
// events) and per-frame updates. Fully headless: no window, renderer or audio device is needed, so
// the same code drives the player, the editor's play mode and automated tests.
class GameRuntime final : public GameContext {
  public:
    // Takes ownership of the scene. The scene's current state is remembered so restart() can
    // rebuild it exactly.
    static Result<std::unique_ptr<GameRuntime>> create(std::unique_ptr<Scene> scene,
                                                       RuntimeOptions options = {});
    ~GameRuntime() override;

    // Frame driver: accumulates time into fixed ticks (bounded catch-up), then runs one variable
    // update. Key and button edges in `input` are delivered to exactly one tick. The Keyboard
    // overloads are for callers with no gamepads (tests, scripted input).
    void update(double frameSeconds, const InputFrame &input);
    void update(double frameSeconds, const Keyboard &keyboard);
    // Deterministic single step for tests and tools: exactly one fixed tick plus one update.
    void stepOnce(const InputFrame &input);
    void stepOnce(const Keyboard &keyboard);

    // Rebuilds the scene from its start state and clears variables and events. requestRestart()
    // does this automatically at the end of the current update, never in the middle of a hook.
    Status restart();
    // Set by requestSceneChange; the host (player or editor) decides what to do with it.
    const std::string &sceneChangeRequested() const;
    void clearSceneChangeRequest();

    void setPaused(bool paused);
    bool paused() const;
    void setViewportSize(Vec2 pixels);

    // GameContext
    Scene &scene() override;
    physics::World &physics() override;
    const Keyboard &keyboard() const override;
    const ActionInput &input() const override;
    AudioSink &audio() override;
    const AssetSource *assets() const override;
    Blackboard &blackboard() override;
    EventBus &events() override;
    const LayerConfig &layers() const override;
    float fixedDelta() const override;
    double time() const override;
    std::uint64_t tick() const override;
    Vec2 viewportSize() const override;
    std::optional<physics::BodyHandle> bodyOf(EntityId entity) const override;
    Entity *entityOfBody(physics::BodyHandle body) const override;
    Entity *entityOfShape(physics::ShapeHandle shape) const override;
    std::vector<physics::ShapeHandle> bodyShapes(EntityId entity) const override;
    const std::vector<EntityId> &overlapping(EntityId trigger) const override;
    void teleport(Entity &entity, Vec2 worldPosition) override;
    void destroyLater(EntityId entity) override;
    Result<EntityId> spawn(const Json &prefab, Vec2 worldPosition, EntityId parent = {}) override;
    void requestRestart() override;
    void requestSceneChange(std::string projectRelativePath) override;

    const physics::World &physics() const;

  private:
    GameRuntime();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace yk
