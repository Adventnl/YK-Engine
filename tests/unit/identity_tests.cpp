// Who characters are and how they regard each other: faction files and their checks, persistent
// identity and the registry that finds characters by it, personal feelings together with factions,
// disguises, saved state, and the rule predicates, facts and actions around them.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/sim/Factions.hpp"
#include "yk/sim/Identity.hpp"
#include "yk/stats/Stats.hpp"
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

const char *dataText = R"({
  "format": "yk.data", "version": 1,
  "stats": [{"id": "health", "max": 100}],
  "effects": [
    {"id": "guard_uniform", "flags": ["disguise.guards"]},
    {"id": "scrubs", "flags": ["disguise.medics"]},
    {"id": "layered", "flags": ["disguise.medics", "disguise.guards", "no_sprint"]}
  ],
  "factions": [
    {"id": "guards", "name": "Guards", "default": "neutral",
     "relations": {"inmates": "suspicious", "dogs": "hostile"}},
    {"id": "inmates", "name": "Inmates", "relations": {"guards": "neutral", "medics": "friendly"}},
    {"id": "medics", "name": "Medics", "default": "friendly"},
    {"id": "dogs", "default": "hostile", "relations": {"guards": "friendly"}}
  ]
})";

struct Rig {
    ComponentRegistry registry;
    MemoryAssets assets;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<GameRuntime> runtime;
    std::vector<GameEvent> heard;

    Rig() {
        registerEngineComponents(registry);
        assets.files["data/world.ykdata"] = dataText;
        scene = std::make_unique<Scene>(registry, 11);
    }
    Entity &actor(const char *name, const char *id, const char *faction, const char *role = "") {
        Entity &entity = scene->createEntity(name);
        auto &who = entity.add<Identity>();
        who.id = id;
        who.faction = faction;
        who.role = role;
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
        step();
    }
    void step(int ticks = 1) {
        for (int i = 0; i < ticks; ++i)
            runtime->stepOnce(Keyboard{});
    }
    ActorService &actors() {
        return runtime->services().get<ActorService>();
    }
    Entity &find(const char *id) {
        return *actors().find(*runtime, id);
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

// ---- The faction file
// -----------------------------------------------------------------------------
void definitions() {
    std::vector<DataProblem> problems;
    MemoryAssets assets;
    assets.files["data/world.ykdata"] = dataText;
    const GameData data = GameData::load(assets, problems);
    CHECK(problems.empty());
    const FactionCatalog &factions = data.factions;
    CHECK(factions.factions.size() == 4 && factions.factions.find("guards")->name == "Guards" &&
          factions.factions.find("dogs")->name == "dogs"); // The name defaults to the id.
    CHECK(data.known("faction", "guards") && !data.known("faction", "wardens") &&
          data.known("gadget", "anything")); // Kinds it does not know answer yes.
    // How one regards another: what it says, else its default; its own kind is friendly.
    CHECK(factions.relation("guards", "inmates") == Relation::Suspicious &&
          factions.relation("inmates", "guards") == Relation::Neutral &&
          factions.relation("guards", "guards") == Relation::Friendly &&
          factions.relation("guards", "medics") == Relation::Neutral &&
          factions.relation("guards", "dogs") == Relation::Hostile &&
          factions.relation("dogs", "inmates") == Relation::Hostile &&
          factions.relation("medics", "dogs") == Relation::Friendly);
    // No faction, or one nobody defined, is neutral toward everyone.
    CHECK(factions.relation("", "guards") == Relation::Neutral &&
          factions.relation("ghosts", "guards") == Relation::Neutral);
    CHECK(factions.relation("guards", "") == Relation::Neutral);
    const auto rows = data.summary();
    CHECK(std::any_of(rows.begin(), rows.end(), [](const auto &row) {
        return row.first == "Factions" && row.second == "4";
    }));

    // Writing a faction back and reading it again gives the same one.
    std::vector<std::string> warnings;
    auto again = FactionDefinition::fromJson(factions.factions.find("inmates")->toJson(), warnings);
    CHECK(again && again.value().toJson() == factions.factions.find("inmates")->toJson() &&
          warnings.empty());

    // What is wrong is said, and the rest still loads.
    MemoryAssets bad;
    bad.files["data/bad.ykdata"] = R"({"factions": [
      {"id": "ok", "relations": {"ghosts": "friendly"}},
      {"id": "bad id"},
      {"id": "moody", "default": "angry"},
      {"id": "odd", "relations": {"ok": "ecstatic"}},
      {"id": "list", "relations": ["ok"]},
      {"id": "ok"},
      {"id": "typo", "defualt": "hostile"},
      3]})";
    problems.clear();
    GameData broken = GameData::load(bad, problems);
    const auto problem = [&](const char *part) {
        return std::any_of(problems.begin(), problems.end(),
                           [&](const DataProblem &item) { return has(item.message, part); });
    };
    CHECK(problem("'bad id' is not a usable id") &&
          problem("'default' must be friendly, neutral, suspicious or hostile") &&
          problem("the relation to 'ok' must be friendly") &&
          problem("'relations' must be an object") && problem("two definitions called 'ok'") &&
          problem("unknown field 'defualt'") && problem("must be an object"));
    CHECK(broken.factions.factions.size() == 2); // "ok" and "typo".
    problems.clear();
    broken.check(nullptr, problems);
    CHECK(problems.size() == 1 &&
          has(problems[0].message, "relation to 'ghosts', which is not a faction"));
    std::vector<DataProblem> notAList;
    FactionCatalog catalog;
    catalog.load(J(R"({"factions": 3})"), "f.ykdata", notAList);
    CHECK(notAList.size() == 1 && has(notAList[0].message, "must be a list"));
}

