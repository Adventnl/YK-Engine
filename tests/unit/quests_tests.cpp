// Quests and objectives on the rule language: the definitions and their files, starting, objectives
// that follow others, conditions, counted events, manual objectives, time limits, failure,
// repeatable and gated quests, rewards, saved state, and the rule predicates and actions.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/items/Inventory.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/sim/Quests.hpp"
#include <algorithm>
#include <string>
#include <vector>

using namespace yk;

namespace {
Json J(const char *text) {
    auto parsed = Json::parse(text);
    CHECK(parsed);
    return parsed ? parsed.value() : Json();
}
bool has(const std::string &text, const char *part) {
    return text.find(part) != std::string::npos;
}

const char *questData = R"({
  "format": "yk.data", "version": 1,
  "stats": [{"id": "health", "max": 100}],
  "items": [{"id": "circuit_board"}, {"id": "energy_module"}, {"id": "coin", "stackSize": 99}],
  "quests": [
    {"id": "repair_autopilot", "title": "Fix the autopilot", "description": "The ship needs it.", "giver": "npc.mechanic",
     "objectives": [
       {"id": "get_board", "text": "Find a circuit board", "complete": {"type": "HasItem", "item": "circuit_board"}},
       {"id": "get_module", "text": "Find an energy module", "complete": {"type": "HasItem", "item": "energy_module"}},
       {"id": "repair", "text": "Repair the console", "after": ["get_board", "get_module"], "manual": true,
        "onComplete": [{"type": "SetVariable", "name": "autopilot_repaired", "value": true}]},
       {"id": "polish", "text": "Polish the panel", "optional": true, "manual": true}],
     "onStart": [{"type": "SetVariable", "name": "quest_started", "value": true}],
     "rewards": [{"type": "GiveItem", "item": "coin", "count": 5}]},
    {"id": "beat_guards", "objectives": [
       {"id": "ko", "text": "Knock out guards", "count": 3,
        "on": {"event": "defeated", "data": {"kind": "guard"}}, "if": {"fact": "actor.entity.tag.player"}}]},
    {"id": "timed", "timeLimit": 2,
     "objectives": [{"id": "wait", "complete": {"var": "never"}}],
     "onFail": [{"type": "SetVariable", "name": "timed_failed", "value": true}]},
    {"id": "fail_cond", "fail": {"var": "alarm"}, "objectives": [{"id": "x", "manual": true}]},
    {"id": "anyof", "complete": "any", "objectives": [{"id": "a", "manual": true}, {"id": "b", "manual": true}]},
    {"id": "custom", "complete": {"all": [{"type": "ObjectiveComplete", "quest": "custom", "objective": "a"}, {"var": "extra"}]},
     "objectives": [{"id": "a", "manual": true}]},
    {"id": "again", "repeatable": true, "objectives": [{"id": "once", "manual": true}]},
    {"id": "gated", "requires": {"type": "QuestState", "quest": "repair_autopilot", "state": "complete"},
     "objectives": [{"id": "x", "manual": true}]},
    {"id": "auto", "autoStart": true, "objectives": [{"id": "x", "complete": {"var": "auto_done"}}]},
    {"id": "chain", "objectives": [
       {"id": "one", "manual": true},
       {"id": "two", "after": ["one"], "complete": {"var": "two_done"}},
       {"id": "three", "after": ["two"], "manual": true}]}
  ]
})";

struct Rig {
    ComponentRegistry registry;
    MemoryAssets assets;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<GameRuntime> runtime;
    EntityId hero, world;
    std::vector<GameEvent> heard;

