// Conversations as graphs: parsing both file forms, walking a graph (variables in text, portraits
// and expressions, choices with conditions and "once", branches, nodes that only run actions), the
// Dialogue component playing a graph with real input, and the rule actions around it.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/sim/Dialogue.hpp"
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

const char *graphText = R"({
  "start": "greet",
  "speakers": {
    "mara": {"name": "Keeper Mara", "side": "left",
             "portraits": {"default": "assets/mara.png", "smile": "assets/mara_smile.png"}},
    "dog": {"name": "Rex", "side": "right"}
  },
  "nodes": {
    "greet": {"speaker": "mara", "text": "Hello, {player_name}.",
              "actions": [{"type": "AddVariable", "name": "greetings"}],
              "choices": [
                {"text": "I need a favor.", "if": {"var": "trusted"}, "next": "favor"},
                {"text": "Who are you?", "once": true, "next": "who"},
                {"text": "Goodbye ({greetings})."}]},
    "who": {"speaker": "mara", "expression": "smile", "text": "I keep the keys.", "next": "greet"},
    "favor": {"speaker": "mara", "text": "Take this.",
              "actions": [{"type": "SetVariable", "name": "favor_given", "value": true}],
              "branch": [{"if": {"var": "owes"}, "next": "debt"}, {"next": "farewell"}]},
    "debt": {"speaker": "dog", "text": "Woof.", "portrait": "assets/rex.png", "expression": "x"},
    "farewell": {"speaker": "mara", "text": "Stay safe.", "side": "right"},
    "logic": {"actions": [{"type": "AddVariable", "name": "passes"}],
              "branch": [{"if": {"var": "x"}, "next": "farewell"}, {"next": "farewell"}]},
    "silent": {"actions": [{"type": "SetVariable", "name": "silent_ran", "value": true}]}
  }
})";

struct Rig {
    ComponentRegistry registry;
    MemoryAssets assets;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<GameRuntime> runtime;
    EntityId hero, mara;
    Keyboard keyboard;
    std::vector<GameEvent> heard;

