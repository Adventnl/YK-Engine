#include "yk/animation/AnimationController.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace yk {
namespace {
const char *typeName(ParameterType type) {
    switch (type) {
    case ParameterType::Float:
        return "float";
    case ParameterType::Bool:
        return "bool";
    case ParameterType::Trigger:
        return "trigger";
    }
    return "float";
}
const char *comparisonName(Comparison comparison) {
    switch (comparison) {
    case Comparison::Greater:
        return ">";
    case Comparison::GreaterEqual:
        return ">=";
    case Comparison::Less:
        return "<";
    case Comparison::LessEqual:
        return "<=";
    case Comparison::Equal:
        return "==";
    case Comparison::NotEqual:
        return "!=";
    }
    return ">";
}
std::optional<Comparison> parseComparison(const std::string &text) {
    for (const Comparison candidate :
         {Comparison::Greater, Comparison::GreaterEqual, Comparison::Less, Comparison::LessEqual,
          Comparison::Equal, Comparison::NotEqual})
        if (text == comparisonName(candidate))
            return candidate;
    return std::nullopt;
}
bool holds(Comparison comparison, double value, double reference) {
    constexpr double epsilon = 1e-9;
    switch (comparison) {
    case Comparison::Greater:
        return value > reference;
    case Comparison::GreaterEqual:
        return value >= reference - epsilon;
    case Comparison::Less:
        return value < reference;
    case Comparison::LessEqual:
        return value <= reference + epsilon;
    case Comparison::Equal:
        return std::fabs(value - reference) < epsilon;
    case Comparison::NotEqual:
        return std::fabs(value - reference) >= epsilon;
    }
    return false;
}
} // namespace

const AnimatorParameter *AnimationController::parameter(const std::string &name) const {
    for (const AnimatorParameter &candidate : parameters)
        if (candidate.name == name)
            return &candidate;
    return nullptr;
}
const AnimatorState *AnimationController::state(const std::string &name) const {
    for (const AnimatorState &candidate : states)
        if (candidate.name == name)
            return &candidate;
    return nullptr;
}

Status AnimationController::validate() const {
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (parameters[i].name.empty())
            return Error{"A controller parameter needs a name"};
        for (std::size_t j = 0; j < i; ++j)
            if (parameters[j].name == parameters[i].name)
                return Error{"Parameter '" + parameters[i].name + "' is defined twice"};
    }
    if (states.empty())
        return Error{"A controller needs at least one state"};
    for (std::size_t i = 0; i < states.size(); ++i) {
        const AnimatorState &current = states[i];
        if (current.name.empty() || current.name == "*")
            return Error{"A state needs a name ('*' is reserved for 'any state')"};
        for (std::size_t j = 0; j < i; ++j)
            if (states[j].name == current.name)
                return Error{"State '" + current.name + "' is defined twice"};
        if (current.clip.empty())
            return Error{"State '" + current.name + "' needs a clip"};
        if (!(current.speed >= 0.0F) || !std::isfinite(current.speed))
            return Error{"State '" + current.name + "': speed must be zero or more"};
        if (!current.speedParameter.empty()) {
            const AnimatorParameter *driver = parameter(current.speedParameter);
            if (!driver || driver->type != ParameterType::Float)
                return Error{"State '" + current.name + "': speedParameter '" +
                             current.speedParameter + "' must be a declared float parameter"};
        }
    }
    if (!entry.empty() && !state(entry))
        return Error{"The entry state '" + entry + "' does not exist"};
    for (const AnimatorTransition &transition : transitions) {
        const std::string label = transition.from + " -> " + transition.to;
        if (transition.from != "*" && !state(transition.from))
            return Error{"Transition " + label + ": unknown state '" + transition.from + "'"};
        if (!state(transition.to))
            return Error{"Transition " + label + ": unknown state '" + transition.to + "'"};
        if (transition.hasExitTime && !(transition.exitTime >= 0.0F && transition.exitTime <= 1.0F))
            return Error{"Transition " + label + ": exitTime must be between 0 and 1"};
        if (!transition.hasExitTime && transition.conditions.empty())
            return Error{"Transition " + label +
                         " has no condition and no exit time, so it would fire at once"};
        for (const TransitionCondition &condition : transition.conditions)
            if (!parameter(condition.parameter))
                return Error{"Transition " + label + ": unknown parameter '" + condition.parameter +
                             "'"};
    }
    return success();
}

Status AnimationController::validateAgainst(const AnimationSet &set) const {
    for (const AnimatorState &current : states)
        if (!set.find(current.clip))
            return Error{"State '" + current.name + "' plays clip '" + current.clip +
                         "', which the animation does not define"};
    return success();
}

