// The rules language: conditions, actions, facts, rules and the service that runs them; the
// RuleSet component, validation, JSON round trips, timers, delays and the game's dice.
#include "support/check.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/core/Rng.hpp"
#include "yk/rules/RuleService.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/runtime/Random.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace yk;

namespace {
using Kind = ParamSpec::Kind;

std::vector<std::string> notes;    // What the test action Note recorded, in order.
std::vector<std::string> warnings; // What the rules logged at warning level.

Json J(const char *text) {
    auto parsed = Json::parse(text);
    CHECK(parsed);
    return parsed ? parsed.value() : Json();
}
bool has(const std::string &text, const char *part) {
    return text.find(part) != std::string::npos;
}

ParamSpec param(const char *name, Kind kind, bool required = false) {
    ParamSpec spec;
    spec.name = name;
    spec.kind = kind;
    spec.required = required;
    return spec;
}

// The engine's registry plus a few things only this test needs: a predicate that reads a variable,
// an action that records a line, one that always fails, and two with references and options so the
// validator has something to check against.
ComponentRegistry makeRegistry() {
    ComponentRegistry registry;
    registerEngineComponents(registry);
    RuleCatalog &catalog = registry.extend<RuleCatalog>();
    catalog.addPredicate({"HasCoins",
                          "Test",
                          "True when the coins variable is at least the amount.",
                          {param("amount", Kind::Number, true)},
                          [](const Json &args, RuleContext &context) {
                              return context.game.blackboard().number("coins") >=
                                     toNumber(context.argument(args.get("amount")));
                          },
                          nullptr});
    catalog.addAction({"Note",
                       "Test",
                       "Records a line.",
                       {param("text", Kind::String, true), param("value", Kind::Any)},
                       [](const Json &args, RuleContext &context) {
                           std::string line = args.get("text").asString();
                           if (args.contains("value"))
                               line += "=" + toText(context.argument(args.get("value")));
                           notes.push_back(line);
                           return ActionResult::Done;
                       },
                       nullptr});
    catalog.addAction({"Fail",
                       "Test",
                       "Always fails.",
                       {},
                       [](const Json &, RuleContext &) { return ActionResult::Failed; },
                       nullptr});
    ParamSpec item = param("item", Kind::Ref, true);
    item.refKind = "item";
    ParamSpec mode = param("mode", Kind::Enum, true);
    mode.options = {"quick", "slow"};
    catalog.addAction({"Give",
                       "Test",
                       "Takes an item and a mode.",
                       {item, mode},
                       [](const Json &, RuleContext &) { return ActionResult::Done; },
                       nullptr});
    return registry;
}

struct Rig {
    ComponentRegistry registry = makeRegistry();
    std::unique_ptr<Scene> scene = std::make_unique<Scene>(registry, 7);
    std::unique_ptr<GameRuntime> runtime;
    EntityId hero, gate, console;