    Rig(const char *graph = graphText, float speed = 0.0F) {
        registerEngineComponents(registry);
        assets.files["dialogue/mara.ykdialogue"] = graph;
        scene = std::make_unique<Scene>(registry, 2);
        hero = scene->createEntity("Hero").id();
        Entity &keeper = scene->createEntity("Mara");
        auto &dialogue = keeper.add<Dialogue>();
        dialogue.sequence.path = "dialogue/mara.ykdialogue";
        dialogue.speaker = "Somebody";
        dialogue.charactersPerSecond = speed;
        dialogue.upAction = "MoveLeft"; // Not S: the standard map also binds it to Interact.
        dialogue.downAction = "MoveRight";
        mara = keeper.id();
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
        runtime->blackboard().set("player_name", std::string("Ember"));
        step();
    }
    void step(int ticks = 1) {
        for (int i = 0; i < ticks; ++i) {
            InputFrame frame;
            frame.keyboard = keyboard;
            runtime->stepOnce(frame);
            keyboard.beginFrame();
        }
    }
    void press(Key key) {
        keyboard.set(key, true);
        step();
        keyboard.set(key, false);
        step();
    }
    Dialogue &dialogue() {
        return *runtime->scene().find(mara)->get<Dialogue>();
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

// ---- The file
// --------------------------------------------------------------------------------------
void parsing() {
    std::vector<std::string> warnings;
    // The old linear form is a chain of nodes.
    auto linear = DialogueGraph::fromJson(
        J(R"({"pages":[{"speaker":"Keeper","text":"One.","portrait":"p.png"},"Two.",{"text":"Three."}]})"),
        warnings);
    CHECK(linear && linear.value().nodes.size() == 3 && linear.value().start == "page1" &&
          linear.value().node("page1")->next == "page2" &&
          linear.value().node("page3")->next.empty() &&
          linear.value().node("page1")->speaker == "Keeper" &&
          linear.value().node("page1")->portrait == "p.png" &&
          linear.value().node("page2")->text == "Two.");
    CHECK(has(DialogueGraph::fromJson(J(R"({"pages":[]})"), warnings).error(),
              "nonempty pages array"));
    CHECK(has(DialogueGraph::fromJson(J(R"({"pages":[{"speaker":"x"}]})"), warnings).error(),
              "page 1 needs text"));

    auto graph = DialogueGraph::fromJson(J(graphText), warnings);
    CHECK(graph);
    if (!graph)
        return;
    const DialogueGraph &g = graph.value();
    CHECK(g.start == "greet" && g.nodes.size() == 7 && g.speakers.size() == 2 &&
          g.node("greet")->choices.size() == 3);
    CHECK(g.node("greet")->choices[1].once && !g.node("greet")->choices[0].condition.empty() &&
          g.node("greet")->choices[2].next.empty());
    CHECK(g.node("favor")->branches.size() == 2 && g.node("favor")->branches[1].condition.empty());
    CHECK(g.speaker("mara")->portraits.at("smile") == "assets/mara_smile.png" &&
          g.speaker("dog")->side == "right" && !g.speaker("nobody"));
    CHECK(g.node("farewell")->side == "right" && !g.node("nothing"));
    // Writing it back and reading again gives the same graph.
    auto again = DialogueGraph::fromJson(g.toJson(), warnings);
    CHECK(again && again.value().toJson() == g.toJson());
    const auto images = g.images();
    CHECK(std::find(images.begin(), images.end(), "assets/mara.png") != images.end() &&
          std::find(images.begin(), images.end(), "assets/rex.png") != images.end() &&
          images.size() == 3);

    const auto error = [&](const char *text) {
        auto parsed = DialogueGraph::fromJson(J(text), warnings);
        return parsed ? std::string() : parsed.error();
    };
    CHECK(has(error("[]"), "object with 'pages' or 'nodes'"));
    CHECK(has(error("{}"), "needs 'nodes'"));
    CHECK(has(error(R"({"nodes":{}})"), "needs 'nodes'"));
    CHECK(has(error(R"({"version":9,"nodes":{"a":{"text":"x"}}})"), "version 9"));
    CHECK(has(error(R"({"nodes":{"a b":{"text":"x"}}})"), "not a usable id"));
    CHECK(has(error(R"({"nodes":{"a":3}})"), "node 'a': must be an object"));
    CHECK(has(error(R"({"nodes":{"a":{"text":"x","next":"zz"}}})"),
              "'next' names 'zz', which is not a node"));
    CHECK(has(error(R"({"nodes":{"a":{"text":"x","choices":[{"text":"c","next":"zz"}]}}})"),
              "choice 1 goes to 'zz'"));
    CHECK(has(error(R"({"nodes":{"a":{"text":"x","branch":[{"next":"zz"}]}}})"),
              "branch 1 goes to 'zz'"));
    CHECK(has(error(R"({"nodes":{"a":{"text":"x","choices":[{"next":"a"}]}}})"),
              "choice 1: needs 'text'"));
    CHECK(has(error(R"({"nodes":{"a":{"text":"x","choices":3}}})"), "'choices' must be a list"));
    CHECK(has(error(R"({"nodes":{"a":{"text":"x","branch":3}}})"), "'branch' must be a list"));
    CHECK(has(error(R"({"nodes":{"a":{"text":"x","choices":[{"text":"c","if":{"all":3}}]}}})"),
              "choice 1: if: "));
    CHECK(has(error(R"({"nodes":{"a":{"text":"x","actions":[{"nope":1}]}}})"), "actions: "));
    CHECK(has(error(R"({"nodes":{"a":{"text":"x","side":"up"}}})"), "\"left\" or \"right\""));
    CHECK(has(error(R"({"speakers":{"m":{"side":"top"}},"nodes":{"a":{"text":"x"}}})"),
              "speaker 'm'"));
    CHECK(has(error(R"({"speakers":{"m":{"portraits":{"d":3}}},"nodes":{"a":{"text":"x"}}})"),
              "must be an image path"));
    CHECK(has(error(R"({"start":"zz","nodes":{"a":{"text":"x"}}})"), "'start' names 'zz'"));
    warnings.clear();
    CHECK(DialogueGraph::fromJson(J(R"({"speakers":{"m":{"portraits":{"default":"p.png"}}},"nodes":{
        "a":{"speaker":"nobody","text":"x"},"b":{"speaker":"m","expression":"angry","text":"y"},"c":{}},"extra":1})"),
                                  warnings));
    CHECK(warnings.size() == 4 && has(warnings[0], "the speaker 'nobody'") &&
          has(warnings[1], "'angry'") && has(warnings[2], "node 'c': has no text") &&
          has(warnings[3], "'extra'"));
    warnings.clear();
    graph.value().check(warnings);
    CHECK(warnings.size() == 2); // "logic" and "silent" cannot be reached from "greet".
    CHECK(has(warnings[0], "'logic' can never be reached") &&
          has(warnings[1], "'silent' can never be reached"));
    warnings.clear();
    auto loopy = DialogueGraph::fromJson(
        J(R"({"nodes":{"a":{"text":"x","next":"b","choices":[{"text":"c","if":{"var":"v"}}]},"b":{"text":"y"}}})"),
        warnings);
    warnings.clear();
    loopy.value().check(warnings);
    CHECK(warnings.size() == 2 && has(warnings[0], "has choices, so its 'next'") &&
          has(warnings[1], "every choice has a condition"));
}

