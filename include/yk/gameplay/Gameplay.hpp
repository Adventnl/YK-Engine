#pragma once
#include "yk/assets/Project.hpp"
#include "yk/components/Components.hpp"
#include "yk/runtime/GameContext.hpp"
#include <algorithm>
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
// The layer set the entity templates expect: Default plus the four above, with characters, props
// and sensors interacting sensibly. Characters pass through each other (no Player <-> Player),
// which keeps two-player puzzles from jamming; enable it in the layer matrix to let them collide.
LayerConfig standard();
} // namespace layers

// Creates a project folder ready to work in: `scenes/main.ykscene` (an empty scene with the camera
// the game looks through), `prefabs/`, `assets/`, the standard collision layers, the standard input
// sets (WASD and pad 1, arrows and pad 2, restart and pause) and the project file. The editor's
// New Project and `yk new` both call this. Fails when the folder already holds a project.
Result<Project> createProject(const std::filesystem::path &directory, const std::string &name,
                              const ComponentRegistry &registry);

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

// True when the entity has a trigger Collider: the zone that lets a lever, goal, hazard or pickup
// notice what enters it. The components' validation uses it to say what a scene is missing.
bool hasTriggerCollider(const Entity &entity);

// Spawns an effect prefab at `worldPosition` when `prefab` names one; a missing or invalid prefab
// is reported once by the runtime and otherwise ignored, so effects can never break gameplay.
void spawnEffect(GameContext &context, const AssetRef &prefab, Vec2 worldPosition);

// Drives a kinematic body toward `worldTarget` within one tick (so riders are carried), or moves
// the entity directly when it has no body. With `worldRotationDegrees` it also turns toward that
// angle by the shortest way (the body's angular velocity is set, so what stands on it is carried
// around too).
void moveKinematic(GameContext &context, Entity &entity, Vec2 worldTarget);
void moveKinematic(GameContext &context, Entity &entity, Vec2 worldTarget,
                   float worldRotationDegrees);

// True when something is squeezed between the moving `entity` and something solid on the far side
// as it moves toward `direction`: a character standing under a gate that is closing, a crate under
// a platform that rises into a ceiling. Something merely riding the entity (a character on top of a
// rising platform) is carried, not squeezed, and does not count. Doors and platforms hold still
// while this is true, instead of pushing what is caught through the floor. The physics reports a
// contact only when the surfaces are about to touch, so a fast door can reach a few centimeters
// into what is in its way before it notices; the solver pushes that back out.
bool pathBlocked(GameContext &context, const Entity &entity, Vec2 direction);

// ----- Character -----------------------------------------------------------------------------
// Health-less "can be killed, then comes back". Dying takes the entity's body and colliders out of
// the world at once, keeps its sprite visible for `deathDuration` so a death animation can play
// (it sets the AnimatedSprite parameter `dead`), then hides it; after `respawnDelay` it returns to
// its spawn point (or last checkpoint), sets `dead` back and raises the trigger `respawned`.
class Killable final : public Component {
  public:
    bool respawn{true};
    float respawnDelay{1.0F};
    float deathDuration{0.6F}; // Seconds the sprite stays visible after dying.
    EntityRef
        spawnPoint; // Where to return; otherwise the last checkpoint, else the start position.
    AssetRef deathSound;
    AssetRef respawnSound;
    AssetRef deathEffect;   // Prefab spawned where it died.
    AssetRef respawnEffect; // Prefab spawned where it returns.
    static void describe(TypeBuilder<Killable> &type);

    bool alive() const {
        return alive_;
    }
    void kill(GameContext &context, EntityId killer);
    void setRespawnPoint(Vec2 worldPosition);
    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    void setSolid(bool solid);
    void setVisible(bool visible);
    bool alive_{true};
    bool dying_{};
    float dyingTimer_{};
    float timer_{};
    Vec2 home_{};
    Vec2 checkpoint_{};
    bool hasCheckpoint_{};
};