    Rig() {
        notes.clear();
        warnings.clear();
        Entity &ember = scene->createEntity("Ember");
        ember.addTag("hero");
        hero = ember.id();
        gate = scene->createEntity("Gate").id();
        console = scene->createEntity("Console").id();
    }
    RuleSet &rules(EntityId id, const char *text) {
        auto &set = scene->find(id)->add<RuleSet>();
        CHECK(set.setRulesJson(J(text)));
        return set;
    }
    void start(std::uint64_t seed = 99) {
        RuntimeOptions options;
        options.randomSeed = seed;
        auto created = GameRuntime::create(std::move(scene), options);
        CHECK(created);
        if (created)
            runtime = std::move(created.value());
    }
    void step(int ticks = 1) {
        for (int i = 0; i < ticks; ++i)
            runtime->stepOnce(Keyboard{});
    }
    Entity &entity(EntityId id) {
        return *runtime->scene().find(id);
    }
    Blackboard &board() {
        return runtime->blackboard();
    }
    // Raises an event and lets the tick deliver it.
    void raise(const char *name, EntityId source = {}, EntityId other = {}, Json data = {}) {
        runtime->events().emit(GameEvent(name, source, other, std::move(data)));
        step();
    }
    RuleContext context() {
        RuleContext ctx(*runtime);
        ctx.actor = hero;
        ctx.self = gate;
        ctx.target = console;
        ctx.origin = "test";
        return ctx;
    }
};

// ---- Conditions
// ----------------------------------------------------------------------------------
void parsingConditions() {
    auto none = Condition::fromJson(Json());
    CHECK(none && none.value().empty());
    auto yes = Condition::fromJson(Json(true));
    CHECK(yes && yes.value().kind == Condition::Kind::Always);
    auto no = Condition::fromJson(Json(false));
    CHECK(no && no.value().kind == Condition::Kind::Never && !no.value().empty());

    auto list = Condition::fromJson(J(R"([{"var":"a"},{"var":"b","op":">=","value":2}])"));
    CHECK(list && list.value().kind == Condition::Kind::All && list.value().children.size() == 2);
    const auto &compare = list.value().children[1];
    CHECK(compare.kind == Condition::Kind::Compare && compare.fact == "var.b" &&
          compare.op == CompareOp::GreaterEqual);
    CHECK(list.value().children[0].op == CompareOp::Equal &&
          list.value().children[0].value == Json(true)); // A bare variable asks "is it true?".

    auto predicate = Condition::fromJson(J(R"({"type":"HasCoins","amount":3})"));
    CHECK(predicate && predicate.value().kind == Condition::Kind::Predicate &&
          predicate.value().type == "HasCoins");

    // Every shape survives a round trip: reading what was written gives what was written.
    for (const char *text :
         {R"({"all":[{"var":"a","op":">=","value":3},{"not":{"type":"HasCoins","amount":2}}]})",
          R"({"any":[{"fact":"actor.entity.name","op":"!=","value":"Ember"},true]})",
          R"({"all":[]})", "false", R"({"not":{"var":"x","op":"<","value":"$var.y"}})"}) {
        auto parsed = Condition::fromJson(J(text));
        CHECK(parsed);
        if (!parsed)
            continue;
        auto again = Condition::fromJson(parsed.value().toJson());
        CHECK(again && again.value().toJson() == parsed.value().toJson());
    }

    const auto error = [](const char *text) {
        auto parsed = Condition::fromJson(J(text));
        return parsed ? std::string() : parsed.error();
    };
    CHECK(has(error("{}"), "needs one of"));
    CHECK(has(error(R"("text")"), "must be an object"));
    CHECK(has(error(R"({"var":"x","op":"~="})"), "unknown comparison '~='"));
    CHECK(has(error(R"({"var":""})"), "name of a fact"));
    CHECK(has(error(R"({"all":3})"), "'all' must be a list"));
    CHECK(has(error(R"({"all":[{"var":"x"},{}]})"), "all[1]"));
    CHECK(has(error(R"({"not":{"type":""}})"), "not: "));
    CHECK(has(error(R"({"type":5})"), "must name a condition"));
}

void evaluatingConditions() {
    Rig rig;
    rig.start();
    RuleContext ctx = rig.context();
    const auto test = [&](const char *text) {
        auto condition = Condition::fromJson(J(text));
        CHECK(condition);
        return condition && evaluate(condition.value(), ctx);
    };
    Blackboard &board = rig.board();
    // A variable nobody set reads as the zero of what it is compared with.
    CHECK(test(R"({"var":"power","op":"==","value":0})"));
    CHECK(test(R"({"var":"lit","op":"==","value":false})"));
    CHECK(test(R"({"var":"label","op":"==","value":""})"));
    CHECK(test(R"({"var":"power","op":"!=","value":1})"));
    CHECK(test(R"({"var":"power","op":"<=","value":0})"));
    CHECK(!test(R"({"var":"power","op":">","value":0})"));
    CHECK(!test(R"({"var":"closed"})"));
    board.set("power", 4.0);
    board.setBool("open", true);
    board.setInt("need", 3);
    CHECK(test(R"({"var":"power","op":">=","value":4})") &&
          !test(R"({"var":"power","op":"<","value":4})"));
    CHECK(test(R"({"var":"open"})"));
    // Either side may be a $fact.
    CHECK(test(R"({"var":"power","op":">","value":"$var.need"})"));
    CHECK(!test(R"({"var":"need","op":">=","value":"$var.power"})"));

    // all / any / not, bare lists, true / false.
    CHECK(test(R"([{"var":"open"},{"var":"power","op":">=","value":4}])"));
    CHECK(!test(R"({"all":[{"var":"open"},{"var":"closed"}]})"));
    CHECK(test(R"({"any":[{"var":"closed"},{"var":"open"}]})"));
    CHECK(test(R"({"all":[]})") && !test(R"({"any":[]})")); // Of nothing: vacuously so.
    CHECK(test(R"({"not":{"var":"closed"}})") && !test(R"({"not":{"var":"open"}})"));
    CHECK(test("true") && !test("false"));

    // Facts about entities: scoped to the actor / self / target, or unscoped (the actor).
    CHECK(test(R"({"fact":"actor.entity.name","value":"Ember"})"));
    CHECK(test(R"({"fact":"self.entity.name","value":"Gate"})"));
    CHECK(test(R"({"fact":"target.entity.name","value":"Console"})"));
    CHECK(test(R"({"fact":"entity.name","value":"Ember"})"));
    CHECK(test(R"({"fact":"actor.entity.tag.hero"})") &&
          !test(R"({"fact":"actor.entity.tag.villain"})"));
    CHECK(test(R"({"fact":"actor.name","value":"Ember"})")); // The entity's own facts, shortened.
    CHECK(test(R"({"fact":"time.tick","op":">=","value":0})"));

    // Predicates a system registered; arguments may be $facts.
    CHECK(!test(R"({"type":"HasCoins","amount":2})"));
    board.set("coins", 5.0);
    CHECK(test(R"({"type":"HasCoins","amount":2})") && !test(R"({"type":"HasCoins","amount":9})"));
    CHECK(test(R"({"type":"HasCoins","amount":"$var.need"})"));
    CHECK(test(R"({"type":"EntityActive","entity":"name:Gate"})") &&
          !test(R"({"type":"EntityActive","entity":"name:Nowhere"})"));
    CHECK(test(R"({"type":"HasTag","tag":"hero"})") &&
          !test(R"({"type":"HasTag","entity":"target","tag":"hero"})"));

    // Something that does not exist counts as false, and says so.
    setLogStderrEnabled(false);
    setLogSink([](LogLevel level, std::string_view, std::string_view message) {
        if (level == LogLevel::Warning)
            warnings.emplace_back(message);
    });
    CHECK(!test(R"({"type":"NoSuchCondition"})"));
    CHECK(warnings.size() == 1 && has(warnings[0], "unknown condition 'NoSuchCondition'") &&
          has(warnings[0], "test"));
    setLogSink({});

    // Dice: 0 never, 1 always, 0.5 about half the time and the same every run of the same seed.
    CHECK(!test(R"({"type":"Chance","probability":0})") &&
          test(R"({"type":"Chance","probability":1})"));
    int heads = 0;
    for (int i = 0; i < 2000; ++i)
        heads += test(R"({"type":"Chance","probability":0.5})") ? 1 : 0;
    CHECK(heads > 900 && heads < 1100);
    Rig other;
    other.start();
    RuleContext otherContext = other.context();
    auto half = Condition::fromJson(J(R"({"type":"Chance","probability":0.5})"));
    Rig again;
    again.start();
    RuleContext againContext = again.context();
    std::string first, second;
    for (int i = 0; i < 64; ++i) {
        first += evaluate(half.value(), otherContext) ? '1' : '0';
        second += evaluate(half.value(), againContext) ? '1' : '0';
    }
    CHECK(first == second);
    Rig different;
    different.start(12345);
    RuleContext differentContext = different.context();
    std::string third;
    for (int i = 0; i < 64; ++i)
        third += evaluate(half.value(), differentContext) ? '1' : '0';
    CHECK(third != first);
}

void eventFacts() {
    Rig rig;
    rig.rules(rig.console, R"([
      {"id":"hear","when":"alarm",
       "then":[{"type":"Note","text":"level","value":"$event.data.level"},
               {"type":"Note","text":"name","value":"$event.name"},
               {"type":"Note","text":"by","value":"$actor.entity.name"},
               {"type":"Note","text":"to","value":"$target.entity.name"}]}])");
    rig.start();
    Json data = Json::object();
    data.set("level", 3);
    rig.raise("alarm", rig.gate, rig.hero, data);
    // The one who did it is the second party when there is one; what it was done to, the first.
    CHECK((notes == std::vector<std::string>{"level=3", "name=alarm", "by=Ember", "to=Gate"}));
    notes.clear();
    rig.raise("alarm", rig.gate); // With no second party the source is the actor.
    CHECK((notes == std::vector<std::string>{"level=", "name=alarm", "by=Gate", "to="}));
}

