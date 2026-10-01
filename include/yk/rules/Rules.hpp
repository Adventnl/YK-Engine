#pragma once
#include "yk/core/Json.hpp"
#include "yk/core/Result.hpp"
#include "yk/core/Value.hpp"
#include "yk/runtime/EventBus.hpp"
#include "yk/scene/EntityId.hpp"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// One language for "if this is true" and "then do this", used everywhere a game needs logic that
// is data: a door's access policy, a quest's objectives, a dialogue choice, an interaction, an AI
// state's transition, a UI binding, the rules of a scenario. There is a single set of conditions
// (all / any / not, comparisons of facts, and predicates systems register: HasItem, StatAtLeast,
// QuestState ...), a single set of actions (GiveItem, SetVariable, OpenDoor, StartDialogue ...), a
// single way to read facts ("actor.stat.health", "var.power", "world.time.hour") and one rule
// shape, WHEN event IF condition THEN actions. Systems add their own predicates, actions and facts
// to a RuleCatalog at registration time; nothing here knows a game.
namespace yk {
class Entity;
class GameContext;
class RuleCatalog;

// What a condition or action is acting on.
//   self    the entity that owns the rule or script
//   actor   who caused it (the one who interacted, entered, was seen)
//   target  what it was done to (the door, the container, the NPC spoken to)
// For an event, `source` is the entity that raised it and `other` the second party; the actor is
// `other` when there is one, else the source.
struct RuleContext {
    GameContext &game;
    EntityId self{};
    EntityId actor{};
    EntityId target{};
    const GameEvent *event{nullptr};
    // Names for what the rule is part of, for log messages ("rule 'repair_console' of 'Console'").
    std::string origin;

    explicit RuleContext(GameContext &context) : game(context) {}
    Entity *selfEntity() const;
    Entity *actorEntity() const;
    Entity *targetEntity() const;
    // "self", "actor", "target", "name:Gate", "id:<hex>", "tag:guard" (all of them), or a bare
    // entity name. Empty when nothing matches.
    std::vector<Entity *> resolve(std::string_view spec) const;
    Entity *resolveOne(std::string_view spec) const;
    // The entities an action's argument names ("entity": "tag:guard"); those of `fallback` when the
    // argument is absent.
    std::vector<Entity *> entitiesFrom(const Json &args, const char *key,
                                       const char *fallback = "self") const;
    // The catalog of the running game's registry, if it has one.
    const RuleCatalog *catalog() const;
    // Reads a fact: "var.name", "actor.stat.health", "world.time.hour", ... none when unknown.
    std::optional<Value> fact(std::string_view path) const;
    // A JSON argument that is either a literal or a "$fact.path" reference.
    Value argument(const Json &json) const;
};

enum class CompareOp { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
const std::vector<std::string> &compareOpNames(); // "==", "!=", "<", "<=", ">", ">="
std::optional<CompareOp> parseCompareOp(std::string_view text);
bool compare(const Value &left, CompareOp op, const Value &right);

// ---- Conditions ---------------------------------------------------------------------------------
struct Condition {
    enum class Kind { Always, Never, All, Any, Not, Compare, Predicate };
    Kind kind{Kind::Always};
    std::vector<Condition> children; // All, Any (and Not: exactly one).
    std::string type;                // Predicate: its registered name.
    Json args;                       // Predicate: the whole JSON object (arguments are its keys).
    std::string fact;                // Compare: the fact on the left.
    CompareOp op{CompareOp::Equal};
    Json value; // Compare: a literal, or "$fact.path" to compare two facts.

    static Result<Condition> fromJson(const Json &json);
    Json toJson() const;
    bool empty() const {
        return kind == Kind::Always;
    }
};
bool evaluate(const Condition &condition, RuleContext &context);

// ---- Actions ------------------------------------------------------------------------------------
// An action is an object with a "type" and its arguments as the other keys. Control actions take
// nested lists of actions ("then", "else", "actions").
struct Action {
    std::string type;
    Json args; // The whole JSON object.
    static Result<Action> fromJson(const Json &json);
    static Result<std::vector<Action>> listFromJson(const Json &json); // One action or an array.
    Json toJson() const;
};
enum class ActionResult { Done, Failed };
// Runs the actions in order; a failing action is logged (with the rule's origin) and the rest
// still run unless `stopOnFailure`. Delays schedule the remainder (see RuleScheduler).
ActionResult execute(const std::vector<Action> &actions, RuleContext &context,
                     bool stopOnFailure = false);
ActionResult execute(const Action &action, RuleContext &context);

// ---- Parameters, for forms and validation -------------------------------------------------------
struct ParamSpec {
    enum class Kind { String, Number, Int, Bool, Entity, Ref, Enum, Condition, Actions, Any, Vec2 };
    std::string name;
    Kind kind{Kind::String};
    bool required{false};
    Json defaultValue;
    std::string description;
    std::string refKind;              // Kind::Ref: "item", "quest", "stat", ...
    std::vector<std::string> options; // Kind::Enum

