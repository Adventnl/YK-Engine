#pragma once
#include "yk/gameplay/Gameplay.hpp"

namespace yk {
// Free movement on a zero-gravity dynamic body. The same named action map used by platformers
// supplies each independently controlled character.
class TopDownController final : public Component {
  public:
    std::string leftAction{"MoveLeft"}, rightAction{"MoveRight"};
    std::string upAction{"MoveUp"}, downAction{"MoveDown"};
    float speed{4.0F}, acceleration{30.0F};
    static void describe(TypeBuilder<TopDownController> &type);
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onUpdate(GameContext &context, float seconds) override;

  private:
    std::string animation_;
    std::string facing_{"down"};
    Vec2 direction_{};
};

// Simple path following between waypoint entities. Empty waypoints means stand still.
class NpcPath final : public Component {
  public:
    std::vector<EntityRef> waypoints;
    float speed{1.5F};
    float waitSeconds{0.5F};
    bool loop{true};
    std::string requiredFlag; // Wait until this world-state flag is set before patrolling.
    static void describe(TypeBuilder<NpcPath> &type);
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    std::size_t next_{};
    float wait_{};
    std::string animation_;
    std::string facing_{"down"};
};

// A generic object action. It can set a persistent flag, emit an event, begin attached dialogue,
// toggle an attached gate, and/or enter a portal. Conditions and single-use behavior are data.
class Interactable final : public Component {
  public:
    std::string prompt{"Interact"};
    float range{1.5F};
    std::string requiredFlag;
    std::string setFlag;
    bool once{false};
    bool hideWhenUsed{false}; // Useful for picked-up items and spent world objects.
    std::string event;
    static void describe(TypeBuilder<Interactable> &type);
    bool available(GameContext &context) const;
    void onStart(GameContext &context) override;
    void activate(GameContext &context, Entity &actor);

  private:
    bool used_{};
};

// Finds the closest available object and uses the actor's action set. A UI text can show
// {interaction_prompt}; projects may style that prompt however they like.
class Interactor final : public Component {
  public:
    std::string action{"Interact"};
    static void describe(TypeBuilder<Interactor> &type);
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    bool inputWasLocked_{};
};

// A solid openable barrier. It may start locked and be unlocked by a kept world flag.
// Collision and visual animation follow the same open progress.
class StateGate final : public Component, public SignalReceiver {
  public:
    bool startsOpen{false};
    bool startsLocked{false};
    std::string unlockFlag;
    std::string persistFlag;
    float animationSeconds{0.35F};
    Vec2 openOffset{}; // Visual slide; zero chooses the barrier's long axis automatically.
    bool stopWhenBlocked{true};
    float proximityRange{0.0F}; // Zero keeps manual operation; positive opens for nearby actors.
    float closeDelay{0.8F};
    int openFrames{0}; // Optional closed-to-open sprite strip, synchronized with collision.
    static void describe(TypeBuilder<StateGate> &type);
    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    bool open() const {
        return open_;
    }
    bool locked(GameContext &context) const;
    void toggle(GameContext &context);