// ---- Identity and the registry
// ------------------------------------------------------------------------
void identities() {
    Rig rig;
    rig.actor("Warden", "npc.warden", "guards", "guard");
    rig.actor("Mara", "npc.mara", "medics", "medic");
    rig.actor("Dave The Inmate", "", "inmates", "inmate"); // No id: made up from the name.
    rig.actor("Dave The Inmate", "", "inmates", "inmate"); // The same name again.
    rig.actor("Clone", "npc.warden", "guards", "guard");   // Wants an id that is taken.
    rig.scene->createEntity("Rock");                       // No identity: not a character.
    rig.start();
    ActorService &actors = rig.actors();
    CHECK(actors.all().size() == 5);
    CHECK(rig.find("npc.warden").name() == "Warden" && rig.find("npc.mara").name() == "Mara");
    // Made-up ids come from the name, and are unique.
    CHECK(rig.find("actor.dave_the_inmate").name() == "Dave The Inmate");
    const auto &ids = actors.all();
    std::vector<std::string> all;
    for (const EntityId id : ids)
        all.push_back(rig.runtime->scene().find(id)->get<Identity>()->id);
    std::sort(all.begin(), all.end());
    CHECK(std::adjacent_find(all.begin(), all.end()) == all.end());
    CHECK(std::count_if(all.begin(), all.end(), [](const std::string &id) {
              return id.starts_with("actor.dave_the_inmate");
          }) == 2);
    CHECK(std::count_if(all.begin(), all.end(),
                        [](const std::string &id) { return id.starts_with("npc.warden"); }) == 2);
    // A character is found by id, by faction and by role; unknown ids find nothing.
    CHECK(!actors.find(*rig.runtime, "npc.nobody") && !actors.idOf("npc.nobody") &&
          actors.idOf("npc.mara") == rig.runtime->scene().findByName("Mara")->id());
    CHECK(actors.inFaction(*rig.runtime, "guards").size() == 2 &&
          actors.inFaction(*rig.runtime, "inmates").size() == 2 &&
          actors.inFaction(*rig.runtime, "nobody").empty());
    CHECK(actors.withRole(*rig.runtime, "medic").size() == 1 &&
          actors.withRole(*rig.runtime, "medic")[0]->name() == "Mara");
    // The display name falls back to the entity's.
    Identity &mara = *rig.find("npc.mara").get<Identity>();
    CHECK(mara.name() == "Mara");
    mara.displayName = "Nurse Mara";
    CHECK(mara.name() == "Nurse Mara");

    // Changing who they are is seen by the registry and announced.
    const std::uint64_t before = actors.revision();
    mara.setFaction(*rig.runtime, "guards");
    mara.setRole(*rig.runtime, "guard");
    mara.setRole(*rig.runtime, "guard"); // Nothing changed.
    rig.step();
    CHECK(actors.revision() > before && actors.inFaction(*rig.runtime, "guards").size() == 3 &&
          actors.withRole(*rig.runtime, "medic").empty());
    CHECK(rig.count("identity.faction_changed") == 1 && rig.count("identity.role_changed") == 1 &&
          rig.last("identity.faction_changed")->data.get("from").asString() == "medics" &&
          rig.last("identity.faction_changed")->data.get("to").asString() == "guards" &&
          rig.last("identity.faction_changed")->source == rig.find("npc.mara").id());

    // A character that goes is forgotten; its id is free again.
    const EntityId gone = rig.find("npc.mara").id();
    rig.runtime->destroyLater(gone);
    rig.step();
    CHECK(!actors.find(*rig.runtime, "npc.mara") && actors.all().size() == 4);
    CHECK(actors.add(EntityId{}, "npc.mara", "x") == "npc.mara"); // Free again.
    actors.remove(EntityId{}, "npc.mara");

    // Saved state: the service keeps its counter, a character keeps who it became.
    CHECK(actors.saveKey() == "actors");
    const Json saved = actors.saveState();
    CHECK(saved.get("serial").isString());
    Json corrupt = Json::object();
    corrupt.set("serial", "x");
    CHECK(!actors.loadState(*rig.runtime, corrupt));
    CHECK(actors.loadState(*rig.runtime, saved));
    Identity &warden = *rig.find("npc.warden").get<Identity>();
    warden.setRole(*rig.runtime, "captain");
    const Json state = warden.saveState();
    CHECK(state.get("id").asString() == "npc.warden" && state.get("role").asString() == "captain" &&
          state.get("faction").asString() == "guards");
    warden.setRole(*rig.runtime, "guard");
    Json moved = state;
    moved.set("id", "npc.warden_2"); // Saved under another id (it was renamed on the way).
    CHECK(warden.loadState(*rig.runtime, moved));
    CHECK(warden.role == "captain" && warden.id == "npc.warden_2" &&
          &rig.find("npc.warden_2") == &warden.entity() &&
          !actors.find(*rig.runtime, "npc.warden")); // The old id is free.
    rig.step();
}

