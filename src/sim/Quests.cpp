#include "yk/sim/Quests.hpp"
#include <algorithm>
#include <set>

namespace yk {
const char *questStateName(QuestState state) {
    switch (state) {
    case QuestState::Inactive:
        return "inactive";
    case QuestState::Active:
        return "active";
    case QuestState::Complete:
        return "complete";
    case QuestState::Failed:
        return "failed";
    }
    return "inactive";
}

std::optional<QuestState> parseQuestState(std::string_view name) {
    for (const QuestState state :
         {QuestState::Inactive, QuestState::Active, QuestState::Complete, QuestState::Failed})
        if (name == questStateName(state))
            return state;
    return std::nullopt;
}

const ObjectiveDefinition *QuestDefinition::objective(std::string_view objectiveId) const {
    for (const ObjectiveDefinition &candidate : objectives)
        if (candidate.id == objectiveId)
            return &candidate;
    return nullptr;
}

namespace {
Result<ObjectiveDefinition> objectiveFromJson(const Json &json, std::size_t index) {
    const std::string place = "objective " + std::to_string(index + 1) + ": ";
    if (!json.isObject())
        return Error{place + "must be an object"};
    ObjectiveDefinition objective;
    auto id = data::requiredString(json, "id");
    if (!id)
        return Error{place + id.error()};
    objective.id = id.value();
    if (!data::validId(objective.id))
        return Error{place + "'" + objective.id +
                     "' is not a usable id (letters, digits, '_' and '-')"};
    const std::string where = "objective '" + objective.id + "': ";
    objective.text = data::optionalString(json, "text", objective.id);
    objective.optional = json.get("optional").asBool(false);
    objective.hidden = json.get("hidden").asBool(false);
    objective.after = data::stringList(json, "after");
    const int ways = (json.contains("complete") ? 1 : 0) + (json.contains("on") ? 1 : 0) +
                     (json.get("manual").asBool(false) ? 1 : 0);
    if (ways != 1)
        return Error{where + "say how it is completed with exactly one of 'complete' (a "
                             "condition), 'on' (an event) or \"manual\": true"};
    if (json.contains("complete")) {
        auto condition = Condition::fromJson(json.get("complete"));
        if (!condition)
            return Error{where + "complete: " + condition.error()};
        objective.complete = std::move(condition.value());
        if (objective.complete.empty())
            return Error{where + "'complete' is always true, so it would be done at once"};
    }
    objective.manual = json.get("manual").asBool(false);
    if (json.contains("on")) {
        auto trigger = EventTrigger::fromJson(json.get("on"));
        if (!trigger)
            return Error{where + "on: " + trigger.error()};
        objective.on = std::move(trigger.value());
        auto guard = Condition::fromJson(json.get("if"));
        if (!guard)
            return Error{where + "if: " + guard.error()};
        objective.whenEvent = std::move(guard.value());
        auto count = data::number(json, "count", 1.0, 1.0, 100000.0);
        if (!count)
            return Error{where + count.error()};
        objective.count = static_cast<int>(count.value());
    } else if (json.contains("if") || json.contains("count")) {
        return Error{where + "'if' and 'count' go with 'on' (an event)"};
    }
    auto actions = Action::listFromJson(json.get("onComplete"));
    if (!actions)
        return Error{where + "onComplete: " + actions.error()};
    objective.onComplete = std::move(actions.value());
    return objective;
}
} // namespace

Result<QuestDefinition> QuestDefinition::fromJson(const Json &json,
                                                  std::vector<std::string> &warnings) {
    if (!json.isObject())
        return Error{"a quest must be an object"};
    QuestDefinition quest;
    auto id = data::requiredString(json, "id");
    if (!id)
        return Error{id.error()};
    quest.id = id.value();
    if (!data::validId(quest.id))
        return Error{"'" + quest.id + "' is not a usable id (letters, digits, '_' and '-')"};
    quest.title = data::optionalString(json, "title", quest.id);
    quest.description = data::optionalString(json, "description");
    quest.giver = data::optionalString(json, "giver");
    quest.category = data::optionalString(json, "category");
    quest.repeatable = json.get("repeatable").asBool(false);
    quest.autoStart = json.get("autoStart").asBool(false);
    auto limit = data::number(json, "timeLimit", 0.0, 0.0, 1.0e7);
    if (!limit)
        return Error{limit.error()};
    quest.timeLimit = limit.value();
    const Json &objectives = json.get("objectives");
    if (!objectives.isArray() || objectives.size() == 0)
        return Error{"'objectives' must be a list with at least one objective"};
    std::set<std::string> ids;
    for (std::size_t i = 0; i < objectives.size(); ++i) {
        auto objective = objectiveFromJson(objectives.at(i), i);
        if (!objective)
            return Error{objective.error()};
        if (!ids.insert(objective.value().id).second)
            return Error{"two objectives are called '" + objective.value().id + "'"};
        quest.objectives.push_back(std::move(objective.value()));
    }
    for (const ObjectiveDefinition &objective : quest.objectives)
        for (const std::string &before : objective.after) {
            if (before == objective.id)
                return Error{"objective '" + objective.id + "' comes after itself"};
            if (!ids.contains(before))
                return Error{"objective '" + objective.id + "' comes after '" + before +
                             "', which is not an objective of this quest"};
        }
    const Json &complete = json.get("complete");
    if (complete.isString()) {
        if (complete.asString() == "any")
            quest.anyObjective = true;
        else if (complete.asString() != "all")
            return Error{"'complete' is \"all\", \"any\" or a condition"};
    } else if (!complete.isNull()) {
        auto condition = Condition::fromJson(complete);
        if (!condition)
            return Error{"complete: " + condition.error()};
        quest.completeWhen = std::move(condition.value());
    }
    auto failure = Condition::fromJson(json.get("fail"));
    if (!failure)
        return Error{"fail: " + failure.error()};
    quest.failWhen = std::move(failure.value());
    auto requirement = Condition::fromJson(json.get("requires"));
    if (!requirement)
        return Error{"requires: " + requirement.error()};
    quest.requirement = std::move(requirement.value());
    const auto readActions = [&](const char *key, std::vector<Action> &out) -> Status {
        auto actions = Action::listFromJson(json.get(key));
        if (!actions)
            return Error{std::string(key) + ": " + actions.error()};
        out = std::move(actions.value());
        return success();
    };
    using ActionField = std::pair<const char *, std::vector<Action> *>;
    const ActionField fields[] = {
        {"onStart", &quest.onStart}, {"rewards", &quest.rewards}, {"onFail", &quest.onFail}};
    for (const auto &[key, out] : fields)
        if (auto status = readActions(key, *out); !status)
            return Error{status.error()};
    data::warnUnknown(json,
                      {"id", "title", "description", "giver", "category", "objectives", "complete",
                       "fail", "requires", "timeLimit", "onStart", "rewards", "onFail",
                       "repeatable", "autoStart"},
                      warnings);
    return quest;
}

Json QuestDefinition::toJson() const {
    Json json = Json::object();
    json.set("id", id);
    if (title != id)
        json.set("title", title);
    if (!description.empty())
        json.set("description", description);
    if (!giver.empty())
        json.set("giver", giver);
    if (!category.empty())
        json.set("category", category);
    Json list = Json::array();
    for (const ObjectiveDefinition &objective : objectives) {
        Json item = Json::object();
        item.set("id", objective.id);
        if (objective.text != objective.id)
            item.set("text", objective.text);
        if (objective.optional)
            item.set("optional", true);
        if (objective.hidden)
            item.set("hidden", true);
        if (!objective.after.empty())
            item.set("after", data::toJsonList(objective.after));
        if (objective.manual)
            item.set("manual", true);
        else if (objective.on) {
            item.set("on", objective.on->toJson());
            if (!objective.whenEvent.empty())
                item.set("if", objective.whenEvent.toJson());
            if (objective.count != 1)
                item.set("count", objective.count);
        } else {
            item.set("complete", objective.complete.toJson());
        }
        if (!objective.onComplete.empty()) {
            Json actions = Json::array();
            for (const Action &action : objective.onComplete)
                actions.push(action.toJson());
            item.set("onComplete", actions);
        }
        list.push(item);
    }
    json.set("objectives", list);
    if (!completeWhen.empty())
        json.set("complete", completeWhen.toJson());
    else if (anyObjective)
        json.set("complete", "any");
    if (!failWhen.empty())
        json.set("fail", failWhen.toJson());
    if (!requirement.empty())
        json.set("requires", requirement.toJson());
    if (timeLimit > 0.0)
        json.set("timeLimit", timeLimit);
    const auto writeActions = [&](const char *key, const std::vector<Action> &actions) {
        if (actions.empty())
            return;
        Json array = Json::array();
        for (const Action &action : actions)
            array.push(action.toJson());
        json.set(key, array);
    };
    writeActions("onStart", onStart);
    writeActions("rewards", rewards);
    writeActions("onFail", onFail);
    if (repeatable)
        json.set("repeatable", true);
    if (autoStart)
        json.set("autoStart", true);
    return json;
}

void QuestCatalog::load(const Json &document, const std::string &file,
                        std::vector<DataProblem> &problems) {
    if (document.contains("quests"))
        quests.load(document.get("quests"), file, problems, "quest");
}

void QuestCatalog::check(std::vector<DataProblem> &problems) const {
    for (const QuestDefinition &quest : quests.all()) {
        const std::string where = "quest '" + quest.id + "': ";
        // An objective that waits for itself, through others, never becomes active.
        for (const ObjectiveDefinition &start : quest.objectives) {
            std::set<std::string> seen;
            std::vector<std::string> pending = start.after;
            while (!pending.empty()) {
                const std::string next = pending.back();
                pending.pop_back();
                if (next == start.id) {
                    problems.push_back({quest.file,
                                        where + "objective '" + start.id +
                                            "' waits for itself through other objectives",
                                        true});
                    break;
                }
                if (!seen.insert(next).second)
                    continue;
                if (const ObjectiveDefinition *other = quest.objective(next))
                    pending.insert(pending.end(), other->after.begin(), other->after.end());
            }
        }
        const bool anyRequired =
            std::any_of(quest.objectives.begin(), quest.objectives.end(),
                        [](const ObjectiveDefinition &objective) { return !objective.optional; });
        if (!anyRequired && quest.completeWhen.empty() && !quest.anyObjective)
            problems.push_back(
                {quest.file,
                 where + "every objective is optional, so it is complete as soon as it starts",
                 false});
        if (quest.timeLimit > 0.0 && quest.onFail.empty() && quest.failWhen.empty())
            problems.push_back(
                {quest.file,
                 where +
                     "has a time limit but nothing happens when it runs out except that it fails",
                 false});
    }
}

void QuestCatalog::visitRules(const RuleSourceVisitor &visit) const {
    for (const QuestDefinition &quest : quests.all()) {
        const std::string label = "quest '" + quest.id + "'";
        const auto condition = [&](const char *what, const Condition &value) {
            if (!value.empty())
                visit({quest.file, label + " " + what, &value, nullptr});
        };
        const auto actions = [&](const char *what, const std::vector<Action> &value) {
            if (!value.empty())
                visit({quest.file, label + " " + what, nullptr, &value});
        };
        condition("requires", quest.requirement);
        condition("complete", quest.completeWhen);
        condition("fail", quest.failWhen);
        actions("onStart", quest.onStart);
        actions("rewards", quest.rewards);
        actions("onFail", quest.onFail);
        for (const ObjectiveDefinition &objective : quest.objectives) {
            const std::string where = label + " objective '" + objective.id + "'";
            if (!objective.complete.empty())
                visit({quest.file, where + " complete", &objective.complete, nullptr});
            if (!objective.whenEvent.empty())
                visit({quest.file, where + " if", &objective.whenEvent, nullptr});
            if (!objective.onComplete.empty())
                visit({quest.file, where + " onComplete", nullptr, &objective.onComplete});
        }
    }
}
} // namespace yk