// ---- Actions
// -------------------------------------------------------------------------------------
void parsingActions() {
    auto one = Action::fromJson(J(R"({"type":"SetVariable","name":"x","value":1})"));
    CHECK(one && one.value().type == "SetVariable");
    auto many = Action::listFromJson(J(R"([{"type":"A"},{"type":"B"}])"));
    CHECK(many && many.value().size() == 2);
    auto single = Action::listFromJson(J(R"({"type":"A"})"));
    CHECK(single && single.value().size() == 1);
    auto empty = Action::listFromJson(Json());
    CHECK(empty && empty.value().empty());
    CHECK(has(Action::fromJson(J("[]")).error(), "must be an object"));
    CHECK(has(Action::fromJson(J("{}")).error(), "needs a 'type'"));
    CHECK(has(Action::listFromJson(J(R"([{"type":"A"},{"name":"b"}])")).error(), "action 2"));
    CHECK(has(Action::listFromJson(J("5")).error(), "action or a list"));
}

void runningActions() {
    Rig rig;
    rig.start();
    RuleContext ctx = rig.context();
    const auto run = [&](const char *text, bool stopOnFailure = false) {
        auto actions = Action::listFromJson(J(text));
        CHECK(actions);
        return actions ? execute(actions.value(), ctx, stopOnFailure) : ActionResult::Failed;
    };
    Blackboard &board = rig.board();
    CHECK(run(R"([{"type":"SetVariable","name":"gems","value":3},
                  {"type":"AddVariable","name":"gems","amount":2},
                  {"type":"AddVariable","name":"gems"}])") == ActionResult::Done);
    CHECK_NEAR(board.number("gems"), 6.0);
    run(R"({"type":"SetVariable","name":"copy","value":"$var.gems"})");
    CHECK_NEAR(board.number("copy"), 6.0);
    run(R"({"type":"SetVariable","name":"who","value":"$actor.entity.name"})");
    CHECK(board.text("who") == "Ember");
    run(R"([{"type":"ToggleVariable","name":"lamp"}])");
    CHECK(board.flag("lamp"));
    run(R"({"type":"ToggleVariable","name":"lamp"})");
    CHECK(!board.flag("lamp"));
    run(R"({"type":"ClearVariable","name":"gems"})");
    CHECK(!board.has("gems") && board.has("copy"));
    run(R"({"type":"SetVariable","name":"lives","value":2,"keep":true})");
    CHECK(board.kept().contains("lives"));

    // If / Sequence.
    run(R"({"type":"If","if":{"var":"copy","op":">","value":5},
            "then":{"type":"Note","text":"big"},"else":{"type":"Note","text":"small"}})");
    run(R"({"type":"If","if":{"var":"copy","op":"<","value":5},
            "then":{"type":"Note","text":"big"},"else":{"type":"Note","text":"small"}})");
    run(R"({"type":"Sequence","actions":[{"type":"Note","text":"a"},{"type":"Note","text":"b"}]})");
    CHECK((notes == std::vector<std::string>{"big", "small", "a", "b"}));
    notes.clear();

    // A failing action does not stop the rest, unless asked; unknown actions fail and say so.
    setLogStderrEnabled(false);
    setLogSink([](LogLevel level, std::string_view, std::string_view message) {
        if (level == LogLevel::Warning)
            warnings.emplace_back(message);
    });
    CHECK(run(R"([{"type":"Note","text":"1"},{"type":"Fail"},{"type":"Note","text":"2"}])") ==
          ActionResult::Failed);
    CHECK((notes == std::vector<std::string>{"1", "2"}));
    notes.clear();
    CHECK(run(R"([{"type":"Note","text":"1"},{"type":"Fail"},{"type":"Note","text":"2"}])", true) ==
          ActionResult::Failed);
    CHECK((notes == std::vector<std::string>{"1"}));
    notes.clear();
    CHECK(run(R"([{"type":"NoSuchAction"},{"type":"Note","text":"after"}])") ==
          ActionResult::Failed);
    CHECK(notes.size() == 1 && warnings.size() == 1 &&
          has(warnings[0], "unknown action 'NoSuchAction'"));
    setLogSink({});
    notes.clear();

    // Events: now, with a payload, and later.
    std::vector<GameEvent> heard;
    rig.runtime->events().subscribe("alarm",
                                    [&](const GameEvent &event) { heard.push_back(event); });
    run(R"({"type":"EmitEvent","name":"alarm","data":{"level":2},"source":"self","other":"actor"})");
    CHECK(heard.empty()); // Raised, delivered at the next safe point.
    rig.runtime->events().dispatch();
    CHECK(heard.size() == 1 && heard[0].source == rig.gate && heard[0].other == rig.hero &&
          heard[0].data.get("level").asNumber() == 2.0);
    heard.clear();
    run(R"({"type":"EmitEvent","name":"alarm","delay":0.5})");
    CHECK(rig.runtime->events().delayed() == 1);
    rig.step(25);
    CHECK(heard.empty());
    rig.step(10);
    CHECK(heard.size() == 1);

    // Entities.
    run(R"({"type":"DisableEntity","entity":"name:Gate"})");
    CHECK(!rig.entity(rig.gate).activeInHierarchy());
    run(R"({"type":"EnableEntity","entity":"name:Gate"})");
    CHECK(rig.entity(rig.gate).activeInHierarchy());
    run(R"({"type":"Teleport","to":[4,2]})"); // The actor by default.
    CHECK_NEAR(rig.entity(rig.hero).worldPosition().x, 4.0);
    CHECK_NEAR(rig.entity(rig.hero).worldPosition().y, 2.0);
    rig.entity(rig.console).setWorldPosition({-3.0F, 1.0F});
    run(R"({"type":"Teleport","entity":"self","toEntity":"target"})");
    CHECK_NEAR(rig.entity(rig.gate).worldPosition().x, -3.0);
    run(R"({"type":"DespawnEntity","entity":"tag:hero"})");
    CHECK(rig.runtime->scene().find(rig.hero) != nullptr); // Removed at the end of the tick.
    rig.step();
    CHECK(rig.runtime->scene().find(rig.hero) == nullptr);
    // A prefab that does not exist fails without taking the game down.
    setLogSink([](LogLevel, std::string_view, std::string_view message) {
        warnings.emplace_back(message);
    });
    CHECK(run(R"({"type":"SpawnEntity","prefab":"prefabs/none.ykprefab","at":[0,0]})") ==
          ActionResult::Failed);
    CHECK(!warnings.empty() && has(warnings.back(), "SpawnEntity"));
    setLogSink({});
}

