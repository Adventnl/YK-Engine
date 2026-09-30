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

  private:
    std::string animation_;
    std::string facing_{"down"};
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
    std::uint8_t baseAlpha_{255};
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

void registerExplorationComponents(ComponentRegistry &registry);
} // namespace yk