    Rig(const char *data = questData) {
        registerEngineComponents(registry);
        assets.files["data/quests.ykdata"] = data;
        scene = std::make_unique<Scene>(registry, 8);
    }
    Entity &person(const char *name) {
        Entity &entity = scene->createEntity(name);
        entity.add<StatSet>();
        entity.add<StatusEffects>();
        entity.add<Inventory>().slots = 8;
        entity.add<QuestLog>();
        return entity;
    }
    void start() {
        RuntimeOptions options;
        options.assets = &assets;
        auto created = GameRuntime::create(std::move(scene), options);
        CHECK(created);
        if (!created)
            return;
        runtime = std::move(created.value());
        runtime->events().subscribe("*",
                                    [this](const GameEvent &event) { heard.push_back(event); });
        runtime->stepOnce(Keyboard{});
    }
    void step(int ticks = 1) {
        for (int i = 0; i < ticks; ++i)
            runtime->stepOnce(Keyboard{});
    }
    Entity &entity(EntityId id) {
        return *runtime->scene().find(id);
    }
    QuestLog &log(EntityId id) {
        return *entity(id).get<QuestLog>();
    }
    Inventory &inventory(EntityId id) {
        return *entity(id).get<Inventory>();
    }
    GameContext &game() {
        return *runtime;
    }
    Blackboard &board() {
        return runtime->blackboard();
    }
    int count(const char *name) const {
        return static_cast<int>(
            std::count_if(heard.begin(), heard.end(),
                          [&](const GameEvent &event) { return event.name == name; }));
    }
    const GameEvent *last(const char *name) const {
        for (auto it = heard.rbegin(); it != heard.rend(); ++it)
            if (it->name == name)
                return &*it;
        return nullptr;
    }
};

// ---- Definitions
// ---------------------------------------------------------------------------------
void definitions() {
    CHECK(std::string(questStateName(QuestState::Active)) == "active" &&
          parseQuestState("failed") == QuestState::Failed && !parseQuestState("done") &&
          parseQuestState("inactive") == QuestState::Inactive);
    std::vector<std::string> warnings;
    auto quest = QuestDefinition::fromJson(
        J(R"({"id":"q","title":"A quest","description":"Do it.","giver":"npc.x","category":"favor",
              "objectives":[{"id":"a","text":"Get it","complete":{"type":"HasItem","item":"x"},"optional":true,"hidden":true,
                             "onComplete":[{"type":"Log","message":"got"}]},
                            {"id":"b","after":["a"],"on":{"event":"defeated","source":"tag:guard","data":{"kind":"guard"}},
                             "if":{"var":"ok"},"count":3},
                            {"id":"c","manual":true,"after":["a","b"]}],
              "complete":{"var":"done"},"fail":{"var":"failed"},"requires":{"var":"ready"},"timeLimit":30,
              "onStart":[{"type":"Log","message":"s"}],"rewards":[{"type":"Log","message":"r"}],
              "onFail":[{"type":"Log","message":"f"}],"repeatable":true,"autoStart":true})"),
        warnings);
    CHECK(quest && warnings.empty());
    if (quest) {
        const QuestDefinition &def = quest.value();
        CHECK(def.title == "A quest" && def.objectives.size() == 3 && def.objectives[0].optional &&
              def.objectives[0].hidden && def.objectives[1].on && def.objectives[1].count == 3 &&
              def.objectives[1].on->source == "tag:guard" && def.objectives[2].manual &&
              def.objectives[2].after.size() == 2 && !def.completeWhen.empty() &&
              !def.failWhen.empty() && !def.requirement.empty() && def.timeLimit == 30 &&
              def.repeatable && def.autoStart);
        CHECK(def.objective("b") && !def.objective("z"));
        auto again = QuestDefinition::fromJson(def.toJson(), warnings);
        CHECK(again && again.value().toJson() == def.toJson());
    }
    const auto error = [&](const char *text) {
        auto parsed = QuestDefinition::fromJson(J(text), warnings);
        return parsed ? std::string() : parsed.error();
    };
    CHECK(has(error("[]"), "must be an object"));
    CHECK(has(error(R"({"id":"q"})"), "'objectives' must be a list"));
    CHECK(has(error(R"({"id":"q","objectives":[]})"), "at least one objective"));
    CHECK(has(error(R"({"id":"q","objectives":[{"id":"a"}]})"), "exactly one of 'complete'"));
    CHECK(has(error(R"({"id":"q","objectives":[{"id":"a","manual":true,"complete":{"var":"x"}}]})"),
              "exactly one of 'complete'"));
    CHECK(has(error(R"({"id":"q","objectives":[{"id":"a","complete":true}]})"), "done at once"));
    CHECK(has(error(R"({"id":"q","objectives":[{"id":"a","manual":true,"count":2}]})"),
              "go with 'on'"));
    CHECK(has(error(R"({"id":"q","objectives":[{"id":"a","on":{"event":""}}]})"), "on: "));
    CHECK(has(error(R"({"id":"q","objectives":[{"id":"a","on":"e","count":0}]})"), "'count'"));
    CHECK(has(error(R"({"id":"q","objectives":[{"id":"a b","manual":true}]})"), "not a usable id"));
    CHECK(
        has(error(R"({"id":"q","objectives":[{"id":"a","manual":true},{"id":"a","manual":true}]})"),
            "two objectives are called 'a'"));
    CHECK(has(error(R"({"id":"q","objectives":[{"id":"a","manual":true,"after":["z"]}]})"),
              "comes after 'z'"));
    CHECK(has(error(R"({"id":"q","objectives":[{"id":"a","manual":true,"after":["a"]}]})"),
              "comes after itself"));
    CHECK(has(error(R"({"id":"q","complete":"some","objectives":[{"id":"a","manual":true}]})"),
              "\"all\", \"any\" or a condition"));
    CHECK(has(error(R"({"id":"q","timeLimit":-1,"objectives":[{"id":"a","manual":true}]})"),
              "'timeLimit'"));
    CHECK(has(error(R"({"id":"q","rewards":[{"x":1}],"objectives":[{"id":"a","manual":true}]})"),
              "rewards: "));
    CHECK(has(error(R"({"id":"q","fail":{"all":3},"objectives":[{"id":"a","manual":true}]})"),
              "fail: "));
    warnings.clear();
    CHECK(QuestDefinition::fromJson(
        J(R"({"id":"q","reward":[],"objectives":[{"id":"a","manual":true}]})"), warnings));
    CHECK(warnings.size() == 1 && has(warnings[0], "'reward'"));
}