// Side-view character movement on a dynamic body: acceleration-based running, variable-height
// jumps, coyote time, jump buffering, slopes, moving platforms. It reads named actions from the
// entity's PlayerInput, so two characters with different keys (or a gamepad) need no code.
//
// It tells the entity's AnimatedSprite what the character is doing through generic parameters and
// never names a clip: `speed` (m/s along the ground), `speedRatio` (speed / moveSpeed), `moveInput`
// (-1..1), `velocityY` (m/s, positive down), `grounded` and `facing` (+1 right, -1 left), and the
// triggers `jumped` and `landed`. The animation controller asset decides what to play.
class PlatformerController final : public Component {
  public:
    std::string moveLeftAction{"MoveLeft"};
    std::string moveRightAction{"MoveRight"};
    std::string jumpAction{"Jump"};
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
    float landingSpeed{4.0F};  // Downward speed (m/s) at which touching down counts as a "landed".
    float groundSnap{0.3F};    // Walking off a ramp crest or down a slope keeps the feet on the
                               // ground when it is at most this far below; 0 turns that off.
    AssetRef jumpSound;
    AssetRef landSound;
    AssetRef jumpEffect; // Prefab spawned at the feet when a jump starts (dust, for example).
    AssetRef landEffect; // Prefab spawned at the feet after a landing.
    static void describe(TypeBuilder<PlatformerController> &type);

    bool grounded() const {
        return grounded_;
    }
    int facing() const {
        return facing_;
    }
    void onStart(GameContext &context) override;
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
    float fallSpeed_{}; // Fastest downward speed since leaving the ground.
    bool wasGroundedForAnimation_{true};
    bool hadGround_{};
    float appliedGravityScale_{-1.0F};
};

// ----- Mechanisms ----------------------------------------------------------------------------
// How a plate decides that it is pressed.
//   Weight  something solid rests on its `pad`: the pad's own contacts are the load, so a jumping
//           character lands on the plate, stands on it and pushes it down.
//   Region  something overlaps a trigger collider of the plate (or its pad): an activation zone
//           that has nothing to do with what blocks movement.
//   Auto    Weight when the plate has a pad with a solid collider and no trigger collider of its
//           own, Region otherwise (what the older plates, which were only a zone, need).
enum class PlateSensing { Auto, Weight, Region };
const std::vector<std::string> &plateSensingNames();

// While pressed it drives its targets. The plate can be a real object: give `pad` (this entity when
// empty) a kinematic RigidBody and a solid Collider and the pad is a surface characters and props
// stand on. It sinks by `pressDepth` under the load, carrying it down, stops at the bottom, and
// rises when the load is gone. With an AnimatedSprite the plate publishes `pressed` (bool) and
// `pressAmount` (0 up .. 1 fully down) so art can follow the motion; a plate with neither pad nor
// AnimatedSprite tints and sinks its sprite.
class PressurePlate final : public Component {
  public:
    std::vector<EntityRef> targets;
    std::vector<std::string> activatorTags; // Empty: any movable body presses it.
    bool latch{false};                      // Stay pressed once triggered.
    PlateSensing sensing{PlateSensing::Auto};
    EntityRef pad;                         // The part that moves and carries; empty: this entity.
    Color pressedColor{90, 220, 110, 255}; // The sprite's own color is the idle look.
    float pressDepth{0.08F};               // How far the plate sinks when pressed, world units.
    float pressSpeed{1.5F};                // Fastest the plate sinks or rises, m/s.
    float acceleration{8.0F};              // m/s^2; below gravity, so a load never loses contact.
    float minimumMass{0.0F};               // Weight sensing: kg that must rest on the pad.
    AssetRef pressSound;
    AssetRef releaseSound;
    static void describe(TypeBuilder<PressurePlate> &type);

    bool pressed() const {
        return pressed_;
    }
    // How far down the plate is, 0 (up) .. 1 (fully pressed).
    float pressAmount() const {
        return pressDepth > 1e-5F ? std::clamp(depth_ / pressDepth, 0.0F, 1.0F)
                                  : (pressed_ ? 1.0F : 0.0F);
    }
    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    enum class Mode { Region, Weight };
    bool senseWeight(GameContext &context, Entity &padEntity) const;
    bool senseRegion(GameContext &context) const;
    void applyVisuals(GameContext &context);
    Mode mode_{Mode::Region};
    bool pressed_{};
    bool loaded_{};       // Something is on the plate (or in its zone) right now.
    float unloadedFor_{}; // Seconds since the last tick with a load.
    float depth_{};       // World units the pad is down.
    float speed_{};       // Signed speed of the pad along the press direction.
    Vec2 restLocal_{};    // The pad's position in its parent's frame when it is up.
    bool bodyDriven_{};
    float baseOffsetY_{};
    Color baseColor_{};
};

// Flips between on and off. By default that happens when a character touches it; with an
// `interactAction` it happens when a character standing in it presses that action of its own
// PlayerInput set (and sets the character's `interact` animation trigger). With an AnimatedSprite
// it publishes the parameter `on`; without one it tints its sprite.
class Lever final : public Component {
  public:
    std::vector<EntityRef> targets;
    std::vector<std::string> activatorTags;
    std::string interactAction; // Empty: flips on touch.
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
    void flip(GameContext &context, Entity &by);
    bool on_{};
    float cooldown_{};
    Color baseColor_{};
};

