#pragma once
#include "yk/components/Components.hpp"
#include "yk/runtime/GameContext.hpp"
#include <map>
#include <string>
#include <vector>

// Reusable 2D gameplay building blocks. Nothing here knows about any particular game: a designer
// wires them together in the editor (plates -> doors, hazards that hurt tagged entities, exits that
// need certain characters) and the same components serve any platformer or puzzle game.
namespace yk {
// Collision layer names the built-in entity templates use. Projects define them (the editor's new
// project does); an undefined name falls back to layer 0 with a logged warning.
namespace layers {
inline constexpr const char *solid = "Solid";   // Level geometry.
inline constexpr const char *player = "Player"; // Characters.
inline constexpr const char *sensor = "Sensor"; // Triggers, hazards, pickups, goals.
inline constexpr const char *prop = "Prop";     // Pushable objects.
} // namespace layers

// ----- Signals -----------------------------------------------------------------------------
// Sources (plates, levers, goals, zones) list `targets`; receivers (doors, platforms) combine all
// the sources that reported to them. Sources report every tick, so receivers never miss a change
// and never depend on update order.
enum class SignalLogic { Any, All };
const std::vector<std::string> &signalLogicNames();

class SignalReceiver {
  public:
    virtual ~SignalReceiver() = default;
    SignalLogic logic{
        SignalLogic::Any}; // Any: one active source is enough. All: every source must be.
    bool invert{false};    // Flip the combined result.

    void setSignal(EntityId source, bool active);
    // Combined state; with no reporting source it is false (true when inverted).
    bool signalActive() const;
    std::size_t sourceCount() const {
        return inputs_.size();
    }

  private:
    std::map<EntityId, bool> inputs_;
};
// Pushes `active` to every SignalReceiver on the target entities (unknown targets are skipped).
void sendSignal(Scene &scene, EntityId source, const std::vector<EntityRef> &targets, bool active);

// True when `entity` may trigger something filtered by `tags`: with tags, it must carry one; with
// no tags, it must have a movable (non-static) body, so level geometry never trips a plate by
// accident.
bool matchesActivator(const Entity &entity, const std::vector<std::string> &tags);

// Drives a kinematic body toward `worldTarget` within one tick (so riders are carried), or moves
// the entity directly when it has no body.
void moveKinematic(GameContext &context, Entity &entity, Vec2 worldTarget);

// ----- Character -----------------------------------------------------------------------------
// Health-less "can be killed, then comes back": disables the entity's body, colliders and sprite
// while dead and returns it to its spawn point (or last checkpoint) after a delay.
class Killable final : public Component {
  public:
    bool respawn{true};
    float respawnDelay{1.0F};
    EntityRef
        spawnPoint; // Where to return; otherwise the last checkpoint, else the start position.
    AssetRef deathSound;
    AssetRef respawnSound;
    static void describe(TypeBuilder<Killable> &type);

    bool alive() const {
        return alive_;
    }
    void kill(GameContext &context, EntityId killer);
    void setRespawnPoint(Vec2 worldPosition);
    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    void setPresent(bool present);
    bool alive_{true};
    float timer_{};
    Vec2 home_{};
    Vec2 checkpoint_{};
    bool hasCheckpoint_{};
};

// Side-view character movement on a dynamic body: acceleration-based running, variable-height
// jumps, coyote time, jump buffering, slopes, moving platforms. Keys are properties, so two
// characters with different keys need no code.
class PlatformerController final : public Component {
  public:
    Key leftKey{Key::A};
    Key rightKey{Key::D};
    Key jumpKey{Key::W};
    float moveSpeed{5.5F};           // m/s
    float groundAcceleration{70.0F}; // m/s^2 while a direction is held on the ground
    float groundDeceleration{80.0F}; // m/s^2 when no direction is held on the ground
    float airAcceleration{40.0F};
    float airDeceleration{6.0F}; // Low, so a jump keeps its momentum.
    float jumpHeight{2.3F};      // Apex above the takeoff point, meters.
    float fallGravityMultiplier{1.35F};
    float jumpCutMultiplier{0.45F}; // Vertical speed kept when the jump key is released early.
    float coyoteTime{0.1F};         // Grace period to jump after walking off an edge.
    float jumpBufferTime{0.12F};    // A jump pressed this early before landing still happens.
    float maxFallSpeed{20.0F};
    float maxSlopeDegrees{55.0F};
    float gripFriction{1.2F};  // Friction when standing still on the ground.
    float slideFriction{0.0F}; // Friction when moving or airborne (no wall sticking).
    AssetRef jumpSound;
    static void describe(TypeBuilder<PlatformerController> &type);

