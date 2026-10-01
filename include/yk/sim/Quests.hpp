#pragma once
#include "yk/data/Table.hpp"
#include "yk/rules/Rules.hpp"
#include "yk/scene/Registry.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

// Quests (favors, missions, objectives) are data on the rule language: an objective is complete
// when a condition holds, when an event has happened enough times, or when a rule says so; a quest
// is complete when its objectives are; what happens at the start, at the end and on failure is a
// list of actions. A character's QuestLog keeps which quests are where.
namespace yk {
class GameContext;

enum class QuestState { Inactive, Active, Complete, Failed };
const char *questStateName(QuestState state);
std::optional<QuestState> parseQuestState(std::string_view name);

struct ObjectiveDefinition {
    std::string id;
    std::string text;
    bool optional{false};           // Not needed for the quest to be complete.
    bool hidden{false};             // Not shown until it is active.
    std::vector<std::string> after; // It becomes active when these are complete.
    // How it is completed (exactly one):
    Condition complete;             // a condition that comes to hold,
    std::optional<EventTrigger> on; // an event (with an optional condition, `count` times),
    bool manual{false};             // or an action (CompleteObjective).
    Condition whenEvent; // For `on`: must hold when the event arrives ("if" in the file).
    int count{1};        // For `on`: how many times.
    std::vector<Action> onComplete;
};

struct QuestDefinition {
    std::string id;
    std::string file;
    std::string title;
    std::string description;
    std::string giver; // Informational: who hands it out (a persistent id or a name).
    std::string category;
    std::vector<ObjectiveDefinition> objectives;
    bool anyObjective{false}; // Complete when any objective is (default: all required ones).
    Condition completeWhen;   // Overrides the above when set ("complete" in the file).
    Condition failWhen;
    double timeLimit{0.0}; // Seconds from the start; 0 none.
    std::vector<Action> onStart;
    std::vector<Action> rewards; // Run when the quest completes.
    std::vector<Action> onFail;
    Condition requirement; // Must hold for the quest to be started ("requires" in the file).
    bool repeatable{false};
    bool autoStart{false}; // Started for every QuestLog when the scene starts.

    const ObjectiveDefinition *objective(std::string_view id) const;
    static Result<QuestDefinition> fromJson(const Json &json, std::vector<std::string> &warnings);
    Json toJson() const;
};
using QuestTable = DefinitionTable<QuestDefinition>;

struct QuestCatalog {
    QuestTable quests;
    void load(const Json &document, const std::string &file, std::vector<DataProblem> &problems);
    void check(std::vector<DataProblem> &problems) const;
    void visitRules(const RuleSourceVisitor &visit) const;
};

// The quests of one character (or of the world: put one on a scenario entity). It is where quests
// are started, progressed, completed and failed, and it raises
//   quest.started, quest.completed, quest.failed   (source: the owner; data: quest)
//   objective.completed                            (data: quest, objective)
// with the log's entity as the source. Conditions see the log's entity as the actor.
class QuestLog final : public Component {
  public:
    std::vector<std::string> begin; // Quests started when the scene starts.
    static void describe(TypeBuilder<QuestLog> &type);

    struct ObjectiveProgress {
        QuestState state{QuestState::Inactive};
        int progress{0};
    };
    struct Entry {
        QuestState state{QuestState::Inactive};
        double startedAt{0.0};
        std::map<std::string, ObjectiveProgress> objectives;
    };

    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onDestroy(GameContext &context) override;
    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;

    QuestState state(const std::string &quest) const;
    QuestState objectiveState(const std::string &quest, const std::string &objective) const;
    int progress(const std::string &quest, const std::string &objective) const;
    // Quests that are active, in the order they were started.
    std::vector<std::string> active() const;
    const Entry *entry(const std::string &quest) const;
    // Seconds left on a quest with a time limit (negative: no limit or not active).
    double remaining(GameContext &context, const std::string &quest) const;
    std::uint64_t revision() const {
        return revision_;
    }

    // Starts the quest (a completed or failed one only when it is repeatable, and only when its
    // `requires` holds). False when it cannot be.
    bool start(GameContext &context, const std::string &quest);
    // Completes an objective by hand (a manual objective, or forcing any active one).
    bool completeObjective(GameContext &context, const std::string &quest,
                           const std::string &objective);
    // Adds progress to an event-driven objective (what a counted event does).
    bool progressObjective(GameContext &context, const std::string &quest,
                           const std::string &objective, int by = 1);
    bool complete(GameContext &context, const std::string &quest); // Forces the quest complete.
    bool fail(GameContext &context, const std::string &quest);
    bool reset(const std::string &quest); // Back to inactive.

  private:
    const QuestDefinition *definition(GameContext &context, const std::string &quest) const;
    RuleContext rules(GameContext &context, const std::string &origin) const;
    void reevaluate(GameContext &context); // Looks at every active quest's conditions.
    void activateObjectives(GameContext &context, const QuestDefinition &quest, Entry &entry);
    void finishObjective(GameContext &context, const QuestDefinition &quest, Entry &entry,
                         ObjectiveDefinition const &objective);
    void checkCompletion(GameContext &context, const QuestDefinition &quest, Entry &entry);
    void onEvent(GameContext &context, const GameEvent &event);
    void announce(GameContext &context, const char *event, const std::string &quest,
                  const std::string &objective = {});

    std::map<std::string, Entry> entries_;
    std::vector<std::string> order_;
    EventBus::Subscription subscription_{};
    std::uint64_t revision_{0};
    std::uint64_t lastEvaluated_{0};
    double now_{0.0}; // Game time at the last tick, for saving how long a quest has been running.
};

void registerQuestRules(RuleCatalog &catalog);
void registerQuestComponents(ComponentRegistry &registry);
} // namespace yk