Json AnimationController::toJson() const {
    Json root = Json::object();
    root.set("format", formatName);
    root.set("version", 1);
    Json parameterList = Json::array();
    for (const AnimatorParameter &item : parameters) {
        Json entryJson = Json::object();
        entryJson.set("name", item.name);
        entryJson.set("type", typeName(item.type));
        if (item.type == ParameterType::Bool && item.initial != 0.0)
            entryJson.set("default", true);
        else if (item.type == ParameterType::Float && item.initial != 0.0)
            entryJson.set("default", item.initial);
        parameterList.push(std::move(entryJson));
    }
    root.set("parameters", parameterList);
    if (!entry.empty())
        root.set("entry", entry);
    Json stateList = Json::array();
    for (const AnimatorState &item : states) {
        Json stateJson = Json::object();
        stateJson.set("name", item.name);
        stateJson.set("clip", item.clip);
        if (item.speed != 1.0F)
            stateJson.set("speed", item.speed);
        if (!item.speedParameter.empty())
            stateJson.set("speedParameter", item.speedParameter);
        stateList.push(std::move(stateJson));
    }
    root.set("states", stateList);
    Json transitionList = Json::array();
    for (const AnimatorTransition &item : transitions) {
        Json transitionJson = Json::object();
        transitionJson.set("from", item.from);
        transitionJson.set("to", item.to);
        Json when = Json::array();
        for (const TransitionCondition &condition : item.conditions) {
            Json conditionJson = Json::object();
            conditionJson.set("parameter", condition.parameter);
            const AnimatorParameter *declared = parameter(condition.parameter);
            const bool flag = declared && declared->type != ParameterType::Float;
            if (flag && condition.comparison == Comparison::Equal && condition.value == 1.0) {
                // "is true" needs neither operator nor value.
            } else if (flag && condition.comparison == Comparison::Equal &&
                       condition.value == 0.0) {
                conditionJson.set("value", false);
            } else {
                conditionJson.set("op", comparisonName(condition.comparison));
                conditionJson.set("value", condition.value);
            }
            when.push(std::move(conditionJson));
        }
        if (when.size() > 0)
            transitionJson.set("when", when);
        if (item.hasExitTime)
            transitionJson.set("exitTime", item.exitTime);
        transitionList.push(std::move(transitionJson));
    }
    root.set("transitions", transitionList);
    return root;
}

Result<AnimationController> AnimationController::fromJson(const Json &json) {
    if (!json.isObject() || json.get("format").asString() != formatName ||
        json.get("version").asInt() != 1)
        return Error{"Not a yk.animator version 1 document"};
    AnimationController controller;
    controller.entry = json.get("entry").asString();
    for (const Json &item : json.get("parameters").items()) {
        AnimatorParameter parameterValue;
        parameterValue.name = item.get("name").asString();
        const std::string type =
            item.get("type").isString() ? item.get("type").asString() : "float";
        if (type == "float")
            parameterValue.type = ParameterType::Float;
        else if (type == "bool")
            parameterValue.type = ParameterType::Bool;
        else if (type == "trigger")
            parameterValue.type = ParameterType::Trigger;
        else
            return Error{"Parameter '" + parameterValue.name + "': unknown type '" + type +
                         "' (use float, bool or trigger)"};
        const Json &initial = item.get("default");
        parameterValue.initial =
            initial.isBool() ? (initial.asBool() ? 1.0 : 0.0) : initial.asNumber(0.0);
        controller.parameters.push_back(std::move(parameterValue));
    }
    for (const Json &item : json.get("states").items()) {
        AnimatorState stateValue;
        stateValue.name = item.get("name").asString();
        stateValue.clip = item.get("clip").asString();
        stateValue.speed = static_cast<float>(item.get("speed").asNumber(1.0));
        stateValue.speedParameter = item.get("speedParameter").asString();
        controller.states.push_back(std::move(stateValue));
    }
    for (const Json &item : json.get("transitions").items()) {
        AnimatorTransition transition;
        transition.from = item.get("from").asString();
        transition.to = item.get("to").asString();
        if (const Json *exit = item.find("exitTime")) {
            if (!exit->isNumber())
                return Error{"Transition " + transition.from + " -> " + transition.to +
                             ": exitTime must be a number"};
            transition.hasExitTime = true;
            transition.exitTime = static_cast<float>(exit->asNumber());
        }
        const Json &when = item.get("when");
        if (item.contains("when") && !when.isArray())
            return Error{"Transition " + transition.from + " -> " + transition.to +
                         ": 'when' must be an array of conditions"};
        for (const Json &conditionJson : when.items()) {
            TransitionCondition condition;
            condition.parameter = conditionJson.get("parameter").asString();
            const Json &value = conditionJson.get("value");
            if (const Json *op = conditionJson.find("op")) {
                const auto parsed = parseComparison(op->asString());
                if (!parsed)
                    return Error{"Transition " + transition.from + " -> " + transition.to +
                                 ": unknown operator '" + op->asString() +
                                 "' (use >, >=, <, <=, == or !=)"};
                condition.comparison = *parsed;
                condition.value = value.isBool() ? (value.asBool() ? 1.0 : 0.0) : value.asNumber();
            } else {
                // No operator: a bool or trigger that is true (or, with "value", equal to it).
                condition.comparison = Comparison::Equal;
                condition.value =
                    value.isBool() ? (value.asBool() ? 1.0 : 0.0) : value.asNumber(1.0);
            }
            transition.conditions.push_back(std::move(condition));
        }
        controller.transitions.push_back(std::move(transition));
    }
    if (auto status = controller.validate(); !status)
        return Error{status.error()};
    return controller;
}