void disguises() {
    Rig rig;
    rig.actor("Guard", "g", "guards", "guard");
    Entity &inmate = rig.actor("Inmate", "i", "inmates", "inmate");
    inmate.add<StatSet>();
    inmate.add<StatusEffects>();
    rig.start();
    Entity &guard = rig.find("g");
    Entity &prisoner = rig.find("i");
    StatusEffects &effects = *prisoner.get<StatusEffects>();
    // As themselves, an inmate is what they are; in a uniform they look like a guard.
    CHECK(perceivedFaction(prisoner) == "inmates" && perceivedFaction(guard) == "guards");
    CHECK(effects.apply(*rig.runtime, "guard_uniform"));
    CHECK(perceivedFaction(prisoner) == "guards" && prisoner.get<Identity>()->faction == "inmates");
    // The flags of the effects that are on, without the prefix, each once, first worn first.
    CHECK(effects.apply(*rig.runtime, "scrubs"));
    CHECK((effects.flagsWithPrefix("disguise.") == std::vector<std::string>{"guards", "medics"}));
    CHECK(effects.apply(*rig.runtime, "layered"));
    CHECK((effects.flagsWithPrefix("disguise.") == std::vector<std::string>{"guards", "medics"}));
    CHECK(effects.flagsWithPrefix("no_").size() == 1 && effects.flagsWithPrefix("none.").empty());
    CHECK(effects.remove(*rig.runtime, "guard_uniform"));
    CHECK((effects.flagsWithPrefix("disguise.") == std::vector<std::string>{"medics", "guards"}));
    // How the guard regards them depends on whether it is fooled.
    CHECK(relationBetween(*rig.runtime, guard, prisoner) == Relation::Suspicious);
    CHECK(relationBetween(*rig.runtime, guard, prisoner, true) == Relation::Neutral); // As medics.
    CHECK(effects.remove(*rig.runtime, "scrubs") && effects.remove(*rig.runtime, "layered"));
    CHECK(relationBetween(*rig.runtime, guard, prisoner, true) == Relation::Suspicious);
}