void delayedActions() {
    Rig rig;
    rig.start();
    RuleContext ctx = rig.context();
    const auto run = [&](const char *text) {
        auto actions = Action::listFromJson(J(text));
        CHECK(actions);
        if (actions)
            execute(actions.value(), ctx);
    };
    run(R"({"type":"Delay","seconds":0.25,
            "then":[{"type":"Note","text":"later","value":"$actor.entity.name"}]})");
    CHECK(notes.empty());
    rig.step(14);
    CHECK(notes.empty());
    rig.step(3);
    CHECK((notes == std::vector<std::string>{"later=Ember"})); // The context travelled with it.
    notes.clear();
    rig.step(30);
    CHECK(notes.empty()); // Once.

    // Repeat: the first run is now, the rest a fixed time apart.
    run(R"({"type":"Repeat","count":3,"every":0.1,"then":{"type":"Note","text":"r"}})");
    CHECK(notes.size() == 1);
    rig.step(8);
    CHECK(notes.size() == 2);
    rig.step(8);
    CHECK(notes.size() == 3);
    rig.step(30);
    CHECK(notes.size() == 3);
    notes.clear();
    run(R"({"type":"Repeat","count":4,"then":{"type":"Note","text":"now"}})");
    CHECK(notes.size() == 4); // Without a gap they all run at once.
    notes.clear();

    // Delays nest, and one delayed list may schedule another.
    run(R"({"type":"Delay","seconds":0.1,"then":[{"type":"Note","text":"one"},
        {"type":"Delay","seconds":0.1,"then":{"type":"Note","text":"two"}}]})");
    rig.step(8);
    CHECK((notes == std::vector<std::string>{"one"}));
    rig.step(8);
    CHECK((notes == std::vector<std::string>{"one", "two"}));
    CHECK(rig.runtime->services().get<RuleService>().counters().delayed >= 4);
}