void files() {
    MemoryAssets assets;
    assets.files["data/quests.ykdata"] = questData;
    assets.files["quests/single.ykquest"] =
        R"({"id":"single","objectives":[{"id":"x","manual":true}]})";
    assets.files["quests/bad.ykquest"] = R"({"quests":[
        {"id":"loop","objectives":[{"id":"a","manual":true,"after":["b"]},{"id":"b","manual":true,"after":["c"]},{"id":"c","manual":true,"after":["a"]}]},
        {"id":"lazy","objectives":[{"id":"a","manual":true,"optional":true}]},
        {"id":"slow","timeLimit":10,"objectives":[{"id":"a","manual":true}]},
        {"id":"ghosts","requires":{"type":"QuestState","quest":"nowhere","state":"complete"},
         "objectives":[{"id":"a","complete":{"type":"HasItem","item":"unicorn"},"onComplete":[{"type":"StartQuest","quest":"nowhere"}]},
                       {"id":"b","on":"defeated","if":{"type":"Nope"}}],
         "rewards":[{"type":"GiveItem","item":"phoenix"}]},
        {"id":"single","objectives":[{"id":"x","manual":true}]}]})";
    std::vector<DataProblem> problems;
    const GameData data = GameData::load(assets, problems);
    CHECK(data.quests.quests.size() == 15 && data.quests.quests.contains("single") &&
          data.quests.quests.contains("repair_autopilot"));
    const auto problem = [&](const char *file, const char *part, bool error = true) {
        return std::any_of(problems.begin(), problems.end(), [&](const DataProblem &item) {
            return item.file == file && has(item.message, part) && item.error == error;
        });
    };
    CHECK(problem("quests/single.ykquest", "two definitions called 'single'") ||
          problem("quests/bad.ykquest", "two definitions called 'single'"));
    ComponentRegistry registry;
    registerEngineComponents(registry);
    problems.clear();
    data.check(registry.extension<RuleCatalog>(), problems);
    CHECK(problem("quests/bad.ykquest", "quest 'loop': objective 'a' waits for itself"));
    CHECK(problem("quests/bad.ykquest", "quest 'lazy': every objective is optional", false));
    CHECK(
        problem("quests/bad.ykquest", "quest 'slow': has a time limit but nothing happens", false));
    CHECK(problem("quests/bad.ykquest",
                  "quest 'ghosts' requires: condition 'QuestState': there is no quest 'nowhere'"));
    CHECK(problem(
        "quests/bad.ykquest",
        "quest 'ghosts' objective 'a' complete: condition 'HasItem': there is no item 'unicorn'"));
    CHECK(problem("quests/bad.ykquest", "quest 'ghosts' objective 'a' onComplete: action "
                                        "'StartQuest': there is no quest 'nowhere'"));
    CHECK(
        problem("quests/bad.ykquest", "quest 'ghosts' objective 'b' if: unknown condition 'Nope'"));
    CHECK(problem("quests/bad.ykquest",
                  "quest 'ghosts' rewards: action 'GiveItem': there is no item 'phoenix'"));
    CHECK(!problem("data/quests.ykdata", "no ") && !problem("data/quests.ykdata", "unknown"));
    CHECK(data.known("quest", "chain") && !data.known("quest", "zz"));
    const auto rows = data.summary();
    CHECK(std::any_of(rows.begin(), rows.end(),
                      [](const auto &row) { return row.first == "Quests" && row.second == "15"; }));
}