// ---- Walking a graph
// ---------------------------------------------------------------------------------
void walking() {
    Rig rig;
    rig.start();
    GameContext &game = *rig.runtime;
    std::vector<std::string> warnings;
    auto graph = DialogueGraph::fromJson(J(graphText), warnings);
    DialogueSession session(std::make_shared<const DialogueGraph>(std::move(graph.value())),
                            "mara");
    RuleContext rc(game);
    rc.self = rig.mara;
    rc.actor = rig.hero;
    CHECK(session.begin(rc) && !session.finished() && session.node()->id == "greet");
    // The first node: text with a variable, the speaker's name, portrait and side; its action ran.
    CHECK(session.line().speaker == "Keeper Mara" && session.line().text == "Hello, Ember." &&
          session.line().portrait == "assets/mara.png" && session.line().side == "left" &&
          session.line().expression.empty());
    CHECK_NEAR(game.blackboard().number("greetings"), 1.0);
    // The choice that needs trust is not on offer; the others are, with variables filled in.
    CHECK((session.line().choices == std::vector<std::string>{"Who are you?", "Goodbye (1)."}));
    CHECK((session.offered() == std::vector<int>{1, 2}));
    // Pick "Who are you?": an expression picks the portrait; "once" remembers it.
    CHECK(session.advance(rc, 0) && session.node()->id == "who" &&
          session.line().portrait == "assets/mara_smile.png" &&
          session.line().expression == "smile");
    CHECK(game.blackboard().flag("dialogue.seen.mara.greet.1"));
    CHECK(session.advance(rc) && session.node()->id == "greet"); // A plain next.
    CHECK_NEAR(game.blackboard().number("greetings"), 2.0);
    CHECK((session.line().choices == std::vector<std::string>{"Goodbye (2)."}));
    // Becoming trusted adds the favor; a branch picks where it goes next.
    game.blackboard().setBool("trusted", true);
    CHECK(session.advance(rc, 5) &&
          session.node()->id == "greet"); // Not on offer: nothing happens.
    CHECK(!session.advance(rc, 0) &&
          session.finished()); // "Goodbye" was the only one offered: the end.
    // A fresh walk with trust: the favor, then the branch that holds.
    DialogueSession second(std::make_shared<const DialogueGraph>(
                               DialogueGraph::fromJson(J(graphText), warnings).value()),
                           "second");
    CHECK(second.begin(rc));
    CHECK((second.line().choices ==
           std::vector<std::string>{"I need a favor.", "Who are you?", "Goodbye (3)."}));
    CHECK(second.advance(rc, 0) && second.node()->id == "favor" &&
          game.blackboard().flag("favor_given"));
    CHECK(second.advance(rc) &&
          second.node()->id == "farewell"); // Not owing anything: the plain way on.
    CHECK(second.line().side == "right" && second.line().speaker == "Keeper Mara");
    CHECK(!second.advance(rc) && second.finished() && !second.advance(rc)); // The end.
    game.blackboard().setBool("owes", true);
    DialogueSession third(std::make_shared<const DialogueGraph>(
                              DialogueGraph::fromJson(J(graphText), warnings).value()),
                          "third");
    third.begin(rc);
    third.advance(rc, 0);
    third.advance(rc); // Owing: the branch that holds.
    CHECK(third.node()->id == "debt" && third.line().speaker == "Rex" &&
          third.line().portrait == "assets/rex.png" && third.line().side == "right");
    CHECK(!third.advance(rc) && third.finished());
    // A node with no text passes straight through (its actions run); a graph of only such nodes
    // ends.
    auto logic = DialogueGraph::fromJson(
        J(R"({"start":"logic","nodes":{"logic":{"actions":[{"type":"AddVariable","name":"passes"}],"branch":[{"if":{"var":"never"},"next":"x"},{"next":"farewell"}]},
              "x":{"text":"unreachable"},"farewell":{"text":"Stay safe."}}})"),
        warnings);
    DialogueSession passing(std::make_shared<const DialogueGraph>(logic.value()), "logic");
    CHECK(passing.begin(rc) && passing.node()->id == "farewell" &&
          passing.line().text == "Stay safe." && game.blackboard().number("passes") == 1.0);
    auto silent = DialogueGraph::fromJson(
        J(R"({"nodes":{"a":{"actions":[{"type":"SetVariable","name":"silent_ran","value":true}]}}})"),
        warnings);
    DialogueSession quiet(std::make_shared<const DialogueGraph>(silent.value()), "silent");
    CHECK(quiet.begin(rc) && quiet.finished() && game.blackboard().flag("silent_ran"));
    auto spin =
        DialogueGraph::fromJson(J(R"({"nodes":{"a":{"next":"b"},"b":{"next":"a"}}})"), warnings);
    DialogueSession spinning(std::make_shared<const DialogueGraph>(spin.value()), "spin");
    CHECK(spinning.begin(rc) &&
          spinning.finished()); // No end to the loop of silent nodes: it gives up.
    // Choices that all have conditions and none hold: the conversation ends where it is.
    auto closed = DialogueGraph::fromJson(
        J(R"({"nodes":{"a":{"text":"hm","choices":[{"text":"x","if":{"var":"never"}}]}}})"),
        warnings);
    DialogueSession stuck(std::make_shared<const DialogueGraph>(closed.value()), "stuck");
    CHECK(stuck.begin(rc) && stuck.finished());
    DialogueSession empty(std::make_shared<const DialogueGraph>(DialogueGraph{}), "empty");
    CHECK(!empty.begin(rc));
}

