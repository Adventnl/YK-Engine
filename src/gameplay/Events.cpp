#include "yk/gameplay/Gameplay.hpp"
#include <algorithm>

namespace yk {
const std::vector<std::string> &signalChangeNames() {
    static const std::vector<std::string> names{"None", "Set", "Clear", "Toggle"};
    return names;
}
const std::vector<std::string> &variableChangeNames() {
    static const std::vector<std::string> names{"None", "Add", "Set"};
    return names;
}

void EventAction::describe(TypeBuilder<EventAction> &type) {
    type.category("Gameplay")
        .description("When an event happens (after a delay), does things: changes a signal that "
                     "doors and platforms follow, raises another event, switches entities on or "
                     "off, triggers animations, changes a game variable, plays a sound, restarts "
                     "or changes scene. With no event and an interval it is a repeating timer.")
        .allowMultiple()
        .check([](const Entity &, const EventAction &action, const CheckContext &,
                  std::vector<std::string> &problems) {
            if (action.onEvent.empty() && action.every <= 0.0F)
                problems.push_back("waits for no event and has no interval, so it never runs");
            if (action.signal != SignalChange::None && action.targets.empty())
                problems.push_back("changes a signal but has no targets to follow it");
            if (!action.animationTrigger.empty() && action.animate.empty())
                problems.push_back("sets an animation trigger but names no entities to animate");
            if (action.variableChange != VariableChange::None && action.variable.empty())
                problems.push_back("changes a variable but names none");
            const bool acts =
                action.signal != SignalChange::None || !action.raiseEvent.empty() ||
                !action.activate.empty() || !action.deactivate.empty() ||
                !action.animationTrigger.empty() || action.variableChange != VariableChange::None ||
                !action.sound.path.empty() || action.restartLevel || !action.changeScene.empty();
            if (!acts)
                problems.push_back("does nothing when it runs: give it something to do");
        });
    type.field("onEvent", &EventAction::onEvent)
        .tooltip("The event that starts it: plate_pressed, lever_toggled, collected, goal_reached, "
                 "level_completed, scene_started, or any event another component raises. Empty "
                 "with an interval: a repeating timer.");
    type.field("from", &EventAction::from)
        .tooltip("Only events raised by this entity count. Empty: any.");
    type.field("delay", &EventAction::delay)
        .range(0, 3600, 0.05)
        .tooltip("Seconds between the event and the actions.");
    type.field("every", &EventAction::every)
        .range(0, 3600, 0.1)
        .tooltip("With no onEvent: runs the actions every this many seconds.");
    type.field("once", &EventAction::once).tooltip("Stop after running once.");
    type.field("signal", &EventAction::signal)
        .options(signalChangeNames())
        .tooltip("Set, clear or toggle the signal this component holds; the targets follow it.");
    type.field("targets", &EventAction::targets)
        .tooltip("Doors, platforms... that follow the signal this component holds.");
    type.field("raiseEvent", &EventAction::raiseEvent)
        .tooltip("Another event to raise (so actions can be chained with delays).");
    type.field("activate", &EventAction::activate).tooltip("Entities to switch on.");
    type.field("deactivate", &EventAction::deactivate).tooltip("Entities to switch off.");
    type.field("animate", &EventAction::animate)
        .tooltip("Entities whose animation gets the trigger below.");
    type.field("animationTrigger", &EventAction::animationTrigger)
        .tooltip("A trigger parameter of the entities' animation controllers.");
    type.field("variableChange", &EventAction::variableChange)
        .options(variableChangeNames())
        .tooltip("Add to a game variable, or set it.");
    type.field("variable", &EventAction::variable);
    type.field("amount", &EventAction::amount).range(-100000, 100000, 0.5);
    type.field("sound", &EventAction::sound).asset("sound");
    type.field("restartLevel", &EventAction::restartLevel);
    type.field("changeScene", &EventAction::changeScene)
        .tooltip("Project-relative scene to go to.");
    type.field("signalOn", &EventAction::signal_).readOnly();
}

void EventAction::onStart(GameContext &context) {
    pending_.clear();
    signal_ = false;
    finished_ = false;
    timer_ = 0.0F;
    runs_ = 0;
    if (onEvent.empty())
        return;
    subscription_ = context.events().subscribe(onEvent, [this, &context](const GameEvent &event) {
        if (finished_ || (from && event.source != from))
            return;
        pending_.push_back(
            {context.time() + static_cast<double>(std::max(delay, 0.0F)), event.other});
    });
}

void EventAction::onDestroy(GameContext &context) {
    context.events().unsubscribe(subscription_);
}

void EventAction::onFixedUpdate(GameContext &context, float seconds) {
    if (!targets.empty())
        sendSignal(context.scene(), entity().id(), targets, signal_);
    if (finished_)
        return;
    if (onEvent.empty() && every > 0.0F) {
        timer_ += seconds;
        if (timer_ >= every) {
            timer_ -= every;
            run(context, {});
        }
        return;
    }
    // Run what has come due (in the order it was asked for); running may raise events, and those
    // are delivered later, so nothing is added to the list meanwhile.
    for (std::size_t i = 0; i < pending_.size();) {
        if (context.time() + 1e-9 < pending_[i].due) {
            ++i;
            continue;
        }
        const EntityId other = pending_[i].other;
        pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
        run(context, other);
        if (finished_)
            return;
    }
}

void EventAction::run(GameContext &context, EntityId other) {
    ++runs_;
    if (once) {
        finished_ = true;
        pending_.clear();
    }
    switch (signal) {
    case SignalChange::Set:
        signal_ = true;
        break;
    case SignalChange::Clear:
        signal_ = false;
        break;
    case SignalChange::Toggle:
        signal_ = !signal_;
        break;
    case SignalChange::None:
        break;
    }
    if (!targets.empty())
        sendSignal(context.scene(), entity().id(), targets, signal_);
    if (!raiseEvent.empty())
        context.emit(raiseEvent, entity().id(), other);
    for (const EntityRef reference : activate)
        if (Entity *target = context.scene().find(reference))
            target->setActive(true);
    for (const EntityRef reference : deactivate)
        if (Entity *target = context.scene().find(reference))
            target->setActive(false);
    if (!animationTrigger.empty())
        for (const EntityRef reference : animate)
            if (Entity *target = context.scene().find(reference))
                if (auto *animated = target->get<AnimatedSprite>())
                    animated->trigger(animationTrigger);
    if (variableChange == VariableChange::Add && !variable.empty())
        context.blackboard().add(variable, static_cast<double>(amount));
    else if (variableChange == VariableChange::Set && !variable.empty())
        context.blackboard().set(variable, static_cast<double>(amount));
    if (!sound.path.empty())
        context.audio().play(sound.path);
    if (restartLevel)
        context.requestRestart();
    if (!changeScene.empty())
        context.requestSceneChange(changeScene);
}
} // namespace yk