// ---- Rules
// ---------------------------------------------------------------------------------------
void parsingRules() {
    const auto error = [](const char *text) {
        auto parsed = rulesFromJson(J(text));
        return parsed ? std::string() : parsed.error();
    };
    CHECK(has(error("{}"), "must be a list"));
    CHECK(has(error(R"([{"then":{"type":"A"}}])"), "needs 'when'"));
    CHECK(has(error(R"([{"when":{},"then":{"type":"A"}}])"), "needs an event"));
    CHECK(has(error(R"([{"when":{"every":-1},"then":{"type":"A"}}])"), "positive times"));
    CHECK(has(error(R"([{"when":"x"}])"), "needs 'then'"));
    CHECK(has(error(R"([{"when":"x","if":{"all":3},"then":{"type":"A"}}])"), "if: "));
    CHECK(has(error(R"([{"when":"x","then":[{"x":1}]}])"), "then: "));
    CHECK(has(error(R"([{"when":{"event":"x","data":[1]},"then":{"type":"A"}}])"), "when.data"));
    CHECK(has(
        error(
            R"([{"id":"a","when":"x","then":{"type":"A"}},{"id":"a","when":"y","then":{"type":"A"}}])"),
        "two rules are called 'a'"));
    // The message names the rule that is wrong.
    CHECK(has(error(R"([{"id":"fine","when":"x","then":{"type":"A"}},{"id":"broken","when":"y"}])"),
              "rule 2 ('broken')"));
    CHECK(rulesFromJson(Json()).value().empty());

    // A rule written with every optional key keeps all of them.
    const char *full = R"([{"id":"all_the_keys","enabled":false,
        "when":{"event":"touched","source":"tag:crate","other":"name:Ember","data":{"kind":"fire"}},
        "if":{"all":[{"var":"a","op":">","value":1},{"type":"HasCoins","amount":2}]},
        "then":[{"type":"Note","text":"t"}],"else":[{"type":"Note","text":"e"}],
        "once":true,"cooldown":2.5,"priority":-3},
       {"id":"timer","when":{"every":1.5},"then":[{"type":"Note","text":"tick"}]},
       {"id":"after","when":{"after":4},"then":[{"type":"Note","text":"late"}]}])";
    auto rules = rulesFromJson(J(full));
    CHECK(rules && rules.value().size() == 3);
    if (rules && rules.value().size() == 3) {
        const Rule &rule = rules.value()[0];
        CHECK(rule.id == "all_the_keys" && !rule.enabled && rule.event == "touched" &&
              rule.source == "tag:crate" && rule.other == "name:Ember" &&
              rule.dataFilter.get("kind").asString() == "fire" && rule.once &&
              rule.cooldown == 2.5 && rule.priority == -3 && rule.then.size() == 1 &&
              rule.otherwise.size() == 1);
        CHECK(rules.value()[1].every == 1.5 && rules.value()[2].after == 4.0);
        const Json written = rulesToJson(rules.value());
        CHECK(written == J(full)); // Written the way the editor writes them: lists of actions.
        auto again = rulesFromJson(written);
        CHECK(again && rulesToJson(again.value()) == written);
    }
}

void ruleSets() {
    Rig rig;
    rig.rules(rig.console, R"([
      {"id":"repair","when":{"event":"interacted","source":"self"},
       "if":{"all":[{"type":"HasCoins","amount":2},{"var":"power","op":"==","value":0}]},
       "then":[{"type":"AddVariable","name":"coins","amount":-2},
               {"type":"SetVariable","name":"power","value":1},
               {"type":"Note","text":"repaired","value":"$actor.entity.name"}],
       "else":{"type":"Note","text":"cannot repair"}}])");
    rig.start();
    // The console is interacted with by the hero; nobody else's interaction counts.
    rig.raise("interacted", rig.gate, rig.hero);
    CHECK((notes == std::vector<std::string>{}));
    rig.raise("interacted", rig.console, rig.hero);
    CHECK((notes == std::vector<std::string>{"cannot repair"}));
    CHECK(!rig.board().has("power"));
    notes.clear();
    rig.board().set("coins", 3.0);
    rig.raise("interacted", rig.console, rig.hero);
    CHECK((notes == std::vector<std::string>{"repaired=Ember"}));
    CHECK_NEAR(rig.board().number("coins"), 1.0);
    CHECK_NEAR(rig.board().number("power"), 1.0);
    notes.clear();
    rig.raise("interacted", rig.console, rig.hero); // power is no longer 0.
    CHECK((notes == std::vector<std::string>{"cannot repair"}));
    const auto &service = rig.runtime->services().get<RuleService>();
    CHECK(service.counters().fired == 3 && service.recent().size() == 3 &&
          service.recent().back().rule == "repair" && !service.recent().back().conditionMet &&
          has(service.recent().back().origin, "Console"));
}

void priorityAndSelf() {
    Rig rig;
    rig.rules(rig.console, R"([
      {"id":"low","when":"ping","priority":-1,"then":{"type":"Note","text":"low"}},
      {"id":"first","when":"ping","then":{"type":"Note","text":"first"}},
      {"id":"high","when":"ping","priority":5,"then":{"type":"Note","text":"high"}},
      {"id":"second","when":"ping","then":{"type":"Note","text":"second"}}])");
    rig.rules(
        rig.gate,
        R"([{"id":"me","when":"ping","then":{"type":"Note","text":"gate","value":"$self.entity.name"}}])");
    rig.start();
    rig.raise("ping");
    // Highest priority first, equals in the order written; every set answers for itself.
    CHECK((notes == std::vector<std::string>{"gate=Gate", "high", "first", "second", "low"}));
}

void ruleFilters() {
    Rig rig;
    rig.rules(rig.console, R"([
      {"id":"fire","when":{"event":"hit","data":{"kind":"fire"}},"then":{"type":"Note","text":"fire"}},
      {"id":"hero","when":{"event":"touched","other":"name:Ember"},"then":{"type":"Note","text":"hero"}},
      {"id":"mine","when":{"event":"touched","source":"self"},"then":{"type":"Note","text":"mine"}},
      {"id":"crimes","when":"crime.*","then":{"type":"Note","text":"crime","value":"$event.name"}},
      {"id":"off","enabled":false,"when":"ping","then":{"type":"Note","text":"never"}},
      {"id":"everything","when":"*","if":{"fact":"event.name","value":"zzz"},
       "then":{"type":"Note","text":"zzz"}}])");
    rig.start();
    Json ice = Json::object();
    ice.set("kind", "ice");
    Json fire = Json::object();
    fire.set("kind", "fire");
    rig.raise("hit", {}, {}, ice);
    rig.raise("hit");
    CHECK(notes.empty());
    rig.raise("hit", {}, {}, fire);
    CHECK((notes == std::vector<std::string>{"fire"}));
    notes.clear();
    rig.raise("touched", rig.gate, rig.gate);
    CHECK(notes.empty());
    rig.raise("touched", rig.gate, rig.hero);
    CHECK((notes == std::vector<std::string>{"hero"}));
    notes.clear();
    rig.raise("touched", rig.console, rig.hero);
    CHECK((notes == std::vector<std::string>{"hero", "mine"}));
    notes.clear();
    rig.raise("crime.theft");
    rig.raise("crime.assault.minor");
    rig.raise("crimes");
    rig.raise("crime");
    CHECK((notes == std::vector<std::string>{"crime=crime.theft", "crime=crime.assault.minor"}));
    notes.clear();
    rig.raise("ping");
    CHECK(notes.empty());
    rig.raise("zzz");
    CHECK((notes == std::vector<std::string>{"zzz"}));
}