// A gate: slides by `openOffset` and/or turns by `openRotation` about its origin while its combined
// signal is active (or, with startsOpen, while it is inactive). Sliding doors, hinged doors,
// drawbridges and the handle of a lever are the same thing. Needs a kinematic body so it carries
// and blocks characters correctly.
class Door final : public Component, public SignalReceiver {
  public:
    Vec2 openOffset{0.0F, -3.0F}; // World-space displacement when open.
    float openRotation{0.0F};     // Degrees it turns about its origin when open (a hinged door).
    bool stopWhenBlocked{true};   // Hold still while something is caught in its way.
    float speed{3.0F};            // m/s along the offset.
    float rotationSpeed{90.0F};   // Degrees per second of the turn.
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
    float closedRotation_{};
    float amount_{};
    bool opening_{};
    bool started_{};
};

// Shuttles between its start and start + travel on a kinematic body, carrying whatever stands on
// it, and can turn about its origin as it goes (a rotating platform: `travel` zero, `spinSpeed`
// set). Riders move with the point of the platform they stand on.
class MovingPlatform final : public Component, public SignalReceiver {
  public:
    Vec2 travel{4.0F, 0.0F};
    float speed{2.0F};
    float acceleration{8.0F};   // m/s^2 speeding up and slowing down; below gravity so that props
                                // riding a platform that starts downward stay on it.
    float spinSpeed{0.0F};      // Degrees per second it turns about its origin while moving.
    float pause{0.5F};          // Seconds to wait at each end.
    bool requireSignal{false};  // Only move while the combined signal is active.
    bool stopWhenBlocked{true}; // Hold still while something is squeezed in its way.
    static void describe(TypeBuilder<MovingPlatform> &type);

    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    Vec2 start_{};
    float startRotation_{};
    float spin_{}; // Degrees turned since the start.
    float t_{};
    float speed_{}; // Current speed along the path, m/s (never negative; direction_ has the sign).
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
    AssetRef collectEffect; // Prefab spawned where it was picked up (a sparkle).
    static void describe(TypeBuilder<Collectible> &type);
    // Adds `value` to `<variable>_total`, so UI text can show "{gems}/{gems_total}".
    void onStart(GameContext &context) override;
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

// An exit: satisfied while an entity carrying `requiredTag` (and alive) stands in it. When the
// level completes (LevelFlow), whoever stands in it walks into it and vanishes over `exitDuration`
// seconds (`enterOnComplete`): the AnimatedSprite parameter `exiting` is set on them meanwhile, so
// art can play a "going through the door" clip.
class Goal final : public Component {
  public:
    std::string requiredTag;
    std::vector<EntityRef> targets;
    Color satisfiedColor{90, 220, 110, 255}; // The sprite's own color is the idle look.
    AssetRef sound;
    bool enterOnComplete{true};
    float exitDuration{0.6F};
    static void describe(TypeBuilder<Goal> &type);

    bool satisfied() const {
        return satisfied_;
    }
    // Starts the walk into the exit for whoever is in it. LevelFlow calls it on completion.
    void beginExit(GameContext &context);
    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    struct Visitor {
        EntityId entity;
        Vec2 from;
    };
    void applyVisuals();
    bool satisfied_{};
    Color baseColor_{};
    std::vector<Visitor> leaving_;
    float leavingTime_{};
    bool exiting_{};
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

// ----- Level rules ---------------------------------------------------------------------------
// The rules of a level, as a small state machine:
//
//   intro     (optional) the level shows `introMessage` and the game ignores input for
//             `introDuration` seconds;
//   playing   the clock runs; the level completes when every listed Goal is satisfied at once, and
//             fails when anyone dies (`restartOnDeath`) or `timeLimit` runs out;
//   complete  input is locked (`lockInputOnComplete`), whoever stands in an exit walks into it
//             (Goal::beginExit), `completeMessage` shows, and after `completeDelay` seconds the
//             game moves on to `nextScene` (staying put when it is empty);
//   failed    `failMessage` shows and after `restartDelay` seconds the level starts over.
//
// A person can press `restartAction` at any time to start over, and `continueAction` on the
// complete or failed screen to move on without waiting. Restarts and scene changes fade the screen
// (the host sets the runtime's transition time). It publishes `level_state` ("intro", "playing",
// "complete", "failed"), `level_message`, `level_time` (whole seconds played) and `level_time_left`
// (with a time limit) to the Blackboard, so UiText can show them, and raises "level_started",
// "level_completed" and "level_failed". `keepVariables` names the Blackboard variables (a score,
// the gems collected) that are carried into the next scene.
class LevelFlow final : public Component {
  public:
    std::vector<EntityRef> goals;
    float introDuration{0.0F};
    std::string introMessage;
    bool restartOnDeath{false};
    float timeLimit{0.0F}; // Seconds; 0 is no limit.
    bool lockInputOnComplete{true};
    float restartDelay{1.5F};
    float completeDelay{2.5F};
    std::string nextScene; // Project-relative scene to load after completion; empty stays.
    std::string restartSet{"Global"};
    std::string restartAction{"Restart"};
    std::string continueAction; // Skips the wait on the complete or failed screen. Empty: none.
    std::string completeMessage{"LEVEL COMPLETE!"};
    std::string failMessage{"TRY AGAIN"};
    std::string timeUpMessage{"TIME'S UP!"};
    std::vector<std::string> keepVariables;
    AssetRef completeSound;
    AssetRef failSound;
    static void describe(TypeBuilder<LevelFlow> &type);