    static ParamSpec make(const char *name, Kind kind, bool required = false,
                          const char *description = "", const char *refKind = "") {
        ParamSpec spec;
        spec.name = name;
        spec.kind = kind;
        spec.required = required;
        spec.description = description;
        spec.refKind = refKind;
        return spec;
    }
};

// What a validator is told while it walks a condition or action.
class RuleReport {
  public:
    virtual ~RuleReport() = default;
    virtual void error(const std::string &message) = 0;
    virtual void warning(const std::string &message) = 0;
    // True when an id of this kind exists (an item, a quest...), if the validator can tell; the
    // default is "yes" so that a check that cannot know does not complain.
    virtual bool known(std::string_view /*kind*/, std::string_view /*id*/) const {
        return true;
    }
};

struct PredicateType {
    std::string name;
    std::string category{"General"};
    std::string description;
    std::vector<ParamSpec> params;
    std::function<bool(const Json &args, RuleContext &)> evaluate;
    // Extra checks beyond the parameter list (optional).
    std::function<void(const Json &args, RuleReport &)> check;
};
struct ActionType {
    std::string name;
    std::string category{"General"};
    std::string description;
    std::vector<ParamSpec> params;
    std::function<ActionResult(const Json &args, RuleContext &)> execute;
    std::function<void(const Json &args, RuleReport &)> check;
};
// Reads a fact for one namespace ("stat", "inventory", ...). `subject` is the entity the path was
// scoped to ("actor.stat.health" asks the stat provider about the actor); null for global facts.
using FactProvider =
    std::function<std::optional<Value>(RuleContext &, Entity *subject, std::string_view rest)>;

// The things rules are made of. Systems register into one of these; the editor builds its forms
// from the parameter lists; the validator walks conditions and actions against it.
class RuleCatalog {
  public:
    void addPredicate(PredicateType type);
    void addAction(ActionType type);
    // A namespace of facts. `perEntity` namespaces need a subject (the actor unless the path names
    // another scope: "target.stat.health").
    void addFacts(std::string ns, FactProvider provider, bool perEntity);
    const PredicateType *predicate(std::string_view name) const;
    const ActionType *action(std::string_view name) const;
    const std::map<std::string, PredicateType, std::less<>> &predicates() const {
        return predicates_;
    }
    const std::map<std::string, ActionType, std::less<>> &actions() const {
        return actions_;
    }
    std::vector<std::string> factNamespaces() const;
    std::optional<Value> read(RuleContext &context, std::string_view path) const;

    // Checks a condition / action list: known types, required and well-typed parameters, nested
    // conditions and actions, references to things that exist.
    void check(const Condition &condition, RuleReport &report) const;
    void check(const std::vector<Action> &actions, RuleReport &report) const;

  private:
    friend bool evaluate(const Condition &, RuleContext &);
    struct Facts {
        FactProvider provider;
        bool perEntity{};
    };
    std::map<std::string, PredicateType, std::less<>> predicates_;
    std::map<std::string, ActionType, std::less<>> actions_;
    std::map<std::string, Facts, std::less<>> facts_;
};

// Control actions and the always-available predicates and facts (variables, chance, comparisons
// need none). Called by every host's registry setup before the other modules add theirs.
void registerCoreRules(RuleCatalog &catalog);

// ---- Events a rule, an objective or a script waits for ---------------------------------------
// An event name or pattern ("interacted", "crime.*", "*"), who must have raised it, who it must
// have been done to, and the values its payload must carry.
struct EventTrigger {
    std::string pattern;
    std::string source; // "" any, "self", "name:Gate", "tag:guard"...
    std::string other;
    Json dataFilter; // Object of key -> literal.

    // From "crime.*" or {"event": ..., "source": ..., "other": ..., "data": {...}}.
    static Result<EventTrigger> fromJson(const Json &json);
    Json toJson() const;
    // `self` is the entity the trigger belongs to (what "self" names).
    bool matches(GameContext &game, EntityId self, const GameEvent &event) const;
};
// Whether an event name matches a pattern: equal, "*", or the prefix before ".*".
bool eventNameMatches(std::string_view pattern, std::string_view name);

// ---- Rules: WHEN event IF condition THEN actions ------------------------------------------------
struct Rule {
    std::string id;
    bool enabled{true};
    // WHEN: an event name ("interacted", "crime.*", "*") or a timer ("every" / "after" seconds).
    std::string event;
    double every{0.0};
    double after{0.0};
    // Who must have raised it: "" any, "self", or an entity spec; the same for the second party.
    std::string source;
    std::string other;
    Json dataFilter; // Object of key -> literal that the event data must match.
    // IF
    Condition condition;
    // THEN / ELSE
    std::vector<Action> then;
    std::vector<Action> otherwise;
    bool once{false};
    double cooldown{0.0};
    int priority{0};

    static Result<Rule> fromJson(const Json &json);
    Json toJson() const;
};
Result<std::vector<Rule>> rulesFromJson(const Json &json);
Json rulesToJson(const std::vector<Rule> &rules);
void check(const RuleCatalog &catalog, const Rule &rule, RuleReport &report);

// A place in a definition file that holds rules (an effect's "onApply", a quest's "complete when"),
// handed to the validator so it can check them against the catalog and the project's data.
struct RuleSource {
    std::string file;
    std::string label; // For messages: "effect 'poisoned' onApply".
    const Condition *condition{nullptr};
    const std::vector<Action> *actions{nullptr};
};
using RuleSourceVisitor = std::function<void(const RuleSource &)>;

// Runs a delayed continuation: Delay and Repeat actions ask the scheduler (the RuleService) to run
// the rest later with the same context.
class RuleScheduler {
  public:
    virtual ~RuleScheduler() = default;
    virtual void schedule(double seconds, std::vector<Action> actions,
                          const RuleContext &context) = 0;
};
// The scheduler a context should use, set by the service that runs rules; null runs delays at once.
RuleScheduler *schedulerFor(GameContext &game);
} // namespace yk