// ---- Feelings
// -------------------------------------------------------------------------------------------
void feelings() {
    Rig rig;
    Entity &guard = rig.actor("Guard", "guard.1", "guards", "guard");
    guard.add<Relationships>().start =
        J(R"({"inmate.1": {"opinion": 20, "trust": 120, "hostility": -5}})");
    Entity &second = rig.actor("Guard Two", "guard.2", "guards", "guard");
    second.add<Relationships>();
    rig.actor("Inmate", "inmate.1", "inmates", "inmate");
    rig.actor("Inmate Two", "inmate.2", "inmates", "inmate");
    rig.actor("Dog", "dog.1", "dogs");
    rig.actor("Stranger", "stranger.1", "");
    rig.start();
    Entity &g1 = rig.find("guard.1");
    Entity &g2 = rig.find("guard.2");
    Entity &i1 = rig.find("inmate.1");
    Entity &i2 = rig.find("inmate.2");
    Relationships &r1 = *g1.get<Relationships>();
    Relationships &r2 = *g2.get<Relationships>();

    // The starting feelings are read and kept within their ranges.
    CHECK(r1.toward("inmate.1").opinion == 20.0 && r1.toward("inmate.1").trust == 100.0 &&
          r1.toward("inmate.1").hostility == 0.0 && r1.toward("nobody").empty());
    // Without feelings the factions decide: guards are suspicious of inmates.
    CHECK(relationBetween(*rig.runtime, g2, i1) == Relation::Suspicious &&
          relationBetween(*rig.runtime, g2, rig.find("dog.1")) == Relation::Hostile &&
          relationBetween(*rig.runtime, g2, g1) == Relation::Friendly &&
          relationBetween(*rig.runtime, i1, g1) == Relation::Neutral &&
          relationBetween(*rig.runtime, g2, rig.find("stranger.1")) == Relation::Neutral);
    // A character with no Relationships component at all is judged by factions alone.
    CHECK(relationBetween(*rig.runtime, rig.find("stranger.1"), g1) == Relation::Neutral);

    // Liking someone enough makes them a friend despite their faction; a hostile faction stays
    // hostile.
    CHECK(relationBetween(*rig.runtime, g1, i1) == Relation::Suspicious); // 20 is not enough yet.
    CHECK(r1.change(*rig.runtime, "inmate.1", 35.0, 0.0, 0.0));
    CHECK(relationBetween(*rig.runtime, g1, i1) == Relation::Friendly);
    CHECK(rig.runtime->scene().find(r1.entity().id()) &&
          r1.change(*rig.runtime, "dog.1", 80.0, 0.0, 0.0));
    CHECK(relationBetween(*rig.runtime, g1, rig.find("dog.1")) == Relation::Hostile);
    // Disliking someone makes a neutral stranger suspicious, but never softens the factions.
    CHECK(r2.change(*rig.runtime, "stranger.1", -40.0, 0.0, 0.0));
    CHECK(relationBetween(*rig.runtime, g2, rig.find("stranger.1")) == Relation::Suspicious);
    // Enough hostility is enmity, whatever the factions and the opinion say.
    CHECK(r1.change(*rig.runtime, "inmate.1", 0.0, 0.0, 70.0));
    CHECK(relationBetween(*rig.runtime, g1, i1) == Relation::Hostile);
    CHECK(r1.change(*rig.runtime, "inmate.1", 0.0, 0.0, -70.0));
    CHECK(relationBetween(*rig.runtime, g1, i1) == Relation::Friendly);
    // Limits.
    CHECK(r1.change(*rig.runtime, "inmate.2", 500.0, 500.0, 500.0));
    CHECK(r1.toward("inmate.2").opinion == 100.0 && r1.toward("inmate.2").trust == 100.0 &&
          r1.toward("inmate.2").hostility == 100.0);
    CHECK(r1.change(*rig.runtime, "inmate.2", -500.0, -500.0, -500.0));
    CHECK(r1.toward("inmate.2").opinion == -100.0 && r1.toward("inmate.2").trust == 0.0 &&
          r1.toward("inmate.2").hostility == 0.0);
    CHECK(!r1.change(*rig.runtime, "inmate.2", -1.0, 0.0, 0.0)); // Already at the floor.
    CHECK(!r1.change(*rig.runtime, "", 5.0, 0.0, 0.0));          // Nobody in particular.
    // Events say who felt what about whom.
    rig.step();
    const GameEvent *event = rig.last("relationship.changed");
    CHECK(event && event->source == g1.id() &&
          event->data.get("subject").asString() == "inmate.2" &&
          event->data.get("opinion").asNumber() == -100.0 && event->other == i2.id());
    const int changes = rig.count("relationship.changed");
    CHECK(changes >= 5);
    // Back to nothing removes the entry.
    CHECK(r1.set(*rig.runtime, "inmate.2", PersonalRelation{}));
    CHECK(r1.all().count("inmate.2") == 0);
    CHECK(!r1.set(*rig.runtime, "inmate.2", PersonalRelation{}));

    // Saved state.
    const Json state = r1.saveState();
    CHECK(state.get("feelings").get("inmate.1").get("opinion").asNumber() == 55.0 &&
          state.get("feelings").contains("dog.1"));
    r1.set(*rig.runtime, "inmate.1", PersonalRelation{-90.0, 0.0, 0.0});
    CHECK(r1.loadState(*rig.runtime, state));
    CHECK(r1.toward("inmate.1").opinion == 55.0 && r1.toward("dog.1").opinion == 80.0 &&
          r1.all().size() == 2);
    CHECK(r1.loadState(*rig.runtime, Json::object()) && r1.all().empty()); // Nothing was saved.
}