// ---- Running quests
// ------------------------------------------------------------------------------
void objectivesAndRewards() {
    Rig rig;
    rig.hero = rig.person("Hero").id();
    rig.start();
    GameContext &game = rig.game();
    QuestLog &log = rig.log(rig.hero);
    Inventory &inventory = rig.inventory(rig.hero);
    CHECK(log.state("repair_autopilot") == QuestState::Inactive &&
          !log.start(game, "no_such_quest") && !rig.board().has("quest_started"));
    CHECK(log.start(game, "repair_autopilot") &&
          !log.start(game, "repair_autopilot")); // Not twice.
    CHECK(log.state("repair_autopilot") == QuestState::Active && rig.board().flag("quest_started"));
    // What is not waiting for something else is active; the rest waits.
    CHECK(log.objectiveState("repair_autopilot", "get_board") == QuestState::Active &&
          log.objectiveState("repair_autopilot", "repair") == QuestState::Inactive &&
          log.objectiveState("repair_autopilot", "polish") ==
              QuestState::Active); // Optional ones run too.
    CHECK((log.active() ==
           std::vector<std::string>{"auto", "repair_autopilot"})); // In the order they began.
    rig.step();
    CHECK(rig.count("quest.started") >= 1 && rig.last("quest.started")->source == rig.hero);

    // A condition that comes to hold completes its objective (within a tenth of a second).
    inventory.addItem(game, "circuit_board");
    rig.step(8);
    CHECK(log.objectiveState("repair_autopilot", "get_board") == QuestState::Complete &&
          log.objectiveState("repair_autopilot", "repair") == QuestState::Inactive);
    rig.step();
    CHECK(rig.count("objective.completed") == 1 &&
          rig.last("objective.completed")->data.get("objective").asString() == "get_board" &&
          rig.last("objective.completed")->data.get("quest").asString() == "repair_autopilot");
    inventory.addItem(game, "energy_module");
    rig.step(8);
    // Both prerequisites are done: the objective that came after them is active, and it is manual.
    CHECK(log.objectiveState("repair_autopilot", "repair") == QuestState::Active &&
          log.state("repair_autopilot") == QuestState::Active);
    CHECK(!rig.board().has("autopilot_repaired"));
    CHECK(log.completeObjective(game, "repair_autopilot", "repair"));
    CHECK(rig.board().flag("autopilot_repaired"));
    // Every required objective is complete: the quest is, and its rewards ran. The optional one did
    // not matter.
    CHECK(log.state("repair_autopilot") == QuestState::Complete &&
          log.objectiveState("repair_autopilot", "polish") == QuestState::Active);
    CHECK(inventory.count(ItemId("coin")) == 5);
    rig.step();
    CHECK(rig.count("quest.completed") == 1 &&
          rig.last("quest.completed")->data.get("quest").asString() == "repair_autopilot");
    // A finished quest cannot be started again, and what is done cannot be done twice.
    CHECK(!log.start(game, "repair_autopilot") &&
          !log.completeObjective(game, "repair_autopilot", "repair") &&
          !log.complete(game, "repair_autopilot"));
    CHECK(!log.completeObjective(game, "repair_autopilot", "nothing") &&
          !log.completeObjective(game, "nothing", "x"));
    CHECK(log.revision() > 0);
}

