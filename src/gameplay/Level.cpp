#include "yk/gameplay/Gameplay.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
void LevelFlow::describe(TypeBuilder<LevelFlow> &type) {
    type.category("Gameplay")
        .description("Level rules: an optional intro, complete when every goal is satisfied, fail "
                     "on a death or when time runs out, restart, and on to the next scene.");
    type.field("goals", &LevelFlow::goals)
        .tooltip("Goal entities that must all be satisfied at once.");
    type.field("introDuration", &LevelFlow::introDuration)
        .range(0, 30, 0.1)
        .tooltip("Seconds the level shows introMessage at the start, ignoring input. 0: none.");
    type.field("introMessage", &LevelFlow::introMessage);
    type.field("restartOnDeath", &LevelFlow::restartOnDeath)
        .tooltip("Fail the level when anyone dies: it starts over after restartDelay.");
    type.field("timeLimit", &LevelFlow::timeLimit)
        .range(0, 3600, 1)
        .tooltip("Seconds to complete the level in; the level fails when they run out. 0: no "
                 "limit.");
    type.field("lockInputOnComplete", &LevelFlow::lockInputOnComplete)
        .tooltip("Ignore the players' input once the level is complete or has failed.");
    type.field("restartDelay", &LevelFlow::restartDelay).range(0, 30, 0.1);
    type.field("completeDelay", &LevelFlow::completeDelay)
        .range(0, 30, 0.1)
        .tooltip("Seconds between completing the level and moving on to the next scene.");
    type.field("nextScene", &LevelFlow::nextScene)
        .asset("scene")
        .tooltip("The scene to load after completion (none: stay on this one).");
    type.field("restartSet", &LevelFlow::restartSet).inputSet();
    type.field("restartAction", &LevelFlow::restartAction).inputAction();
    type.field("continueAction", &LevelFlow::continueAction)
        .inputAction()
        .tooltip("An action (of restartSet) that skips the wait on the complete or failed screen.");
    type.field("completeMessage", &LevelFlow::completeMessage);
    type.field("failMessage", &LevelFlow::failMessage);
    type.field("timeUpMessage", &LevelFlow::timeUpMessage);
    type.field("keepVariables", &LevelFlow::keepVariables)
        .tooltip("Game variables (a score, gems collected) carried into the next scene.");
    type.field("completeSound", &LevelFlow::completeSound).asset("sound");
    type.field("failSound", &LevelFlow::failSound).asset("sound");
    type.field("state", &LevelFlow::state_)
        .readOnly()
        .options({"Intro", "Playing", "Complete", "Failed"});
}

void LevelFlow::publish(GameContext &context, const char *state, const std::string &message) {
    context.blackboard().set("level_state", std::string(state));
    context.blackboard().set("level_message", message);
}

void LevelFlow::onStart(GameContext &context) {
    elapsed_ = 0.0F;
    context.blackboard().set("level_time", 0.0);
    if (timeLimit > 0.0F)
        context.blackboard().set("level_time_left", std::ceil(static_cast<double>(timeLimit)));
    for (const std::string &name : keepVariables)
        context.blackboard().keep(name);
    if (introDuration > 0.0F) {
        state_ = State::Intro;
        timer_ = introDuration;
        context.lockInput("level_intro", true);
        publish(context, "intro", introMessage);
    } else {
        state_ = State::Playing;
        publish(context, "playing", {});
        context.emit("level_started", entity().id());
    }
    deathSubscription_ =
        context.events().subscribe("entity_died", [this, &context](const GameEvent &) {
            if (restartOnDeath && state_ == State::Playing)
                fail(context, failMessage);
        });
}

void LevelFlow::fail(GameContext &context, const std::string &message) {
    state_ = State::Failed;
    timer_ = restartDelay;
    publish(context, "failed", message);
    if (!failSound.path.empty())
        context.audio().play(failSound.path);
    if (lockInputOnComplete)
        context.lockInput("level_end", true);
    context.emit("level_failed", entity().id());
}

void LevelFlow::onFixedUpdate(GameContext &context, float seconds) {
    if (context.input().state(restartSet, restartAction).pressed) {
        context.requestRestart();
        return;
    }
    switch (state_) {
    case State::Intro:
        timer_ -= seconds;
        if (timer_ <= 0.0F) {
            context.lockInput("level_intro", false);
            state_ = State::Playing;
            publish(context, "playing", {});
            context.emit("level_started", entity().id());
        }
        return;
    case State::Playing: {
        elapsed_ += seconds;
        context.blackboard().set("level_time", std::floor(static_cast<double>(elapsed_)));
        if (timeLimit > 0.0F) {
            const float left = std::max(0.0F, timeLimit - elapsed_);
            context.blackboard().set("level_time_left", std::ceil(static_cast<double>(left)));
            if (left <= 0.0F) {
                fail(context, timeUpMessage);
                return;
            }
        }
        bool all = !goals.empty();
        for (const EntityRef reference : goals) {
            const Entity *goalEntity = context.scene().find(reference);
            const auto *goal = goalEntity ? goalEntity->get<Goal>() : nullptr;
            all = all && goal && goal->satisfied();
        }
        if (!all)
            return;
        state_ = State::Complete;
        timer_ = completeDelay;
        publish(context, "complete", completeMessage);
        if (!completeSound.path.empty())
            context.audio().play(completeSound.path);
        if (lockInputOnComplete)
            context.lockInput("level_end", true);
        for (const EntityRef reference : goals) // Whoever is in an exit walks into it.
            if (Entity *goalEntity = context.scene().find(reference))
                if (auto *goal = goalEntity->get<Goal>())
                    goal->beginExit(context);
        context.emit("level_completed", entity().id());
        return;
    }
    case State::Complete:
    case State::Failed: {
        timer_ -= seconds;
        const bool skip =
            !continueAction.empty() && context.input().state(restartSet, continueAction).pressed;
        if (timer_ > 0.0F && !skip)
            return;
        timer_ = 1e9F; // Ask once; the runtime does the rest (and fades while it does it).
        if (state_ == State::Failed)
            context.requestRestart();
        else if (!nextScene.path.empty())
            context.requestSceneChange(nextScene.path);
        return;
    }
    }
}

void LevelFlow::onDestroy(GameContext &context) {
    context.events().unsubscribe(deathSubscription_);
}
} // namespace yk
