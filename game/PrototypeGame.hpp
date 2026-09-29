#pragma once
#include "yk/gameplay/Gameplay.hpp"

// The Elemental Prototype: a two-character cooperative platformer used to validate the engine. This
// is a game module: it depends on the engine and gameplay libraries, never the other way around. The
// editor and the player link it so its components and entity templates appear in the Add Component
// and Create menus.
namespace yk::prototype {
// Tags that give characters their elements. Hazards and exits filter on them.
inline constexpr const char *fireTag = "fire";
inline constexpr const char *waterTag = "water";

// Level rules: completes when every goal is satisfied, optionally restarts the level when anyone
// dies, publishes level_state / level_message / level_time to the Blackboard for HUD text, and
// restarts on a key press.
class LevelFlow final : public Component {
  public:
    std::vector<EntityRef> goals;
    bool restartOnDeath{false};
    float restartDelay{1.5F};
    float completeDelay{2.5F};
    std::string nextScene; // Project-relative scene to load after completion; empty stays.
    Key restartKey{Key::R};
    std::string completeMessage{"LEVEL COMPLETE!"};
    std::string failMessage{"TRY AGAIN"};
    AssetRef completeSound;
    AssetRef failSound;
    static void describe(TypeBuilder<LevelFlow> &type);

    bool completed() const {
        return state_ == State::Complete;
    }
    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onDestroy(GameContext &context) override;

  private:
    enum class State { Playing, Complete, Failed };
    State state_{State::Playing};
    float timer_{};
    float elapsed_{};
    EventBus::Subscription deathSubscription_{};
};

// Entity builders, shared by the editor's Create menu and the level generator.
EntityId createFireCharacter(Scene &scene, Vec2 at);
EntityId createWaterCharacter(Scene &scene, Vec2 at);
EntityId createFireExit(Scene &scene, Vec2 at);
EntityId createWaterExit(Scene &scene, Vec2 at);
EntityId createLavaPool(Scene &scene, Vec2 at, Vec2 size);
EntityId createWaterPool(Scene &scene, Vec2 at, Vec2 size);
EntityId createGooPool(Scene &scene, Vec2 at, Vec2 size);
EntityId createFireGem(Scene &scene, Vec2 at);
EntityId createWaterGem(Scene &scene, Vec2 at);
EntityId createLevelFlow(Scene &scene);

// Registers LevelFlow and the prototype's entity templates. Requires the gameplay components.
void registerPrototypeGame(ComponentRegistry &registry);
} // namespace yk::prototype