void onceAndCooldown() {
    Rig rig;
    rig.rules(rig.console, R"([
      {"id":"cool","when":"poke","cooldown":1,"then":{"type":"AddVariable","name":"pokes"}},
      {"id":"gift","when":"bell","once":true,"if":{"var":"ready"},
       "then":{"type":"Note","text":"gift"},"else":{"type":"Note","text":"not yet"}},
      {"id":"cooled_else","when":"knock","cooldown":1,"if":false,
       "then":{"type":"Note","text":"no"}}])");
    rig.start();
    for (int i = 0; i < 3; ++i)
        rig.runtime->events().emit(GameEvent("poke"));
    rig.step();
    CHECK_NEAR(rig.board().number("pokes"), 1.0); // The same tick: the cooldown holds.
    rig.step(30);
    rig.raise("poke");
    CHECK_NEAR(rig.board().number("pokes"), 1.0);
    rig.step(40);
    rig.raise("poke");
    CHECK_NEAR(rig.board().number("pokes"), 2.0);

    // A one-shot is only used up by succeeding; its else keeps answering until then.
    rig.raise("bell");
    rig.raise("bell");
    CHECK((notes == std::vector<std::string>{"not yet", "not yet"}));
    rig.board().setBool("ready", true);
    rig.raise("bell");
    rig.raise("bell");
    CHECK((notes == std::vector<std::string>{"not yet", "not yet", "gift"}));

    // A rule with nothing to do (condition false, no else) does not start its cooldown.
    rig.raise("knock");
    const auto &recent = rig.runtime->services().get<RuleService>().recent();
    CHECK(!recent.empty() && recent.back().rule == "cooled_else" && !recent.back().conditionMet);
    CHECK(rig.runtime->services().get<RuleService>().counters().fired == 5);
}

void timers() {
    Rig rig;
    rig.rules(rig.console, R"([
      {"id":"beat","when":{"every":0.5},"then":{"type":"AddVariable","name":"beats"}},
      {"id":"late","when":{"after":1},"then":{"type":"Note","text":"late"}},
      {"id":"gated","when":{"every":0.5},"if":{"var":"go"},"then":{"type":"AddVariable","name":"gated"}},
      {"id":"once_beat","when":{"every":0.25},"once":true,"then":{"type":"AddVariable","name":"first"}}])");
    rig.start();
    rig.step(29);
    CHECK(!rig.board().has("beats") && notes.empty());
    rig.step(96);                                 // 125 ticks, a little over two seconds.
    CHECK_NEAR(rig.board().number("beats"), 4.0); // At 0.5, 1.0, 1.5 and 2.0: no drift.
    CHECK((notes == std::vector<std::string>{"late"}));
    CHECK(!rig.board().has("gated"));
    CHECK_NEAR(rig.board().number("first"), 1.0);
    rig.board().setBool("go", true);
    rig.step(60);
    CHECK_NEAR(rig.board().number("gated"), 2.0);
    CHECK_NEAR(rig.board().number("beats"), 6.0);
    // A stall does not make a timer fire once per missed beat.
    rig.board().erase("beats");
    for (int i = 0; i < 3; ++i)
        rig.runtime->services().get<RuleService>().onFixedUpdate(*rig.runtime, 5.0F);
    CHECK(rig.board().number("beats") <= 3.0);
}

void chainsAndGuards() {
    Rig rig;
    rig.rules(rig.console, R"([
      {"id":"a","when":"first","then":{"type":"EmitEvent","name":"second"}},
      {"id":"b","when":"second","then":{"type":"Note","text":"chained"}},
      {"id":"loop","when":"spin","then":[{"type":"Note","text":"spin"},
                                          {"type":"EmitEvent","name":"spin"}]}])");
    rig.start();
    rig.raise("first");
    CHECK(
        (notes == std::vector<std::string>{"chained"})); // In the same delivery, not the next tick.
    notes.clear();
    // A rule that raises its own event loops forever; the bus stops it and the game goes on.
    setLogStderrEnabled(false);
    rig.raise("spin");
    CHECK(notes.size() > 100 && notes.size() <= EventBus::dispatchLimit);
    rig.step(3);
    CHECK(rig.runtime->events().pending() == 0);
}