    bool grounded() const {
        return grounded_;
    }
    int facing() const {
        return facing_;
    }
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    bool grounded_{};
    bool wasGrounded_{};
    Vec2 lastGroundVelocity_{};
    int facing_{1};
    float coyote_{};
    float jumpBuffer_{};
    bool jumping_{};
    bool gripping_{};
    bool frictionApplied_{};
    float appliedGravityScale_{-1.0F};
};

// ----- Mechanisms ----------------------------------------------------------------------------
class PressurePlate final : public Component {
  public:
    std::vector<EntityRef> targets;
    std::vector<std::string> activatorTags; // Empty: any movable body presses it.
    bool latch{false};                      // Stay pressed once triggered.
    Color pressedColor{90, 220, 110, 255};  // The sprite's own color is the idle look.
    float pressDepth{0.08F};                // The sprite sinks by this much while pressed.
    AssetRef pressSound;
    AssetRef releaseSound;
    static void describe(TypeBuilder<PressurePlate> &type);

    bool pressed() const {
        return pressed_;
    }
    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    void applyVisuals();
    bool pressed_{};
    float baseOffsetY_{};
    Color baseColor_{};
};

class Lever final : public Component {
  public:
    std::vector<EntityRef> targets;
    std::vector<std::string> activatorTags;
    bool startsOn{false};
    float cooldown{0.4F};             // Seconds before it can be flipped again.
    Color onColor{90, 220, 110, 255}; // The sprite's own color is the off look.
    AssetRef sound;
    static void describe(TypeBuilder<Lever> &type);

    bool on() const {
        return on_;
    }
    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onTriggerEnter(GameContext &context, Entity &other) override;

  private:
    void applyVisuals();
    bool on_{};
    float cooldown_{};
    Color baseColor_{};
};

// A gate: slides by `openOffset` while its combined signal is active (or, with startsOpen, while it
// is inactive). Needs a kinematic body so it carries and blocks characters correctly.
class Door final : public Component, public SignalReceiver {
  public:
    Vec2 openOffset{0.0F, -3.0F}; // World-space displacement when open.
    float speed{3.0F};            // m/s
    bool startsOpen{false};
    AssetRef openSound;
    AssetRef closeSound;
    static void describe(TypeBuilder<Door> &type);

    // 0 closed .. 1 open.
    float openAmount() const {
        return amount_;
    }
    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    Vec2 closedPosition_{};
    float amount_{};
    bool opening_{};
    bool started_{};
};

// Shuttles between its start and start + travel on a kinematic body, carrying whatever stands on
// it.
class MovingPlatform final : public Component, public SignalReceiver {
  public:
    Vec2 travel{4.0F, 0.0F};
    float speed{2.0F};
    float pause{0.5F};         // Seconds to wait at each end.
    bool requireSignal{false}; // Only move while the combined signal is active.
    static void describe(TypeBuilder<MovingPlatform> &type);

    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    Vec2 start_{};
    float t_{};
    float direction_{1.0F};
    float waiting_{};
};

// ----- Interactions --------------------------------------------------------------------------
class Hazard final : public Component {
  public:
    std::vector<std::string> affectsTags; // Empty: kills anything Killable.
    static void describe(TypeBuilder<Hazard> &type);
    void onTriggerEnter(GameContext &context, Entity &other) override;
};

class Collectible final : public Component {
  public:
    std::vector<std::string> collectorTags; // Empty: any movable body collects it.
    std::string variable{"score"};          // Blackboard variable to increase.
    float value{1.0F};
    AssetRef sound;
    static void describe(TypeBuilder<Collectible> &type);
    void onTriggerEnter(GameContext &context, Entity &other) override;
};

class Checkpoint final : public Component {
  public:
    std::vector<std::string> activatorTags;
    Vec2 respawnOffset{0.0F, -0.5F}; // Added to the checkpoint's position.
    Color activeColor{255, 220, 80, 255};
    AssetRef sound;
    static void describe(TypeBuilder<Checkpoint> &type);
    void onTriggerEnter(GameContext &context, Entity &other) override;
};

// A marker: where a character starts and returns to. Assign `character` to place it there at start.
class SpawnPoint final : public Component {
  public:
    EntityRef character;
    static void describe(TypeBuilder<SpawnPoint> &type);
    void onStart(GameContext &context) override;
};

// An exit: satisfied while an entity carrying `requiredTag` (and alive) stands in it.
class Goal final : public Component {
  public:
    std::string requiredTag;
    std::vector<EntityRef> targets;
    Color satisfiedColor{90, 220, 110, 255}; // The sprite's own color is the idle look.
    AssetRef sound;
    static void describe(TypeBuilder<Goal> &type);

    bool satisfied() const {
        return satisfied_;
    }
    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    void applyVisuals();
    bool satisfied_{};
    Color baseColor_{};
};

// A trigger region that raises named events and drives targets while occupied.
class TriggerZone final : public Component {
  public:
    std::vector<std::string> filterTags;
    std::string enterEvent;
    std::string exitEvent;
    bool once{false}; // Raise the events for the first visit only.
    std::vector<EntityRef> targets;
    static void describe(TypeBuilder<TriggerZone> &type);

    bool occupied() const {
        return occupied_;
    }
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    bool occupied_{};
    bool finished_{};
};

// Registers every component above plus generic entity templates (Platform, Door, ...).
void registerGameplayComponents(ComponentRegistry &registry);
} // namespace yk