void forgetting() {
    Rig rig;
    Entity &guard = rig.actor("Guard", "guard.1", "guards");
    auto &feelings = guard.add<Relationships>();
    feelings.forgetPerSecond = 0.5F;
    feelings.start = J(R"({"inmate.1": {"opinion": -60, "trust": 40, "hostility": 80}})");
    rig.actor("Inmate", "inmate.1", "inmates");
    rig.start();
    Relationships &r = *rig.find("guard.1").get<Relationships>();
    CHECK(r.toward("inmate.1").hostility > 75.0);
    rig.step(60); // A second: half of it is forgotten.
    CHECK_NEAR(r.toward("inmate.1").hostility, 40.0, 1.0);
    CHECK_NEAR(r.toward("inmate.1").opinion, -30.0, 1.0);
    rig.step(60 * 40); // Long enough for it all to fade away.
    CHECK(r.toward("inmate.1").empty());
}

// ---- Rules
// ---------------------------------------------------------------------------------------
void rules() {
    Rig rig;
    Entity &warden = rig.actor("Warden", "npc.warden", "guards", "guard");
    warden.add<Relationships>();
    CHECK(warden.add<RuleSet>().setRulesJson(J(R"([
      {"id": "is_susp", "when": "ask", "if": {"type": "Relation", "toward": "actor", "is": "suspicious"},
       "then": {"type": "SetVariable", "name": "is_susp", "value": true}},
      {"id": "at_least", "when": "ask", "if": {"type": "Relation", "toward": "actor", "atLeast": "suspicious"},
       "then": {"type": "SetVariable", "name": "at_least", "value": true}},
      {"id": "at_most", "when": "ask", "if": {"type": "Relation", "toward": "actor", "atMost": "neutral"},
       "then": {"type": "SetVariable", "name": "at_most", "value": true}},
      {"id": "seen_as", "when": "ask", "if": {"type": "Relation", "toward": "actor", "is": "neutral", "perceived": true},
       "then": {"type": "SetVariable", "name": "seen_as", "value": true}},
      {"id": "faction", "when": "ask", "if": {"type": "InFaction", "faction": "inmates"},
       "then": {"type": "SetVariable", "name": "in_inmates", "value": true}},
      {"id": "seen_faction", "when": "ask", "if": {"type": "InFaction", "faction": "medics", "perceived": true},
       "then": {"type": "SetVariable", "name": "looks_medic", "value": true}},
      {"id": "role", "when": "ask", "if": {"type": "HasRole", "role": "inmate"},
       "then": {"type": "SetVariable", "name": "is_inmate", "value": true}},
      {"id": "facts", "when": "ask",
       "then": [{"type": "SetVariable", "name": "who", "value": "$actor.identity.name"},
                {"type": "SetVariable", "name": "fac", "value": "$actor.identity.faction"},
                {"type": "SetVariable", "name": "role_fact", "value": "$actor.identity.role"},
                {"type": "SetVariable", "name": "pid", "value": "$actor.identity.id"},
                {"type": "SetVariable", "name": "cell", "value": "$actor.identity.data.home"},
                {"type": "SetVariable", "name": "seen_fac", "value": "$actor.identity.perceivedFaction"}]},
      {"id": "like", "when": "like",
       "then": {"type": "ChangeRelationship", "toward": "actor", "opinion": 70, "trust": 10}},
      {"id": "hate", "when": "hate",
       "then": {"type": "ChangeRelationship", "toward": "actor", "hostility": 80}},
      {"id": "nobody", "when": "nobody",
       "then": {"type": "ChangeRelationship", "toward": "name:Rock", "opinion": 5}},
      {"id": "recruit", "when": "recruit",
       "then": [{"type": "SetFaction", "faction": "guards"}, {"type": "SetRole", "role": "trainee"}]},
      {"id": "scan", "when": "scan",
       "then": [{"type": "SetVariable", "name": "opinion_fact", "value": "$self.relationship.opinion.inmate.1"},
                {"type": "SetVariable", "name": "relation_fact", "value": "$self.relationship.relation.inmate.1"},
                {"type": "SetVariable", "name": "hostility_fact", "value": "$self.relationship.hostility.inmate.1"},
                {"type": "SetVariable", "name": "trust_fact", "value": "$self.relationship.trust.inmate.1"}]}
    ])")));
    Entity &inmate = rig.actor("Inmate", "inmate.1", "inmates", "inmate");
    inmate.get<Identity>()->data = J(R"({"home": "cell_12"})");
    inmate.add<StatSet>();
    inmate.add<StatusEffects>();
    rig.scene->createEntity("Rock");
    rig.start();
    GameContext &game = *rig.runtime;
    const EntityId wardenId = rig.find("npc.warden").id();
    const EntityId inmateId = rig.find("inmate.1").id();
    const auto fire = [&](const char *name) {
        game.events().emit(GameEvent(name, wardenId, inmateId));
        rig.step(2);
    };
    fire("ask");
    CHECK(game.blackboard().flag("is_susp") && game.blackboard().flag("at_least") &&
          !game.blackboard().flag("at_most") && !game.blackboard().flag("seen_as") &&
          game.blackboard().flag("in_inmates") && !game.blackboard().flag("looks_medic") &&
          game.blackboard().flag("is_inmate"));
    CHECK(game.blackboard().text("who") == "Inmate" && game.blackboard().text("fac") == "inmates" &&
          game.blackboard().text("role_fact") == "inmate" &&
          game.blackboard().text("pid") == "inmate.1" &&
          game.blackboard().text("cell") == "cell_12" &&
          game.blackboard().text("seen_fac") == "inmates");
    // In a medic's scrubs they are seen as a medic: neutral, and one of them to the guard's eyes.
    CHECK(rig.find("inmate.1").get<StatusEffects>()->apply(game, "scrubs"));
    for (const char *name : {"is_susp", "at_least", "at_most", "seen_as", "looks_medic"})
        game.blackboard().setBool(name, false);
    fire("ask");
    CHECK(game.blackboard().flag("is_susp") &&
          game.blackboard().flag("seen_as")); // Guards: neutral to medics.
    CHECK(game.blackboard().flag("looks_medic") && game.blackboard().text("seen_fac") == "medics");
    // Feelings.
    fire("scan");
    CHECK(game.blackboard().number("opinion_fact") == 0.0 &&
          game.blackboard().text("relation_fact") == "suspicious");
    fire("like");
    fire("scan");
    CHECK(game.blackboard().number("opinion_fact") == 70.0 &&
          game.blackboard().number("trust_fact") == 10.0 &&
          game.blackboard().text("relation_fact") == "friendly");
    fire("hate");
    fire("scan");
    CHECK(game.blackboard().number("hostility_fact") == 80.0 &&
          game.blackboard().text("relation_fact") == "hostile");
    fire("nobody"); // Nobody to feel about: nothing happens, nothing breaks.
    // Changing sides.
    game.events().emit(GameEvent("recruit", wardenId, inmateId));
    rig.step(2);
    // SetFaction and SetRole act on the actor by default: the inmate was recruited.
    CHECK(rig.find("inmate.1").get<Identity>()->faction == "guards" &&
          rig.find("inmate.1").get<Identity>()->role == "trainee");

    // The checks the validator makes of what rules say.
    const RuleCatalog &catalog = *rig.registry.extension<RuleCatalog>();
    GameData data;
    std::vector<DataProblem> problems;
    data.add(J(dataText), "d.ykdata", problems);
    std::vector<DataProblem> found;
    const auto check = [&](const char *text) {
        auto action = Action::fromJson(J(text));
        std::vector<Action> list;
        if (action)
            list.push_back(action.value());
        RuleSource source{"x.ykdata", "test", nullptr, &list};
        data.checkRules(catalog, source, found);
    };
    check(R"({"type": "SetFaction", "faction": "wardens"})");
    check(R"({"type": "SetFaction", "faction": "guards"})");
    check(R"({"type": "SetRole"})");
    CHECK(found.size() == 2 && has(found[0].message, "there is no faction 'wardens'") &&
          has(found[1].message, "needs 'role'"));
    // A relation test that tests nothing is pointed out.
    found.clear();
    auto vague = Condition::fromJson(J(R"({"type": "Relation", "toward": "actor"})"));
    CHECK(vague);
    RuleSource vagueSource{"x.ykdata", "test", &vague.value(), nullptr};
    data.checkRules(catalog, vagueSource, found);
    CHECK(found.size() == 1 && !found[0].error &&
          has(found[0].message, "says nothing about which relation"));
}

// ---- What the scene and the validator say
// -----------------------------------------------------------------------
void checks() {
    Rig rig;
    rig.actor("Guard", "g", "guards");
    rig.start();
    const ComponentType *type = rig.registry.find("Identity");
    CHECK(type && type->check);
    if (!type || !type->check)
        return;
    GameData data;
    std::vector<DataProblem> problems;
    data.add(J(dataText), "d.ykdata", problems);
    const auto problemsOf = [&](const std::string &id, const std::string &faction, bool prefab,
                                const char *dataJson = "{}") {
        Identity who;
        who.id = id;
        who.faction = faction;
        who.data = J(dataJson);
        CheckContext context;
        context.prefab = prefab;
        context.known = [&](std::string_view kind, std::string_view name) {
            return data.known(kind, name);
        };
        std::vector<std::string> found;
        type->check(*rig.runtime->scene().findByName("Guard"), who, context, found);
        return found;
    };
    CHECK(problemsOf("npc.ok", "guards", false).empty());
    const auto bad = problemsOf("npc bad!", "wardens", false);
    CHECK(bad.size() == 2 && has(bad[0], "may only hold letters, digits and _ - . :") &&
          has(bad[1], "'wardens', which is not defined"));
    const auto empty = problemsOf("", "", false);
    CHECK(empty.size() == 1 && has(empty[0], "has no persistent id"));
    const auto inPrefab = problemsOf("npc.x", "", true);
    CHECK(inPrefab.size() == 1 && has(inPrefab[0], "is part of a prefab"));
    CHECK(problemsOf("", "", true).empty()); // A prefab leaves it empty.
    const auto notObject = problemsOf("a", "", false, "[1]");
    CHECK(notObject.size() == 1 && has(notObject[0], "'data' must be an object"));
}
} // namespace

int main() {
    setLogStderrEnabled(false);
    definitions();
    identities();
    disguises();
    feelings();
    forgetting();
    rules();
    checks();
    return yk::test::finish("identity");
}