void ownerLifetime() {
    Rig rig;
    rig.rules(rig.console, R"([{"id":"c","when":"ping","then":{"type":"Note","text":"console"}}])");
    rig.rules(rig.gate, R"([{"id":"g","when":"ping","then":{"type":"Note","text":"gate"}}])");
    rig.start();
    rig.raise("ping");
    CHECK(notes.size() == 2);
    notes.clear();
    rig.runtime->destroyLater(rig.gate);
    rig.step();
    rig.raise("ping");
    CHECK((notes == std::vector<std::string>{"console"})); // A destroyed owner's rules are gone.
    // A single rule can be switched off and on again while the game runs.
    notes.clear();
    auto *set = rig.entity(rig.console).get<RuleSet>();
    CHECK(set && set->setRuleEnabled(*rig.runtime, "c", false));
    rig.raise("ping");
    CHECK(notes.empty());
    CHECK(set->setRuleEnabled(*rig.runtime, "c", true));
    rig.raise("ping");
    CHECK(notes.size() == 1);
    CHECK(!set->setRuleEnabled(*rig.runtime, "nope", true)); // No such rule.
    // The same from inside the rules, and the switch is part of the saved state.
    auto ctx = rig.context();
    ctx.self = rig.console;
    CHECK(
        execute(
            Action::fromJson(J(R"({"type":"SetRuleEnabled","rule":"c","enabled":false})")).value(),
            ctx) == ActionResult::Done);
    notes.clear();
    rig.raise("ping");
    CHECK(notes.empty());
    const Json saved = rig.runtime->services().get<RuleService>().saveState();
    CHECK(saved.get("switched").size() == 1 &&
          !saved.get("switched").at(0).get("enabled").asBool(true));
    CHECK(execute(Action::fromJson(J(R"({"type":"SetRuleEnabled","rule":"zzz"})")).value(), ctx) ==
          ActionResult::Failed);
}

void savedState() {
    const char *rules = R"([
      {"id":"once","when":"ring","once":true,"then":{"type":"Note","text":"rang"}},
      {"id":"every","when":"ring","then":{"type":"Note","text":"again"}}])";
    Json state;
    {
        Rig rig;
        rig.rules(rig.console, rules);
        rig.start();
        rig.raise("ring");
        CHECK(notes.size() == 2);
        auto &service = rig.runtime->services().get<RuleService>();
        state = service.saveState();
        CHECK(service.saveKey() == "rules" && state.get("fired").size() == 1);
    }
    notes.clear();
    Rig rig;
    rig.rules(rig.console, rules);
    rig.start();
    rig.step();
    auto &service = rig.runtime->services().get<RuleService>();
    CHECK(service.loadState(*rig.runtime, state));
    rig.raise("ring");
    CHECK((notes == std::vector<std::string>{"again"})); // The one-shot stays used up after a load.
    // The dice are saved with the rest.
    auto &random = rig.runtime->services().get<RandomService>();
    const Json dice = random.saveState();
    const double next = random.rng.uniform();
    CHECK(random.loadState(*rig.runtime, dice));
    CHECK(random.rng.uniform() == next);
    CHECK(!random.loadState(*rig.runtime, J(R"({"a":"x","b":"y"})")));
}

// ---- Validation
// ----------------------------------------------------------------------------------
class Collect final : public RuleReport {
  public:
    std::vector<std::string> errors, warns;
    std::set<std::string> items{"key", "torch"};
    void error(const std::string &message) override {
        errors.push_back(message);
    }
    void warning(const std::string &message) override {
        warns.push_back(message);
    }
    bool known(std::string_view kind, std::string_view id) const override {
        return kind != "item" || items.contains(std::string(id));
    }
    bool mentions(const char *part) const {
        for (const std::string &message : errors)
            if (has(message, part))
                return true;
        return false;
    }
};

std::vector<std::string> problemsOf(const Rig &rig, const char *text, Collect &report) {
    const RuleCatalog *catalog = rig.registry.extension<RuleCatalog>();
    CHECK(catalog != nullptr);
    auto rules = rulesFromJson(J(text));
    CHECK(rules);
    if (catalog && rules)
        for (const Rule &rule : rules.value())
            check(*catalog, rule, report);
    return report.errors;
}

void validation() {
    Rig rig;
    const auto errorsOf = [&](const char *text) {
        Collect report;
        return problemsOf(rig, text, report);
    };
    // A good rule has nothing to say.
    CHECK(errorsOf(R"([{"when":"x","if":{"all":[{"type":"HasCoins","amount":2},
                                                {"var":"p","op":">","value":"$var.q"}]},
                        "then":[{"type":"Give","item":"key","mode":"quick"},
                                {"type":"AddVariable","name":"x","amount":"$var.q"},
                                {"type":"Repeat","count":2,"then":{"type":"Note","text":"hi"}}]}])")
              .empty());
    Collect report;
    problemsOf(rig, R"([{"when":"x","then":{"type":"Explode"}}])", report);
    CHECK(report.mentions("unknown action 'Explode'"));
    Collect conditions;
    problemsOf(rig, R"([{"when":"x","if":{"type":"IsTuesday"},"then":{"type":"Note","text":"a"}}])",
               conditions);
    CHECK(conditions.mentions("unknown condition 'IsTuesday'"));
    Collect missing;
    problemsOf(rig, R"([{"when":"x","then":{"type":"Note"}}])", missing);
    CHECK(missing.mentions("action 'Note' needs 'text'"));
    Collect wrong;
    problemsOf(rig, R"([{"when":"x","then":[{"type":"AddVariable","name":"x","amount":"lots"},
        {"type":"Note","text":5},{"type":"Teleport","to":[1]},
        {"type":"Repeat","count":2.5,"then":{"type":"Note","text":"x"}}]}])",
               wrong);
    CHECK(wrong.mentions("'amount' must be a number") && wrong.mentions("'text' must be text") &&
          wrong.mentions("'to' must be [x, y]") &&
          wrong.mentions("'count' must be a whole number"));
    Collect nested;
    problemsOf(rig, R"([{"when":"x","then":{"type":"If","if":{"type":"Nope"},
        "then":{"type":"Delay","seconds":1,"then":{"type":"AlsoNope"}}}}])",
               nested);
    CHECK(nested.mentions("unknown condition 'Nope'") &&
          nested.mentions("unknown action 'AlsoNope'"));
    Collect facts;
    problemsOf(rig, R"([{"when":"x","if":{"fact":"mystery.level","op":">","value":1},
                         "then":{"type":"Note","text":"a"}},
                        {"when":"y","if":{"fact":"actor.mystery.level"},"then":{"type":"Note","text":"b"}},
                        {"when":"z","if":{"fact":"actor.entity.name","value":"Ember"},
                         "then":{"type":"Note","text":"c"}}])",
               facts);
    CHECK(facts.errors.size() == 2 && facts.mentions("unknown fact 'mystery.level'") &&
          facts.mentions("unknown fact 'actor.mystery.level'"));
    Collect refs;
    problemsOf(rig, R"([{"when":"x","then":[{"type":"Give","item":"sword","mode":"quick"},
                                              {"type":"Give","item":"torch","mode":"fast"}]}])",
               refs);
    CHECK(refs.errors.size() == 2 && refs.mentions("there is no item 'sword'") &&
          refs.mentions("'mode' must be one of quick, slow"));
}