void countedEvents() {
    Rig rig;
    Entity &hero = rig.person("Hero");
    hero.addTag("player");
    rig.hero = hero.id();
    const EntityId other = rig.scene->createEntity("Bystander").id();
    rig.start();
    GameContext &game = rig.game();
    QuestLog &log = rig.log(rig.hero);
    // Events before the objective is active do not count.
    game.events().emit(GameEvent("defeated", other, rig.hero, J(R"({"kind":"guard"})")));
    rig.step();
    CHECK(log.start(game, "beat_guards") && log.progress("beat_guards", "ko") == 0);
    const auto defeat = [&](const char *kind, EntityId by) {
        game.events().emit(GameEvent("defeated", other, by,
                                     J((std::string("{\"kind\":\"") + kind + "\"}").c_str())));
        rig.step();
    };
    defeat("guard", rig.hero);
    CHECK(log.progress("beat_guards", "ko") == 1);
    defeat("dog", rig.hero); // The wrong kind of thing.
    defeat("guard",
           other); // Done by someone who is not the player: the condition on the event refuses.
    CHECK(log.progress("beat_guards", "ko") == 1);
    rig.heard.clear();
    defeat("guard", rig.hero);
    CHECK(log.progress("beat_guards", "ko") == 2 && log.state("beat_guards") == QuestState::Active);
    rig.step();
    CHECK(rig.count("objective.progress") == 1 &&
          rig.last("objective.progress")->data.get("progress").asNumber() == 2 &&
          rig.last("objective.progress")->data.get("count").asNumber() == 3);
    defeat("guard", rig.hero);
    CHECK(log.progress("beat_guards", "ko") == 3 &&
          log.state("beat_guards") == QuestState::Complete);
    defeat("guard", rig.hero); // Complete: nothing more to count.
    CHECK(log.progress("beat_guards", "ko") == 3);
    // Progress can also be added by hand.
    CHECK(log.reset("beat_guards") && !log.reset("beat_guards") && log.start(game, "beat_guards"));
    CHECK(log.progressObjective(game, "beat_guards", "ko", 2) &&
          log.progress("beat_guards", "ko") == 2);
    CHECK(!log.progressObjective(game, "beat_guards", "ko", 0) &&
          !log.progressObjective(game, "beat_guards", "nothing", 1));
    CHECK(log.progressObjective(game, "beat_guards", "ko", 5) &&
          log.state("beat_guards") == QuestState::Complete);
}

