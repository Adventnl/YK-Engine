#include "yk/gameplay/Gameplay.hpp"
#include <cmath>

namespace yk {
void LevelFlow::describe(TypeBuilder<LevelFlow> &type) {
    type.category("Gameplay")
        .description("Level rules: complete when every goal is satisfied; optionally restart when "
                     "anyone dies or a restart action is pressed.");
    type.field("goals", &LevelFlow::goals)
        .tooltip("Goal entities that must all be satisfied at once.");
    type.field("restartOnDeath", &LevelFlow::restartOnDeath)
        .tooltip("Restart the whole level when anyone dies.");
    type.field("restartDelay", &LevelFlow::restartDelay).range(0, 30, 0.1);
    type.field("completeDelay", &LevelFlow::completeDelay).range(0, 30, 0.1);
    type.field("nextScene", &LevelFlow::nextScene)
        .tooltip("Project-relative scene to load after completion.");
    type.field("restartSet", &LevelFlow::restartSet).inputSet();
    type.field("restartAction", &LevelFlow::restartAction).inputAction();
    type.field("completeMessage", &LevelFlow::completeMessage);
    type.field("failMessage", &LevelFlow::failMessage);
    type.field("completeSound", &LevelFlow::completeSound).asset("sound");
    type.field("failSound", &LevelFlow::failSound).asset("sound");
    type.field("state", &LevelFlow::state_).readOnly().options({"Playing", "Complete", "Failed"});
}

void LevelFlow::onStart(GameContext &context) {
    state_ = State::Playing;
    elapsed_ = 0.0F;
    context.blackboard().set("level_state", std::string("playing"));
    context.blackboard().set("level_message", std::string());
    context.blackboard().set("level_time", 0.0);
    deathSubscription_ =
        context.events().subscribe("entity_died", [this, &context](const GameEvent &) {
            if (!restartOnDeath || state_ != State::Playing)
                return;
            state_ = State::Failed;
            timer_ = restartDelay;
            context.blackboard().set("level_state", std::string("failed"));
            context.blackboard().set("level_message", failMessage);
            if (!failSound.path.empty())
                context.audio().play(failSound.path);
        });
}

void LevelFlow::onFixedUpdate(GameContext &context, float seconds) {
    if (context.input().state(restartSet, restartAction).pressed) {
        context.requestRestart();
        return;
    }
    if (state_ == State::Playing) {
        elapsed_ += seconds;
        context.blackboard().set("level_time", std::floor(static_cast<double>(elapsed_)));
        bool all = !goals.empty();
        for (const EntityRef reference : goals) {
            const Entity *goalEntity = context.scene().find(reference);
            const auto *goal = goalEntity ? goalEntity->get<Goal>() : nullptr;
            all = all && goal && goal->satisfied();
        }
        if (all) {
            state_ = State::Complete;
            timer_ = completeDelay;
            context.blackboard().set("level_state", std::string("complete"));
            context.blackboard().set("level_message", completeMessage);
            if (!completeSound.path.empty())
                context.audio().play(completeSound.path);
            context.emit("level_completed", entity().id());
        }
        return;
    }
    timer_ -= seconds;
    if (timer_ > 0.0F)
        return;
    if (state_ == State::Failed) {
        context.requestRestart();
    } else if (!nextScene.empty()) {
        context.requestSceneChange(nextScene);
    }
}

void LevelFlow::onDestroy(GameContext &context) {
    context.events().unsubscribe(deathSubscription_);
}
} // namespace yk