void ruleSetComponent() {
    Rig rig;
    auto &set =
        rig.rules(rig.console, R"([{"id":"a","when":"ping","then":{"type":"Note","text":"hi"}}])");
    CHECK(set.rules().size() == 1 && set.lastError().empty());
    // A bad document is refused and the earlier rules stay.
    CHECK(!set.setRulesJson(J(R"({"when":"x"})")) && !set.lastError().empty() &&
          set.rules().size() == 1);
    CHECK(!set.setRulesJson(J(R"([{"when":"x"}])")) && has(set.lastError(), "rule 1"));
    CHECK(set.setRulesJson(J(R"([{"id":"b","when":"x","then":{"type":"Note","text":"b"}}])")) &&
          set.lastError().empty() && set.rules()[0].id == "b");
    CHECK(set.setRulesJson(Json()) && set.rules().empty() && set.rulesJson().isArray());
    CHECK(set.setRulesJson(J(R"([{"id":"a","when":"ping","then":{"type":"Note","text":"hi"}}])")));

    // It is one property of Json type and goes through the scene file like any other.
    const auto *property = set.type().find("rules");
    CHECK(property && property->type == PropertyType::Json && !property->readOnly);
    const Json document = sceneToJson(*rig.scene);
    auto loaded = sceneFromJson(document, rig.registry);
    CHECK(loaded);
    if (loaded) {
        const auto *console = loaded.value()->findByName("Console");
        CHECK(console && console->get<RuleSet>() &&
              console->get<RuleSet>()->rulesJson() == set.rulesJson());
    }

    // Authoring mistakes are found by the component's own check, as errors when the validator asks.
    auto &bad = rig.scene->find(rig.gate)->add<RuleSet>();
    CHECK(bad.setRulesJson(J(R"([{"when":"x","then":{"type":"Explode"}}])")));
    std::vector<std::string> problems;
    std::vector<std::string> errors;
    CheckContext context;
    context.error = [&](const std::string &message) { errors.push_back(message); };
    bad.type().check(*rig.scene->find(rig.gate), bad, context, problems);
    CHECK(problems.empty() && errors.size() == 1 && has(errors[0], "unknown action 'Explode'"));
    problems.clear();
    bad.type().check(*rig.scene->find(rig.gate), bad, CheckContext{}, problems);
    CHECK(problems.size() == 1); // Without an error channel it is just a sentence in the list.
}

// ---- Dice
// ----------------------------------------------------------------------------------------
void dice() {
    Rng a(42), b(42), c(43);
    bool same = true, differs = false;
    for (int i = 0; i < 100; ++i) {
        const auto x = a.next();
        same = same && x == b.next();
        differs = differs || x != c.next();
    }
    CHECK(same && differs);
    Rng d(7);
    double sum = 0.0;
    int low = 0, high = 0;
    for (int i = 0; i < 20000; ++i) {
        const double u = d.uniform();
        CHECK(u >= 0.0 && u < 1.0);
        sum += u;
        const int r = d.range(3, 6);
        CHECK(r >= 3 && r <= 6);
        low += r == 3;
        high += r == 6;
    }
    CHECK(sum / 20000.0 > 0.48 && sum / 20000.0 < 0.52);
    CHECK(low > 4000 && low < 6000 && high > 4000 && high < 6000);
    CHECK(d.range(5, 5) == 5 && d.range(9, 2) == 9); // An empty range gives the low end.
    // The state is two numbers: save them, keep drawing, put them back and the draws repeat.
    Rng e(1);
    for (int i = 0; i < 10; ++i)
        e.next();
    const auto s0 = e.state(0), s1 = e.state(1);
    const auto ahead = e.next();
    e.setState(s0, s1);
    CHECK(e.next() == ahead);
    e.setState(0, 0); // The one state that would stay at zero forever is not allowed.
    CHECK(e.next() != 0 || e.next() != 0);
}
} // namespace

int main() {
    setLogStderrEnabled(false);
    parsingConditions();
    evaluatingConditions();
    eventFacts();
    parsingActions();
    runningActions();
    delayedActions();
    parsingRules();
    ruleSets();
    priorityAndSelf();
    ruleFilters();
    onceAndCooldown();
    timers();
    chainsAndGuards();
    ownerLifetime();
    savedState();
    validation();
    ruleSetComponent();
    dice();
    return yk::test::finish("rules");
}