void timeAndFailure() {
    Rig rig;
    rig.hero = rig.person("Hero").id();
    rig.start();
    GameContext &game = rig.game();
    QuestLog &log = rig.log(rig.hero);
    CHECK(log.remaining(game, "timed") < 0.0);
    CHECK(log.start(game, "timed") && log.start(game, "fail_cond"));
    CHECK(log.remaining(game, "timed") > 1.9 && log.remaining(game, "fail_cond") < 0.0);
    rig.step(60);
    CHECK(log.remaining(game, "timed") > 0.5 && log.remaining(game, "timed") < 1.2 &&
          log.state("timed") == QuestState::Active);
    rig.step(70);
    CHECK(log.state("timed") == QuestState::Failed && rig.board().flag("timed_failed") &&
          log.remaining(game, "timed") < 0.0);
    CHECK(log.objectiveState("timed", "wait") == QuestState::Failed);
    rig.step();
    CHECK(rig.count("quest.failed") == 1 &&
          rig.last("quest.failed")->data.get("quest").asString() == "timed");
    // A failure condition; a failed quest stays failed.
    rig.board().setBool("alarm", true);
    rig.step(8);
    CHECK(log.state("fail_cond") == QuestState::Failed && !log.fail(game, "fail_cond") &&
          !log.start(game, "fail_cond"));
    CHECK(!log.complete(game, "fail_cond") && !log.completeObjective(game, "fail_cond", "x"));
}

void completionRules() {
    Rig rig;
    rig.hero = rig.person("Hero").id();
    rig.start();
    GameContext &game = rig.game();
    QuestLog &log = rig.log(rig.hero);
    // Any one objective will do.
    log.start(game, "anyof");
    CHECK(log.completeObjective(game, "anyof", "b") && log.state("anyof") == QuestState::Complete &&
          log.objectiveState("anyof", "a") == QuestState::Active);
    // A condition of its own replaces "all objectives".
    log.start(game, "custom");
    CHECK(log.completeObjective(game, "custom", "a") && log.state("custom") == QuestState::Active);
    rig.board().setBool("extra", true);
    rig.step(8);
    CHECK(log.state("custom") == QuestState::Complete);
    // Repeatable quests can go round again; others cannot.
    log.start(game, "again");
    log.completeObjective(game, "again", "once");
    CHECK(log.state("again") == QuestState::Complete && log.start(game, "again") &&
          log.state("again") == QuestState::Active &&
          log.objectiveState("again", "once") == QuestState::Active);
    // A quest can wait for another one.
    CHECK(!log.start(game, "gated") && log.state("gated") == QuestState::Inactive);
    log.start(game, "repair_autopilot");
    CHECK(log.complete(game, "repair_autopilot") &&
          log.state("repair_autopilot") == QuestState::Complete);
    CHECK(log.objectiveState("repair_autopilot", "get_board") ==
          QuestState::Complete); // Forcing completes what was open.
    CHECK(log.start(game, "gated"));
    // One that starts by itself, and objectives that follow each other down a chain in one look.
    CHECK(log.state("auto") == QuestState::Active);
    rig.board().setBool("auto_done", true);
    rig.step(8);
    CHECK(log.state("auto") == QuestState::Complete);
    log.start(game, "chain");
    CHECK(log.objectiveState("chain", "one") == QuestState::Active &&
          log.objectiveState("chain", "two") == QuestState::Inactive &&
          log.objectiveState("chain", "three") == QuestState::Inactive);
    rig.board().setBool("two_done", true);
    rig.step(8);
    CHECK(log.objectiveState("chain", "two") == QuestState::Inactive); // Waits for "one".
    log.completeObjective(game, "chain", "one");
    rig.step(8);
    CHECK(log.objectiveState("chain", "two") == QuestState::Complete &&
          log.objectiveState("chain", "three") == QuestState::Active);
    log.completeObjective(game, "chain", "three");
    CHECK(log.state("chain") == QuestState::Complete);
}