// ----- AnimationPlayer -------------------------------------------------------------------------
Status AnimationPlayer::start(std::shared_ptr<const AnimationSet> set,
                              std::shared_ptr<const AnimationController> controller) {
    set_.reset();
    controller_.reset();
    animator_ = Animator{};
    if (!set || set->clips.empty())
        return Error{"An animation player needs an animation with at least one clip"};
    if (controller) {
        if (auto status = controller->validateAgainst(*set); !status)
            return status;
    }
    for (const AnimationClip &clip : set->clips)
        if (auto status = animator_.define(clip); !status)
            return status;
    set_ = std::move(set);
    controller_ = std::move(controller);
    if (controller_) {
        // Values gameplay already published win over the declared defaults.
        for (const AnimatorParameter &item : controller_->parameters)
            values_.try_emplace(item.name, item.initial);
        enterState(controller_->entry.empty() ? controller_->states.front().name
                                              : controller_->entry);
    } else {
        state_.clear();
        animator_.play(set_->clips.front().name);
    }
    return success();
}

void AnimationPlayer::setFloat(const std::string &name, double value) {
    if (std::isfinite(value))
        values_[name] = value;
}
void AnimationPlayer::setBool(const std::string &name, bool value) {
    values_[name] = value ? 1.0 : 0.0;
}
void AnimationPlayer::trigger(const std::string &name) {
    values_[name] = 1.0;
    triggers_.insert(name);
}
double AnimationPlayer::value(const std::string &name) const {
    const auto found = values_.find(name);
    return found == values_.end() ? 0.0 : found->second;
}

void AnimationPlayer::playClip(const std::string &name) {
    if (set_ && animator_.has(name))
        animator_.play(name);
}

bool AnimationPlayer::conditionHolds(const TransitionCondition &condition) const {
    return holds(condition.comparison, value(condition.parameter), condition.value);
}
bool AnimationPlayer::ready(const AnimatorTransition &transition) const {
    if (transition.hasExitTime && animator_.normalizedTime() < transition.exitTime)
        return false;
    for (const TransitionCondition &condition : transition.conditions)
        if (!conditionHolds(condition))
            return false;
    return true;
}
void AnimationPlayer::enterState(const std::string &name) {
    const AnimatorState *entered = controller_->state(name);
    if (!entered)
        return;
    state_ = name;
    animator_.restart(entered->clip);
}

void AnimationPlayer::followTransitions() {
    // A few hops per update let chains like Any -> Land -> Idle settle at once, but a loop of
    // always-true transitions can never hang a frame.
    for (int hop = 0; hop < 8; ++hop) {
        const AnimatorTransition *chosen = nullptr;
        for (const AnimatorTransition &candidate : controller_->transitions)
            if (candidate.from == "*" && candidate.to != state_ && ready(candidate)) {
                chosen = &candidate;
                break;
            }
        if (!chosen)
            for (const AnimatorTransition &candidate : controller_->transitions)
                if (candidate.from == state_ && ready(candidate)) {
                    chosen = &candidate;
                    break;
                }
        if (!chosen)
            return;
        for (const TransitionCondition &condition : chosen->conditions)
            if (triggers_.erase(condition.parameter) > 0)
                values_[condition.parameter] = 0.0; // Consumed.
        enterState(chosen->to);
    }
}

void AnimationPlayer::update(float seconds) {
    if (!set_)
        return;
    float rate = 1.0F;
    if (controller_)
        if (const AnimatorState *current = controller_->state(state_)) {
            rate = current->speed;
            if (!current->speedParameter.empty())
                rate *= static_cast<float>(std::max(0.0, value(current->speedParameter)));
        }
    animator_.tick(seconds * rate);
    if (controller_)
        followTransitions();
    for (const std::string &name : triggers_)
        values_[name] = 0.0; // Unused triggers do not linger into a later state.
    triggers_.clear();
}

int AnimationPlayer::frame() const {
    return set_ ? animator_.frame() : 0;
}
const std::string &AnimationPlayer::state() const {
    return controller_ ? state_ : animator_.current();
}
const std::string &AnimationPlayer::clip() const {
    return animator_.current();
}
std::vector<ClipEvent> AnimationPlayer::takeEvents() {
    return animator_.takeEvents();
}
} // namespace yk