// ---- The component
// -------------------------------------------------------------------------------------
void playing() {
    Rig rig;
    rig.start();
    Dialogue &dialogue = rig.dialogue();
    GameContext &game = *rig.runtime;
    CHECK(!dialogue.active() && !game.inputLocked());
    dialogue.begin(game, rig.hero);
    CHECK(dialogue.active() && game.inputLocked() &&
          dialogue.currentPage().speaker == "Keeper Mara" &&
          dialogue.currentPage().portrait.path == "assets/mara.png" &&
          dialogue.currentPage().side == "left");
    CHECK(dialogue.visibleText() == "Hello, Ember." && dialogue.choosing());
    CHECK((dialogue.choices() == std::vector<std::string>{"Who are you?", "Goodbye (1)."}) &&
          dialogue.selectedChoice() == 0);
    rig.step();
    CHECK(rig.count("conversation_started") == 1 &&
          rig.last("conversation_started")->other == rig.hero);
    // Move the selection (it wraps) and pick.
    rig.press(Key::D);
    CHECK(dialogue.selectedChoice() == 1);
    rig.press(Key::D);
    CHECK(dialogue.selectedChoice() == 0);
    rig.press(Key::A);
    CHECK(dialogue.selectedChoice() == 1);
    rig.press(Key::A);
    CHECK(dialogue.selectedChoice() == 0);
    rig.press(Key::E);
    CHECK(dialogue.active() && dialogue.currentPage().expression == "smile" &&
          dialogue.currentPage().portrait.path == "assets/mara_smile.png" &&
          dialogue.choices().empty());
    CHECK(rig.count("dialogue.choice") == 1 &&
          rig.last("dialogue.choice")->data.get("node").asString() == "greet" &&
          rig.last("dialogue.choice")->data.get("text").asString() == "Who are you?" &&
          rig.last("dialogue.choice")->other == rig.hero);
    CHECK(rig.count("conversation_page") == 1);
    rig.press(Key::E); // A node with no choices goes on.
    CHECK(dialogue.choices().size() == 1 && dialogue.choices()[0] == "Goodbye (2).");
    rig.press(Key::E); // "Goodbye".
    CHECK(!dialogue.active() && !game.inputLocked());
    rig.step();
    CHECK(rig.count("conversation_finished") == 1 && rig.count("conversation_page") == 2);
    // It can start again; the "once" choice stays gone.
    dialogue.begin(game, rig.hero);
    CHECK(dialogue.active() && dialogue.choices().size() == 1);
    dialogue.close(game);
    CHECK(!dialogue.active() && !game.inputLocked());
    dialogue.close(game); // Closing twice is nothing.
    rig.step();
    CHECK(rig.count("conversation_finished") == 2);
}