    bool completed() const {
        return state_ == State::Complete;
    }
    bool failed() const {
        return state_ == State::Failed;
    }
    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onDestroy(GameContext &context) override;

  private:
    enum class State { Intro, Playing, Complete, Failed };
    void fail(GameContext &context, const std::string &message);
    void publish(GameContext &context, const char *state, const std::string &message);
    State state_{State::Playing};
    float timer_{};
    float elapsed_{};
    EventBus::Subscription deathSubscription_{};
};

// ----- Reactions -----------------------------------------------------------------------------
enum class SignalChange { None, Set, Clear, Toggle };
enum class VariableChange { None, Add, Set };
const std::vector<std::string> &signalChangeNames();
const std::vector<std::string> &variableChangeNames();

// "When this happens, after that long, do these things": the glue between events that mechanisms
// raise ("plate_pressed", "lever_toggled", "collected", "goal_reached", "level_completed",
// "scene_started"...) and what should follow, with no code. It waits for `onEvent` (optionally only
// from `from`), lets `delay` seconds pass, and then does whatever it is set up to do, in this
// order: change the signal it holds (doors, platforms and other receivers listing it as a target
// follow that signal), raise another event, switch entities on and off, set a trigger on entities'
// animations, change a game variable, play a sound, restart the level or go to another scene. With
// no `onEvent` and an `every` it is a repeating timer; `scene_started` plus a `delay` is a
// one-shot timer that starts with the level.
class EventAction final : public Component {
  public:
    std::string onEvent;
    EntityRef from;    // Only events raised by this entity count. Empty: any.
    float delay{0.0F}; // Seconds from the event to the actions.
    float every{0.0F}; // With no onEvent: run the actions every this many seconds.
    bool once{false};  // Stop after running once.
    SignalChange signal{SignalChange::None};
    std::vector<EntityRef> targets; // Receivers that follow the signal this component holds.
    std::string raiseEvent;
    std::vector<EntityRef> activate;
    std::vector<EntityRef> deactivate;
    std::vector<EntityRef> animate;
    std::string animationTrigger; // Set on the AnimatedSprite of each `animate` entity.
    VariableChange variableChange{VariableChange::None};
    std::string variable;
    float amount{1.0F};
    AssetRef sound;
    bool restartLevel{false};
    std::string changeScene;
    static void describe(TypeBuilder<EventAction> &type);

    // The signal it holds right now (what `targets` follow).
    bool signalOn() const {
        return signal_;
    }
    int runCount() const {
        return runs_;
    }
    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onDestroy(GameContext &context) override;

  private:
    void run(GameContext &context, EntityId other);
    struct Pending {
        double due;
        EntityId other;
    };
    std::vector<Pending> pending_;
    EventBus::Subscription subscription_{};
    bool signal_{};
    bool finished_{};
    float timer_{};
    int runs_{};
};

// Registers every component above plus generic entity templates (Platform, Door, ...).
void registerGameplayComponents(ComponentRegistry &registry);
// Everything the stock tools know: the engine's standard components, effects and this library. The
// editor, the player and the `yk` command line start from it; a game with its own C++ components
// registers them after it (see docs/BUILDING.md, "Game modules").
void registerStandardComponents(ComponentRegistry &registry);
} // namespace yk