void savedQuests() {
    Json saved;
    {
        Rig rig;
        rig.hero = rig.person("Hero").id();
        rig.start();
        GameContext &game = rig.game();
        QuestLog &log = rig.log(rig.hero);
        log.start(game, "beat_guards");
        log.progressObjective(game, "beat_guards", "ko", 2);
        log.start(game, "timed");
        rig.step(30);
        log.start(game, "again");
        log.completeObjective(game, "again", "once");
        rig.inventory(rig.hero).addItem(game, "circuit_board");
        log.start(game, "repair_autopilot");
        rig.step(8);
        saved = log.saveState();
    }
    Rig rig;
    rig.hero = rig.person("Hero").id();
    rig.start();
    GameContext &game = rig.game();
    QuestLog &log = rig.log(rig.hero);
    setLogStderrEnabled(false);
    CHECK(log.loadState(game, saved));
    CHECK(log.state("beat_guards") == QuestState::Active && log.progress("beat_guards", "ko") == 2);
    CHECK(log.state("again") == QuestState::Complete &&
          log.objectiveState("repair_autopilot", "get_board") == QuestState::Complete);
    CHECK(log.state("timed") == QuestState::Active && log.remaining(game, "timed") > 0.0);
    CHECK(log.active().size() >= 3);
    // The one that was running counts on from where it was.
    CHECK(log.progressObjective(game, "beat_guards", "ko", 1) &&
          log.state("beat_guards") == QuestState::Complete);
    // Quests that no longer exist are dropped; a state that is not one is refused.
    Json old = saved;
    Json quests = old.get("quests");
    quests.set("ghost", J(R"({"state":"active","elapsed":0,"objectives":{}})"));
    old.set("quests", quests);
    CHECK(log.loadState(game, old) && log.state("ghost") == QuestState::Inactive);
    CHECK(has(log.loadState(game, J(R"({"quests":{"again":{"state":"maybe"}}})")).error(),
              "'maybe' is not valid"));
    CHECK(has(
        log.loadState(
               game,
               J(R"({"quests":{"again":{"state":"active","objectives":{"once":{"state":"??"}}}}})"))
            .error(),
        "objective 'once'"));
}