void typewriterAndLocks() {
    Rig rig(graphText, 10.0F);
    rig.start();
    Dialogue &dialogue = rig.dialogue();
    GameContext &game = *rig.runtime;
    dialogue.lockInput = false;
    dialogue.begin(game, rig.hero);
    CHECK(dialogue.active() && !game.inputLocked());
    CHECK(dialogue.visibleText().empty() && !dialogue.choosing());
    rig.step(30); // Half a second: five characters.
    CHECK(dialogue.visibleText().size() >= 4 && dialogue.visibleText().size() <= 6 &&
          !dialogue.choosing());
    rig.press(Key::E); // The first press shows it all.
    CHECK(dialogue.visibleText() == "Hello, Ember." && dialogue.choosing() && dialogue.active());
    rig.press(Key::D);
    CHECK(dialogue.selectedChoice() == 1);
    rig.press(Key::E);
    CHECK(!dialogue.active());
}

void rules() {
    Rig rig;
    Entity &console = rig.scene->createEntity("Console");
    CHECK(console.add<RuleSet>().setRulesJson(J(R"([
      {"id":"talk","when":"talk","then":{"type":"StartDialogue","entity":"name:Mara"}},
      {"id":"again","when":"again","then":{"type":"StartDialogue","entity":"name:Mara"}},
      {"id":"hush","when":"hush","then":{"type":"EndDialogue","entity":"name:Mara"}},
      {"id":"asks","when":"asks","if":{"type":"DialogueActive","entity":"name:Mara"},
       "then":{"type":"SetVariable","name":"active","value":true},"else":{"type":"SetVariable","name":"active","value":false}},
      {"id":"nobody","when":"nobody","then":{"type":"StartDialogue","entity":"name:Nobody"}}])")));
    const EntityId consoleId = console.id();
    rig.start();
    GameContext &game = *rig.runtime;
    const auto fire = [&](const char *name) {
        game.events().emit(GameEvent(name, consoleId, rig.hero));
        rig.step();
    };
    fire("asks");
    CHECK(game.blackboard().has("active") && !game.blackboard().flag("active"));
    fire("talk");
    CHECK(rig.dialogue().active() && rig.last("conversation_started")->other == rig.hero);
    fire("asks");
    CHECK(game.blackboard().flag("active"));
    fire("again"); // Already talking: the action fails and the conversation goes on.
    CHECK(rig.dialogue().active());
    fire("hush");
    CHECK(!rig.dialogue().active());
    fire("hush");
    fire("nobody");
    // Actions in a conversation see who is talking to whom.
    Rig other(
        R"({"nodes":{"a":{"text":"Hi.","actions":[{"type":"SetVariable","name":"talker","value":"$actor.entity.name"},
                                                            {"type":"SetVariable","name":"owner","value":"$self.entity.name"}]}}})");
    other.start();
    other.dialogue().begin(*other.runtime, other.hero);
    CHECK(other.runtime->blackboard().text("talker") == "Hero" &&
          other.runtime->blackboard().text("owner") == "Mara");

    // The graph's own conditions and actions are checked against the project's data.
    GameData data;
    std::vector<DataProblem> problems;
    data.add(J(R"({"items":[{"id":"favor_token"}]})"), "d.ykdata", problems);
    std::vector<std::string> graphWarnings;
    auto graph = DialogueGraph::fromJson(
        J(R"({"nodes":{"a":{"text":"x","actions":[{"type":"GiveItem","item":"unicorn"}],
              "choices":[{"text":"c","if":{"type":"HasItem","item":"favor_token"},"actions":[{"type":"Explode"}],"next":"b"}]},
              "b":{"text":"y","branch":[{"if":{"type":"HasItem","item":"nothing"},"next":"a"},{"next":"a"}]}}})"),
        graphWarnings);
    CHECK(graph);
    std::vector<DataProblem> found;
    graph.value().visitRules("mara.ykdialogue", [&](const RuleSource &source) {
        data.checkRules(*rig.registry.extension<RuleCatalog>(), source, found);
    });
    const auto problem = [&](const char *part) {
        return std::any_of(found.begin(), found.end(), [&](const DataProblem &item) {
            return item.file == "mara.ykdialogue" && has(item.message, part);
        });
    };
    CHECK(found.size() == 3);
    CHECK(problem("dialogue node 'a' actions: action 'GiveItem': there is no item 'unicorn'"));
    CHECK(problem("dialogue node 'a' choice 1 actions: unknown action 'Explode'"));
    CHECK(
        problem("dialogue node 'b' branch 1 if: condition 'HasItem': there is no item 'nothing'"));
}
} // namespace

int main() {
    setLogStderrEnabled(false);
    parsing();
    walking();
    playing();
    typewriterAndLocks();
    rules();
    return yk::test::finish("dialogue");
}