  private:
    bool open_{};
    float amount_{};
    Vec2 baseOffset_{}, slide_{};
    bool signalWasActive_{};
    float closeTimer_{};
    void setOpen(GameContext &context, bool value);
    void updateVisual();
};

class MapPortal final : public Component {
  public:
    AssetRef destination;
    std::string spawn;
    std::string requiredFlag;
    bool onTouch{false};
    std::vector<std::string> activatorTags;
    static void describe(TypeBuilder<MapPortal> &type);
    void enter(GameContext &context, Entity &actor);
    void onTriggerEnter(GameContext &context, Entity &other) override;
};

// Marks an entry point in a scene. The portal's spawn name is carried across scene changes.
class MapSpawn final : public Component {
  public:
    std::string name{"entry"};
    std::string actorTag{"player"};
    static void describe(TypeBuilder<MapSpawn> &type);
    void onStart(GameContext &context) override;
};

// Procedural breathing and stepping for single-pose character artwork.
class CharacterMotion final : public Component {
  public:
    float stepHeight{0.06F};
    float idleHeight{0.025F};
    bool animateSteps{true};
    bool flipWithMovement{true};
    static void describe(TypeBuilder<CharacterMotion> &type);
    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    Vec2 offset_{}, size_{};
    float phase_{};
};

// Publishes the actor's current room independently of the camera and screen labels.
class LocationArea final : public Component {
  public:
    EntityRef actor;
    std::string title;
    Vec2 boundsMin{}, boundsMax{1, 1};
    static void describe(TypeBuilder<LocationArea> &type);
    void onFixedUpdate(GameContext &context, float seconds) override;
};

// Ordered, data-authored objectives. The first missing flag supplies the next instruction.
class ObjectiveList final : public Component {
  public:
    std::vector<std::string> flags, instructions;
    std::string completeText{"Objectives complete."};
    static void describe(TypeBuilder<ObjectiveList> &type);
    void onFixedUpdate(GameContext &context, float seconds) override;
};

// Presentation and timed actions over the engine's real containers and crafting recipes.
// Items are transferred only after the action completes; cancellation never grants loot.
class SearchAction final : public Component {
  public:
    std::string title{"Search"}, verb{"SEARCHING"}, successText{"Search complete."};
    std::string emptyText{"Nothing left here."}, lockedText;
    std::string successFlag, recipe;
    std::vector<std::string> requiredItems;
    float duration{2.0F};
    static void describe(TypeBuilder<SearchAction> &type);
    void begin(GameContext &context, Entity &actor);
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onDestroy(GameContext &context) override;
    bool active() const {
        return active_;
    }

  private:
    void close(GameContext &context);
    void result(GameContext &context, const std::string &message);
    EntityId actor_{};
    bool active_{}, busy_{};
    float elapsed_{};
    std::uint64_t startedTick_{};
};

// Project-authored HUD, inventory presentation and a patrol's restricted-area watch.
class InventoryHud final : public Component {
  public:
    EntityRef actor;
    std::string title;
    std::string completionTitle{"COMPLETE"}, completionText;
    std::string statusText, alertText{"ALERT"}, controlsText;
    std::string continueText{"ENTER - CONTINUE     R - RESTART"};
    std::string restartText{"R - RESTART"};
    static void describe(TypeBuilder<InventoryHud> &type);
    void onFixedUpdate(GameContext &context, float seconds) override;
};
class PatrolWatch final : public Component {
  public:
    EntityRef actor;
    Vec2 restrictedMin{}, restrictedMax{1, 1}, returnPosition{};
    std::string caughtText{"Caught! Returned to the checkpoint."};
    float range{3.5F}, catchSeconds{3.0F};
    static void describe(TypeBuilder<PatrolWatch> &type);
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    float suspicion_{}, cooldown_{};
};

// Keyboard/gamepad matching puzzle. Matching all four labeled terminals commits successFlag.
class WirePuzzle final : public Component {
  public:
    std::vector<std::string> requiredFlags;
    std::string successFlag{"wires_connected"};
    std::string missingText{"Find the repair supplies first."};
    std::string inputSet{"Player1"};
    static void describe(TypeBuilder<WirePuzzle> &type);
    void begin(GameContext &context, EntityId actor);
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onDestroy(GameContext &context) override;
    bool active() const {
        return active_;
    }
    bool ready() const {
        return ready_;
    }
    int source() const {
        return source_;
    }
    int target() const {
        return target_;
    }
    int connected() const {
        return connected_;
    }
    const std::string &message() const {
        return message_;
    }
    static int targetFor(int source) {
        return (source + 2) % 4;
    }

  private:
    void close(GameContext &context);
    bool active_{}, ready_{};
    int source_{}, target_{}, connected_{};
    std::uint64_t startedTick_{};
    std::string message_;
};

void registerExplorationComponents(ComponentRegistry &registry);
} // namespace yk