void questRules() {
    Rig rig;
    rig.hero = rig.person("Hero").id();
    Entity &scenario = rig.scene->createEntity("Scenario");
    scenario.add<QuestLog>();
    rig.world = scenario.id();
    Entity &console = rig.scene->createEntity("Console");
    CHECK(console.add<RuleSet>().setRulesJson(J(R"([
      {"id":"go","when":"go","then":[{"type":"StartQuest","quest":"chain"},
                                     {"type":"StartQuest","quest":"anyof","entity":"name:Scenario"}]},
      {"id":"one","when":"one","then":{"type":"CompleteObjective","quest":"chain","objective":"one"}},
      {"id":"b","when":"b","then":{"type":"ProgressObjective","quest":"beat_guards","objective":"ko","amount":3}},
      {"id":"startb","when":"startb","then":{"type":"StartQuest","quest":"beat_guards"}},
      {"id":"look","when":"look","then":[
         {"type":"SetVariable","name":"chain_state","value":"$actor.quest.chain"},
         {"type":"SetVariable","name":"one_state","value":"$actor.quest.chain.one"},
         {"type":"SetVariable","name":"ko_progress","value":"$actor.quest.beat_guards.progress.ko"}]},
      {"id":"asks","when":"asks","if":{"all":[{"type":"QuestState","quest":"chain","state":"active"},
                                                {"type":"ObjectiveComplete","quest":"chain","objective":"one"}]},
       "then":{"type":"SetVariable","name":"asks","value":true},"else":{"type":"SetVariable","name":"asks","value":false}},
      {"id":"world","when":"world","if":{"type":"QuestState","quest":"anyof","state":"active","entity":"name:Scenario"},
       "then":{"type":"SetVariable","name":"world","value":true},"else":{"type":"SetVariable","name":"world","value":false}},
      {"id":"fail","when":"fail","then":{"type":"FailQuest","quest":"chain"}},
      {"id":"finish","when":"finish","then":{"type":"CompleteQuest","quest":"beat_guards"}},
      {"id":"reset","when":"reset","then":{"type":"ResetQuest","quest":"chain"}}])")));
    const EntityId consoleId = console.id();
    rig.start();
    GameContext &game = rig.game();
    Blackboard &board = rig.board();
    const auto fire = [&](const char *name) {
        game.events().emit(GameEvent(name, consoleId, rig.hero));
        rig.step();
    };
    fire("go");
    CHECK(rig.log(rig.hero).state("chain") == QuestState::Active &&
          rig.log(rig.world).state("anyof") == QuestState::Active &&
          rig.log(rig.hero).state("anyof") == QuestState::Inactive);
    fire("asks");
    CHECK(!board.flag("asks"));
    fire("one");
    fire("asks");
    CHECK(board.flag("asks"));
    fire("world");
    CHECK(board.flag("world"));
    fire("b"); // The quest is not started: nothing counts and the action fails.
    CHECK(rig.log(rig.hero).progress("beat_guards", "ko") == 0);
    fire("startb");
    fire("b");
    CHECK(rig.log(rig.hero).state("beat_guards") == QuestState::Complete);
    fire("look");
    CHECK(board.text("chain_state") == "active" && board.text("one_state") == "complete" &&
          board.integer("ko_progress") == 3);
    fire("fail");
    CHECK(rig.log(rig.hero).state("chain") == QuestState::Failed);
    fire("reset");
    CHECK(rig.log(rig.hero).state("chain") == QuestState::Inactive);
    RuleContext ctx(game);
    ctx.actor = consoleId; // The console has no quest log: the actions fail without trouble.
    ctx.origin = "test";
    CHECK(execute(Action::fromJson(J(R"({"type":"StartQuest","quest":"chain"})")).value(), ctx) ==
          ActionResult::Failed);
    ctx.actor = rig.hero;
    CHECK(execute(Action::fromJson(J(R"({"type":"StartQuest","quest":"nothing"})")).value(), ctx) ==
          ActionResult::Failed);
    CHECK(execute(Action::fromJson(J(R"({"type":"CompleteQuest","quest":"chain"})")).value(),
                  ctx) == ActionResult::Failed);

    // The component names only quests that exist.
    GameData data;
    std::vector<DataProblem> problems;
    data.add(J(questData), "q.ykdata", problems);
    CheckContext context;
    context.known = [&](std::string_view kind, std::string_view id) {
        return data.known(kind, id);
    };
    QuestLog &log = rig.log(rig.hero);
    log.begin = {"chain", "ghost"};
    std::vector<std::string> found;
    log.type().check(rig.entity(rig.hero), log, context, found);
    CHECK(found.size() == 1 && has(found[0], "'ghost', which is not defined"));
}

void beginList() {
    Rig rig;
    Entity &hero = rig.person("Hero");
    hero.get<QuestLog>()->begin = {"chain", "gated", "no_such_quest"};
    rig.hero = hero.id();
    setLogStderrEnabled(false);
    rig.start();
    // Quests it begins with are started; one whose requirement is not met is not.
    CHECK(rig.log(rig.hero).state("chain") == QuestState::Active &&
          rig.log(rig.hero).state("gated") == QuestState::Inactive &&
          rig.log(rig.hero).state("auto") == QuestState::Active);
}
} // namespace

int main() {
    setLogStderrEnabled(false);
    definitions();
    files();
    objectivesAndRewards();
    countedEvents();
    timeAndFailure();
    completionRules();
    savedQuests();
    questRules();
    beginList();
    return yk::test::finish("quests");
}
