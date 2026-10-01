#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/sim/Quests.hpp"
#include <algorithm>

namespace yk {
namespace {
constexpr std::uint64_t evaluateEvery = 6; // Ticks between looks at conditions (10 times a second).
} // namespace

void QuestLog::describe(TypeBuilder<QuestLog> &type) {
    type.category("Quests")
        .description("The quests of a character (or of the world: put one on a scenario entity): "
                     "which are active, complete or failed, and how far each objective is. "
                     "Conditions see this entity as the actor.")
        .updatePhase(UpdatePhase::PreUpdate);
    type.field("begin", &QuestLog::begin)
        .ref("quest")
        .tooltip("Quests started when the scene starts.");
    type.check([](const Entity &, const QuestLog &log, const CheckContext &context,
                  std::vector<std::string> &problems) {
        if (!context.known)
            return;
        for (const std::string &id : log.begin)
            if (!context.known("quest", id))
                problems.push_back("starts the quest '" + id + "', which is not defined");
    });
}

const QuestDefinition *QuestLog::definition(GameContext &context, const std::string &quest) const {
    return gameData(context).quests.quests.find(quest);
}

RuleContext QuestLog::rules(GameContext &context, const std::string &origin) const {
    RuleContext rc(context);
    rc.self = entity().id();
    rc.actor = entity().id();
    rc.origin = origin;
    return rc;
}

void QuestLog::announce(GameContext &context, const char *event, const std::string &quest,
                        const std::string &objective) {
    Json data = Json::object();
    data.set("quest", quest);
    if (!objective.empty())
        data.set("objective", objective);
    context.events().emit(GameEvent(event, entity().id(), {}, std::move(data)));
    ++revision_;
}

void QuestLog::onStart(GameContext &context) {
    subscription_ = context.events().subscribe(
        EventBus::anyEvent, [this, &context](const GameEvent &event) { onEvent(context, event); });
    for (const std::string &id : begin)
        if (state(id) == QuestState::Inactive)
            start(context, id);
    for (const QuestDefinition &quest : gameData(context).quests.quests.all())
        if (quest.autoStart && state(quest.id) == QuestState::Inactive)
            start(context, quest.id);
}

void QuestLog::onDestroy(GameContext &context) {
    if (subscription_ != 0)
        context.events().unsubscribe(subscription_);
    subscription_ = 0;
}

void QuestLog::onFixedUpdate(GameContext &context, float) {
    now_ = context.time();
    if (context.tick() - lastEvaluated_ < evaluateEvery)
        return;
    lastEvaluated_ = context.tick();
    reevaluate(context);
}

// ---- Reading
// ---------------------------------------------------------------------------------------
QuestState QuestLog::state(const std::string &quest) const {
    const auto found = entries_.find(quest);
    return found == entries_.end() ? QuestState::Inactive : found->second.state;
}
const QuestLog::Entry *QuestLog::entry(const std::string &quest) const {
    const auto found = entries_.find(quest);
    return found == entries_.end() ? nullptr : &found->second;
}
QuestState QuestLog::objectiveState(const std::string &quest, const std::string &objective) const {
    const Entry *found = entry(quest);
    if (!found)
        return QuestState::Inactive;
    const auto item = found->objectives.find(objective);
    return item == found->objectives.end() ? QuestState::Inactive : item->second.state;
}
int QuestLog::progress(const std::string &quest, const std::string &objective) const {
    const Entry *found = entry(quest);
    if (!found)
        return 0;
    const auto item = found->objectives.find(objective);
    return item == found->objectives.end() ? 0 : item->second.progress;
}
std::vector<std::string> QuestLog::active() const {
    std::vector<std::string> list;
    for (const std::string &id : order_)
        if (state(id) == QuestState::Active)
            list.push_back(id);
    return list;
}
double QuestLog::remaining(GameContext &context, const std::string &quest) const {
    const QuestDefinition *def = definition(context, quest);
    const Entry *found = entry(quest);
    if (!def || !found || found->state != QuestState::Active || def->timeLimit <= 0.0)
        return -1.0;
    return std::max(0.0, def->timeLimit - (context.time() - found->startedAt));
}

// ---- Changing
// -------------------------------------------------------------------------------------
void QuestLog::activateObjectives(GameContext &context, const QuestDefinition &quest,
                                  Entry &entry) {
    (void)context;
    for (const ObjectiveDefinition &objective : quest.objectives) {
        ObjectiveProgress &progress = entry.objectives[objective.id];
        if (progress.state != QuestState::Inactive)
            continue;
        const bool ready = std::all_of(
            objective.after.begin(), objective.after.end(), [&](const std::string &before) {
                return entry.objectives[before].state == QuestState::Complete;
            });
        if (ready) {
            progress.state = QuestState::Active;
            ++revision_;
        }
    }
}

bool QuestLog::start(GameContext &context, const std::string &questId) {
    const QuestDefinition *quest = definition(context, questId);
    if (!quest)
        return false;
    const auto existing = entries_.find(questId);
    if (existing != entries_.end()) {
        if (existing->second.state == QuestState::Active)
            return false;
        if (!quest->repeatable && existing->second.state != QuestState::Inactive)
            return false;
    }
    if (!quest->requirement.empty()) {
        RuleContext rc = rules(context, "quest '" + questId + "' requires");
        if (!evaluate(quest->requirement, rc))
            return false;
    }
    Entry &entry = entries_[questId];
    entry = Entry{};
    now_ = context.time();
    entry.state = QuestState::Active;
    entry.startedAt = now_;
    for (const ObjectiveDefinition &objective : quest->objectives)
        entry.objectives[objective.id] = {};
    if (std::find(order_.begin(), order_.end(), questId) == order_.end())
        order_.push_back(questId);
    activateObjectives(context, *quest, entry);
    announce(context, "quest.started", questId);
    if (!quest->onStart.empty()) {
        RuleContext rc = rules(context, "quest '" + questId + "' onStart");
        execute(quest->onStart, rc);
    }
    reevaluate(context); // Conditions that already hold are met at once.
    return true;
}

void QuestLog::finishObjective(GameContext &context, const QuestDefinition &quest, Entry &entry,
                               const ObjectiveDefinition &objective) {
    ObjectiveProgress &progress = entry.objectives[objective.id];
    progress.state = QuestState::Complete;
    progress.progress = std::max(progress.progress, objective.on ? objective.count : 1);
    announce(context, "objective.completed", quest.id, objective.id);
    if (!objective.onComplete.empty()) {
        RuleContext rc =
            rules(context, "quest '" + quest.id + "' objective '" + objective.id + "'");
        execute(objective.onComplete, rc);
    }
    activateObjectives(context, quest, entry);
}

void QuestLog::checkCompletion(GameContext &context, const QuestDefinition &quest, Entry &entry) {
    if (entry.state != QuestState::Active)
        return;
    bool done = false;
    if (!quest.completeWhen.empty()) {
        RuleContext rc = rules(context, "quest '" + quest.id + "' complete");
        done = evaluate(quest.completeWhen, rc);
    } else if (quest.anyObjective) {
        done = std::any_of(quest.objectives.begin(), quest.objectives.end(),
                           [&](const ObjectiveDefinition &objective) {
                               return entry.objectives[objective.id].state == QuestState::Complete;
                           });
    } else {
        done = std::all_of(quest.objectives.begin(), quest.objectives.end(),
                           [&](const ObjectiveDefinition &objective) {
                               return objective.optional ||
                                      entry.objectives[objective.id].state == QuestState::Complete;
                           });
    }
    if (!done)
        return;
    entry.state = QuestState::Complete;
    announce(context, "quest.completed", quest.id);
    if (!quest.rewards.empty()) {
        RuleContext rc = rules(context, "quest '" + quest.id + "' rewards");
        execute(quest.rewards, rc);
    }
}

void QuestLog::reevaluate(GameContext &context) {
    const std::vector<std::string> running = active();
    for (const std::string &id : running) {
        const QuestDefinition *quest = definition(context, id);
        const auto found = entries_.find(id);
        if (!quest || found == entries_.end() || found->second.state != QuestState::Active)
            continue;
        Entry &entry = found->second;
        // Failure comes first: a quest that ran out of time does not also complete.
        bool failed =
            quest->timeLimit > 0.0 && context.time() - entry.startedAt >= quest->timeLimit;
        if (!failed && !quest->failWhen.empty()) {
            RuleContext rc = rules(context, "quest '" + id + "' fail");
            failed = evaluate(quest->failWhen, rc);
        }
        if (failed) {
            fail(context, id);
            continue;
        }
        // Objectives may finish others by activating them, so go round until nothing changes.
        for (bool progressed = true; progressed;) {
            progressed = false;
            for (const ObjectiveDefinition &objective : quest->objectives) {
                if (entry.objectives[objective.id].state != QuestState::Active ||
                    objective.manual || objective.on)
                    continue;
                RuleContext rc =
                    rules(context, "quest '" + id + "' objective '" + objective.id + "'");
                if (evaluate(objective.complete, rc)) {
                    finishObjective(context, *quest, entry, objective);
                    progressed = true;
                }
            }
        }
        checkCompletion(context, *quest, entry);
    }
}

bool QuestLog::progressObjective(GameContext &context, const std::string &questId,
                                 const std::string &objectiveId, int by) {
    const QuestDefinition *quest = definition(context, questId);
    const auto found = entries_.find(questId);
    const ObjectiveDefinition *objective = quest ? quest->objective(objectiveId) : nullptr;
    if (!objective || found == entries_.end() || found->second.state != QuestState::Active ||
        by <= 0)
        return false;
    Entry &entry = found->second;
    ObjectiveProgress &progress = entry.objectives[objectiveId];
    if (progress.state != QuestState::Active)
        return false;
    progress.progress += by;
    ++revision_;
    Json data = Json::object();
    data.set("quest", questId);
    data.set("objective", objectiveId);
    data.set("progress", progress.progress);
    data.set("count", objective->count);
    context.events().emit(GameEvent("objective.progress", entity().id(), {}, std::move(data)));
    if (progress.progress >= objective->count) {
        finishObjective(context, *quest, entry, *objective);
        checkCompletion(context, *quest, entry);
    }
    return true;
}

bool QuestLog::completeObjective(GameContext &context, const std::string &questId,
                                 const std::string &objectiveId) {
    const QuestDefinition *quest = definition(context, questId);
    const auto found = entries_.find(questId);
    const ObjectiveDefinition *objective = quest ? quest->objective(objectiveId) : nullptr;
    if (!objective || found == entries_.end() || found->second.state != QuestState::Active)
        return false;
    if (found->second.objectives[objectiveId].state != QuestState::Active)
        return false;
    finishObjective(context, *quest, found->second, *objective);
    checkCompletion(context, *quest, found->second);
    return true;
}

bool QuestLog::complete(GameContext &context, const std::string &questId) {
    const QuestDefinition *quest = definition(context, questId);
    const auto found = entries_.find(questId);
    if (!quest || found == entries_.end() || found->second.state != QuestState::Active)
        return false;
    Entry &entry = found->second;
    for (const ObjectiveDefinition &objective : quest->objectives)
        if (entry.objectives[objective.id].state == QuestState::Active)
            finishObjective(context, *quest, entry, objective);
    entry.state = QuestState::Complete;
    announce(context, "quest.completed", questId);
    if (!quest->rewards.empty()) {
        RuleContext rc = rules(context, "quest '" + questId + "' rewards");
        execute(quest->rewards, rc);
    }
    return true;
}

bool QuestLog::fail(GameContext &context, const std::string &questId) {
    const QuestDefinition *quest = definition(context, questId);
    const auto found = entries_.find(questId);
    if (!quest || found == entries_.end() || found->second.state != QuestState::Active)
        return false;
    found->second.state = QuestState::Failed;
    for (auto &[id, progress] : found->second.objectives)
        if (progress.state == QuestState::Active)
            progress.state = QuestState::Failed;
    announce(context, "quest.failed", questId);
    if (!quest->onFail.empty()) {
        RuleContext rc = rules(context, "quest '" + questId + "' onFail");
        execute(quest->onFail, rc);
    }
    return true;
}

bool QuestLog::reset(const std::string &questId) {
    const bool had = entries_.erase(questId) > 0;
    order_.erase(std::remove(order_.begin(), order_.end(), questId), order_.end());
    if (had)
        ++revision_;
    return had;
}

void QuestLog::onEvent(GameContext &context, const GameEvent &event) {
    if (event.name.starts_with("quest.") || event.name.starts_with("objective."))
        return; // Quests do not wait on their own announcements (a rule can, through variables).
    const std::vector<std::string> running = active();
    for (const std::string &id : running) {
        const QuestDefinition *quest = definition(context, id);
        if (!quest)
            continue;
        for (const ObjectiveDefinition &objective : quest->objectives) {
            if (!objective.on || objectiveState(id, objective.id) != QuestState::Active)
                continue;
            if (!objective.on->matches(context, entity().id(), event))
                continue;
            if (!objective.whenEvent.empty()) {
                RuleContext rc(context);
                rc.self = entity().id();
                rc.actor = event.other ? event.other : event.source;
                rc.target = event.other ? event.source : EntityId{};
                rc.event = &event;
                rc.origin = "quest '" + id + "' objective '" + objective.id + "'";
                if (!evaluate(objective.whenEvent, rc))
                    continue;
            }
            progressObjective(context, id, objective.id, 1);
            if (state(id) != QuestState::Active)
                break;
        }
    }
}

// ---- Saving
// -----------------------------------------------------------------------------------------
Json QuestLog::saveState() const {
    Json state = Json::object();
    Json quests = Json::object();
    for (const auto &[id, entry] : entries_) {
        Json item = Json::object();
        item.set("state", questStateName(entry.state));
        item.set("elapsed", std::max(0.0, now_ - entry.startedAt));
        Json objectives = Json::object();
        for (const auto &[objective, progress] : entry.objectives) {
            Json line = Json::object();
            line.set("state", questStateName(progress.state));
            line.set("progress", progress.progress);
            objectives.set(objective, line);
        }
        item.set("objectives", objectives);
        quests.set(id, item);
    }
    state.set("quests", quests);
    state.set("order", data::toJsonList(order_));
    return state;
}

Status QuestLog::loadState(GameContext &context, const Json &state) {
    entries_.clear();
    order_.clear();
    const Json &quests = state.get("quests");
    for (std::size_t i = 0; i < quests.size(); ++i) {
        const std::string &id = quests.keyAt(i);
        if (!definition(context, id)) {
            log(LogLevel::Warning, "quests",
                "a saved quest '" + id + "' no longer exists and was dropped");
            continue;
        }
        const Json &item = quests.valueAt(i);
        const auto questState = parseQuestState(item.get("state").asString());
        if (!questState)
            return Error{"quest '" + id + "': the saved state '" + item.get("state").asString() +
                         "' is not valid"};
        Entry entry;
        entry.state = *questState;
        now_ = context.time();
        entry.startedAt = now_ - item.get("elapsed").asNumber(0.0);
        const Json &objectives = item.get("objectives");
        for (std::size_t k = 0; k < objectives.size(); ++k) {
            const auto objectiveState =
                parseQuestState(objectives.valueAt(k).get("state").asString());
            if (!objectiveState)
                return Error{"quest '" + id + "' objective '" + objectives.keyAt(k) +
                             "': the saved state is not valid"};
            entry.objectives[objectives.keyAt(k)] = {
                *objectiveState, static_cast<int>(objectives.valueAt(k).get("progress").asInt(0))};
        }
        entries_[id] = std::move(entry);
    }
    for (const std::string &id : data::stringList(state, "order"))
        if (entries_.contains(id))
            order_.push_back(id);
    for (const auto &[id, entry] : entries_)
        if (std::find(order_.begin(), order_.end(), id) == order_.end())
            order_.push_back(id);
    ++revision_;
    return success();
}

// ---- Rules
// ------------------------------------------------------------------------------------------
namespace {
using Kind = ParamSpec::Kind;
ParamSpec param(const char *name, Kind kind, bool required = false, const char *description = "",
                const char *refKind = "") {
    return ParamSpec::make(name, kind, required, description, refKind);
}

QuestLog *logOf(const Json &args, RuleContext &context) {
    for (const char *spec : {"actor", "self"}) {
        const auto found = context.entitiesFrom(args, "entity", spec);
        if (!found.empty() && found.front()->get<QuestLog>())
            return found.front()->get<QuestLog>();
        if (args.contains("entity"))
            break; // Named explicitly: no fallback.
    }
    return nullptr;
}

std::optional<Value> questFact(RuleContext &, Entity *subject, std::string_view rest) {
    const auto *log = subject ? subject->get<QuestLog>() : nullptr;
    if (!log)
        return std::nullopt;
    const std::string quest(rest.substr(0, rest.find('.')));
    const std::string_view tail =
        quest.size() < rest.size() ? rest.substr(quest.size() + 1) : std::string_view{};
    if (tail.empty())
        return Value{std::string(questStateName(log->state(quest)))};
    if (tail.starts_with("progress."))
        return Value{static_cast<std::int64_t>(log->progress(quest, std::string(tail.substr(9))))};
    return Value{std::string(questStateName(log->objectiveState(quest, std::string(tail))))};
}
} // namespace

void registerQuestRules(RuleCatalog &catalog) {
    catalog.addFacts("quest", questFact, true);
    ParamSpec stateParam = param("state", Kind::Enum, true);
    stateParam.options = {"inactive", "active", "complete", "failed"};
    catalog.addPredicate({"QuestState",
                          "Quests",
                          "True when the quest is in the state for the character's quest log.",
                          {param("quest", Kind::Ref, true, "", "quest"), stateParam,
                           param("entity", Kind::Entity, false, "default the actor")},
                          [](const Json &args, RuleContext &context) {
                              const QuestLog *log = logOf(args, context);
                              const auto wanted = parseQuestState(args.get("state").asString());
                              return log && wanted &&
                                     log->state(args.get("quest").asString()) == *wanted;
                          },
                          nullptr});
    catalog.addPredicate(
        {"ObjectiveComplete",
         "Quests",
         "True when the objective of the quest is complete.",
         {param("quest", Kind::Ref, true, "", "quest"), param("objective", Kind::String, true),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             const QuestLog *log = logOf(args, context);
             return log &&
                    log->objectiveState(args.get("quest").asString(),
                                        args.get("objective").asString()) == QuestState::Complete;
         },
         nullptr});
    const auto questAction = [&](const char *name, const char *description,
                                 bool (QuestLog::*act)(GameContext &, const std::string &)) {
        catalog.addAction({name,
                           "Quests",
                           description,
                           {param("quest", Kind::Ref, true, "", "quest"),
                            param("entity", Kind::Entity, false, "default the actor, else self")},
                           [act](const Json &args, RuleContext &context) {
                               QuestLog *log = logOf(args, context);
                               return log && (log->*act)(context.game, args.get("quest").asString())
                                          ? ActionResult::Done
                                          : ActionResult::Failed;
                           },
                           nullptr});
    };
    questAction("StartQuest", "Starts a quest in the character's quest log.", &QuestLog::start);
    questAction("CompleteQuest", "Completes an active quest (its rewards run).",
                &QuestLog::complete);
    questAction("FailQuest", "Fails an active quest.", &QuestLog::fail);
    catalog.addAction({"ResetQuest",
                       "Quests",
                       "Puts a quest back to inactive (it can be started again).",
                       {param("quest", Kind::Ref, true, "", "quest"),
                        param("entity", Kind::Entity, false, "default the actor, else self")},
                       [](const Json &args, RuleContext &context) {
                           QuestLog *log = logOf(args, context);
                           return log && log->reset(args.get("quest").asString())
                                      ? ActionResult::Done
                                      : ActionResult::Failed;
                       },
                       nullptr});
    catalog.addAction(
        {"CompleteObjective",
         "Quests",
         "Completes an active objective of a quest (a manual objective is only completed this "
         "way).",
         {param("quest", Kind::Ref, true, "", "quest"), param("objective", Kind::String, true),
          param("entity", Kind::Entity, false, "default the actor, else self")},
         [](const Json &args, RuleContext &context) {
             QuestLog *log = logOf(args, context);
             return log && log->completeObjective(context.game, args.get("quest").asString(),
                                                  args.get("objective").asString())
                        ? ActionResult::Done
                        : ActionResult::Failed;
         },
         nullptr});
    catalog.addAction(
        {"ProgressObjective",
         "Quests",
         "Adds to the count of an event-driven objective.",
         {param("quest", Kind::Ref, true, "", "quest"), param("objective", Kind::String, true),
          param("amount", Kind::Int, false, "default 1"),
          param("entity", Kind::Entity, false, "default the actor, else self")},
         [](const Json &args, RuleContext &context) {
             QuestLog *log = logOf(args, context);
             return log && log->progressObjective(
                               context.game, args.get("quest").asString(),
                               args.get("objective").asString(),
                               static_cast<int>(toInt(context.argument(args.get("amount")), 1)))
                        ? ActionResult::Done
                        : ActionResult::Failed;
         },
         nullptr});
}

void registerQuestComponents(ComponentRegistry &registry) {
    registerQuestRules(registry.extend<RuleCatalog>());
    registry.add<QuestLog>("QuestLog");
}
} // namespace yk
