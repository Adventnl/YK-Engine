// Stats, status effects and health: the definitions and their files, the numbers on a character
// (limits, modifiers, regeneration, events), effects (stacking, ticks, flags, actions), the health
// model with knockout and death, saved state, and the rule predicates and actions over them.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/stats/Stats.hpp"
#include <algorithm>
#include <cmath>
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
  "stats": [
    {"id": "health", "name": "Health", "min": 0, "max": 100, "start": "max",
     "thresholds": [{"at": 25, "event": "health.low"}]},
    {"id": "stamina", "min": 0, "max": 50, "start": "max", "regen": 10, "regenDelay": 1},
    {"id": "strength", "min": 0, "max": 100, "start": 10},
    {"id": "hunger", "min": 0, "max": 100, "start": "min", "regen": 2},
    {"id": "money", "min": 0, "max": 9999, "start": 0, "integer": true}
  ],
  "effects": [
    {"id": "poisoned", "duration": 4, "stacking": "stack", "maxStacks": 3,
     "ticks": [{"stat": "health", "amount": -2, "every": 1, "type": "poison"}],
     "modifiers": [{"stat": "strength", "add": -2}], "factors": {"move.speed": 0.8},
     "flags": ["no_sprint"], "tags": ["debuff", "poison"]},
    {"id": "stunned", "duration": 1.5, "flags": ["no_move", "no_attack"], "grants": ["access:test"],
     "tags": ["debuff"]},
    {"id": "knocked_out", "flags": ["no_move", "no_act"]},
    {"id": "boost", "duration": 3,
     "modifiers": [{"stat": "strength", "mult": 2}, {"stat": "health", "channel": "max", "add": 20}],
     "onApply": [{"type": "SetVariable", "name": "boosted", "value": true}],
     "onExpire": [{"type": "SetVariable", "name": "boosted", "value": false}]},
    {"id": "tonic", "duration": 2, "stacking": "extend"},
    {"id": "fragile", "factors": {"damage.taken": 2}},
    {"id": "ward", "duration": 5, "stacking": "ignore"}
  ]
})";

struct Rig {
    ComponentRegistry registry;
    MemoryAssets assets;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<GameRuntime> runtime;
    EntityId hero, other;
    std::vector<GameEvent> heard;

    Rig() {
        registerEngineComponents(registry);
        assets.files["data/character.ykdata"] = dataText;
        scene = std::make_unique<Scene>(registry, 3);
    }
    Entity &add(const char *name) {
        return scene->createEntity(name);
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
        runtime->stepOnce(Keyboard{}); // Components start.
    }
    void step(int ticks = 1) {
        for (int i = 0; i < ticks; ++i)
            runtime->stepOnce(Keyboard{});
    }
    Entity &entity(EntityId id) {
        return *runtime->scene().find(id);
    }
    StatSet &stats(EntityId id) {
        return *entity(id).get<StatSet>();
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
    std::vector<std::string> warnings;
    auto stat = StatDefinition::fromJson(
        J(R"({"id":"stamina","name":"Stamina","category":"vital","min":0,"max":50,"start":"max",
              "regen":8,"regenDelay":1.5,"thresholds":[{"at":10,"direction":"below","event":"tired"},
                                                        {"at":40,"direction":"above","event":"rested"}]})"),
        warnings);
    CHECK(stat && warnings.empty());
    if (stat) {
        const StatDefinition &def = stat.value();
        CHECK(def.id == "stamina" && def.min == 0 && def.max == 50 && def.startsFull &&
              def.startValue() == 50 && def.regen == 8 && def.regenDelay == 1.5 &&
              def.thresholds.size() == 2 && def.thresholds[0].below && !def.thresholds[1].below);
        auto again = StatDefinition::fromJson(def.toJson(), warnings);
        CHECK(again && again.value().toJson() == def.toJson());
    }
    auto numeric = StatDefinition::fromJson(
        J(R"({"id":"strength","min":1,"max":10,"start":4,"integer":true})"), warnings);
    CHECK(numeric && !numeric.value().startsFull && numeric.value().startValue() == 4 &&
          numeric.value().integer);
    auto minimum = StatDefinition::fromJson(J(R"({"id":"hunger","start":"min"})"), warnings);
    CHECK(minimum && minimum.value().startValue() == 0);

    const auto error = [&](const char *text) {
        auto parsed = StatDefinition::fromJson(J(text), warnings);
        return parsed ? std::string() : parsed.error();
    };
    CHECK(has(error("[]"), "must be an object"));
    CHECK(has(error("{}"), "'id' is needed"));
    CHECK(has(error(R"({"id":"a b"})"), "not a usable id"));
    CHECK(has(error(R"({"id":"x","min":5,"max":1})"), "'min' is above 'max'"));
    CHECK(has(error(R"({"id":"x","min":0,"max":10,"start":11})"), "'start'"));
    CHECK(has(error(R"({"id":"x","regenDelay":-1})"), "'regenDelay'"));
    CHECK(has(error(R"({"id":"x","thresholds":[{"event":"e"}]})"), "needs a number 'at'"));
    CHECK(has(error(R"({"id":"x","thresholds":[{"at":1,"direction":"sideways","event":"e"}]})"),
              "below"));
    CHECK(has(error(R"({"id":"x","thresholds":[{"at":1}]})"), "'event'"));
    warnings.clear();
    CHECK(StatDefinition::fromJson(J(R"({"id":"x","regenDelai":1})"), warnings));
    CHECK(warnings.size() == 1 && has(warnings[0], "unknown field 'regenDelai'"));

    auto modifier =
        StatModifierSpec::fromJson(J(R"({"stat":"health","channel":"max","add":20,"mult":1.5})"));
    CHECK(modifier && modifier.value().channel == StatChannel::Max && modifier.value().add == 20 &&
          modifier.value().mult == 1.5);
    CHECK(StatModifierSpec::fromJson(modifier.value().toJson()).value().toJson() ==
          modifier.value().toJson());
    CHECK(has(StatModifierSpec::fromJson(J(R"({"stat":"x"})")).error(), "changes nothing"));
    CHECK(has(StatModifierSpec::fromJson(J(R"({"stat":"x","add":1,"channel":"top"})")).error(),
              "'channel'"));
    CHECK(has(StatModifierSpec::fromJson(J(R"({"add":1})")).error(), "'stat' is needed"));

    auto effect = EffectDefinition::fromJson(
        J(R"({"id":"bleeding","duration":5,"stacking":"stack","maxStacks":4,
              "modifiers":[{"stat":"speed","mult":0.9}],
              "ticks":[{"stat":"health","amount":-1,"every":0.5,"type":"bleed"}],
              "flags":["no_sprint"],"factors":{"move.speed":0.9},"grants":["x"],"tags":["debuff"],
              "animation":"hurt","onApply":[{"type":"Log","message":"ouch"}],"hidden":true})"),
        warnings);
    CHECK(effect);
    if (effect) {
        const EffectDefinition &def = effect.value();
        CHECK(def.duration == 5 && def.stacking == EffectDefinition::Stacking::Stack &&
              def.maxStacks == 4 && def.modifiers.size() == 1 && def.ticks.size() == 1 &&
              def.ticks[0].interval == 0.5 && def.flags.size() == 1 &&
              def.factors.at("move.speed") == 0.9 && def.onApply.size() == 1 && def.hidden &&
              def.animation == "hurt");
        auto again = EffectDefinition::fromJson(def.toJson(), warnings);
        CHECK(again && again.value().toJson() == def.toJson());
    }
    const auto effectError = [&](const char *text) {
        auto parsed = EffectDefinition::fromJson(J(text), warnings);
        return parsed ? std::string() : parsed.error();
    };
    CHECK(has(effectError(R"({"id":"e","stacking":"pile"})"), "'stacking'"));
    CHECK(has(effectError(R"({"id":"e","modifiers":[{"stat":"s"}]})"), "modifier 1"));
    CHECK(has(effectError(R"({"id":"e","ticks":[{"stat":"s"}]})"), "tick 1"));
    CHECK(has(effectError(R"({"id":"e","factors":{"a":-1}})"), "factor 'a'"));
    CHECK(has(effectError(R"({"id":"e","onApply":[{"nope":1}]})"), "onApply"));
    CHECK(has(effectError(R"({"id":"e","duration":-1})"), "'duration'"));
}

void gameDataFiles() {
    MemoryAssets assets;
    assets.files["a/stats.ykdata"] = dataText;
    assets.files["b/more.ykdata"] = R"({"format":"yk.data","version":1,
        "stats":[{"id":"luck","max":10},{"id":"bad id"},{"id":"health"},{"min":1}],
        "effects":[{"id":"cursed","modifiers":[{"stat":"luck","add":-1},{"stat":"charm","add":1}],
                    "ticks":[{"stat":"vigor","amount":-1}]}],
        "tables":{"names":["A","B"]}})";
    assets.files["c/broken.ykdata"] = "{ nope";
    assets.files["d/newer.ykdata"] = R"({"version": 9, "stats": []})";
    assets.files["e/other.ykdata"] = R"({"format":"yk.item","items":[]})";
    assets.files["f/list.ykdata"] = "[1]";
    assets.files["g/typo.ykdata"] = R"({"stat": []})";
    assets.files["h/noise.txt"] = "ignored";
    std::vector<DataProblem> problems;
    const GameData data = GameData::load(assets, problems);
    // What is good loads; each broken part is named with its file.
    CHECK(data.stats.stats.size() == 6 && data.stats.stats.contains("luck") &&
          data.stats.stats.contains("health"));
    CHECK(data.stats.effects.size() == 8 && data.stats.effects.contains("cursed"));
    CHECK(data.tables.at("names").size() == 2);
    const auto problem = [&](const char *file, const char *part, bool error = true) {
        return std::any_of(problems.begin(), problems.end(), [&](const DataProblem &item) {
            return item.file == file && has(item.message, part) && item.error == error;
        });
    };
    CHECK(problem("b/more.ykdata", "stat 2 ('bad id')"));
    CHECK(problem("b/more.ykdata", "there are two definitions called 'health'"));
    CHECK(problem("b/more.ykdata", "stat 4: 'id' is needed"));
    CHECK(problem("c/broken.ykdata", ""));
    CHECK(problem("d/newer.ykdata", "version 9"));
    CHECK(problem("e/other.ykdata", "'yk.item'"));
    CHECK(problem("f/list.ykdata", "JSON object"));
    CHECK(problem("g/typo.ykdata", "unknown field 'stat'", false));
    // Definitions are checked against each other and the rules inside them against the catalog.
    ComponentRegistry registry;
    registerEngineComponents(registry);
    problems.clear();
    data.check(registry.extension<RuleCatalog>(), problems);
    CHECK(problem("b/more.ykdata", "names the stat 'charm', which is not defined"));
    CHECK(problem("b/more.ykdata", "names the stat 'vigor'"));
    CHECK(!problem("a/stats.ykdata", "not defined"));
    const auto rows = data.summary();
    CHECK(rows.size() == 3 && rows[0].first == "Stats" && rows[0].second == "6" &&
          rows[1].second == "8");
    CHECK(data.known("stat", "luck") && !data.known("stat", "charm") &&
          data.known("effect", "boost") && !data.known("effect", "nothing") &&
          data.known("gadget", "anything") /* not defined here: no opinion */);

    // A rule inside an effect that names something that does not exist.
    GameData bad;
    problems.clear();
    bad.add(J(R"({"stats":[{"id":"health"}],"effects":[
        {"id":"x","onApply":[{"type":"ModifyStat","stat":"nonexistent","amount":1},{"type":"Explode"}]}]})"),
            "bad.ykdata", problems);
    bad.check(registry.extension<RuleCatalog>(), problems);
    CHECK(problem("bad.ykdata", "there is no stat 'nonexistent'"));
    CHECK(problem("bad.ykdata", "unknown action 'Explode'"));
    CHECK(problem("bad.ykdata", "effect 'x' onApply"));
}

// ---- The numbers on a character
// ------------------------------------------------------------------
void statSet() {
    Rig rig;
    Entity &hero = rig.add("Hero");
    auto &set = hero.add<StatSet>();
    set.start = J(R"({"strength": 40, "money": 12.7, "unknown": 5, "health": 999})");
    rig.hero = hero.id();
    rig.start();
    StatSet &stats = rig.stats(rig.hero);
    CHECK(stats.has("health") && stats.has("money") && !stats.has("mana") &&
          stats.ids().size() == 5);
    CHECK_NEAR(stats.value("health"), 100.0);
    CHECK_NEAR(stats.value("strength"), 40.0);
    CHECK_NEAR(stats.value("money"), 13.0); // Whole numbers: 12.7 starts as 13.
    CHECK(stats.value("hunger") < 0.1);     // Begins at the minimum and has regenerated for a tick.
    CHECK_NEAR(stats.fraction("health"), 1.0);
    CHECK_NEAR(stats.value("mana"), 0.0); // Not a stat: nothing, never a crash.
    GameContext &game = *rig.runtime;

    // Deliberate changes: limits, return value, events.
    CHECK_NEAR(stats.add(game, "health", -30, "damage:blunt"), -30.0);
    CHECK_NEAR(stats.value("health"), 70.0);
    CHECK_NEAR(stats.add(game, "health", 50, "heal"), 30.0); // Only 30 fit.
    CHECK_NEAR(stats.value("health"), 100.0);
    CHECK_NEAR(stats.add(game, "health", 5), 0.0);
    rig.step();
    CHECK(rig.count("stat.changed") == 2); // A change that changes nothing is not an event.
    const GameEvent *changed = rig.last("stat.changed");
    CHECK(changed && changed->data.get("stat").asString() == "health" &&
          changed->data.get("from").asNumber() == 70 && changed->data.get("to").asNumber() == 100 &&
          changed->data.get("cause").asString() == "heal" && changed->source == rig.hero);
    CHECK(rig.count("stat.full") == 1);

    // Thresholds fire when crossed in their direction, once per crossing.
    rig.heard.clear();
    stats.add(game, "health", -70); // 100 -> 30: not yet below 25.
    stats.add(game, "health", -10); // 30 -> 20: crosses.
    stats.add(game, "health", -5);  // 20 -> 15: already below.
    rig.step();
    CHECK(rig.count("health.low") == 1);
    const GameEvent *low = rig.last("health.low");
    CHECK(low && low->data.get("at").asNumber() == 25 && low->data.get("to").asNumber() == 20);
    stats.add(game, "health", 30); // Back above: nothing for a "below" threshold.
    stats.add(game, "health", -30);
    rig.step();
    CHECK(rig.count("health.low") == 2);
    rig.heard.clear();
    stats.add(game, "health", -1000);
    rig.step();
    CHECK_NEAR(stats.value("health"), 0.0);
    CHECK(rig.count("stat.depleted") == 1);
    stats.set(game, "health", 60);
    CHECK_NEAR(stats.value("health"), 60.0);

    // Spending needs enough, and holds regeneration back for the stat's delay.
    CHECK_NEAR(stats.value("stamina"), 50.0);
    CHECK(stats.spend(game, "stamina", 30));
    CHECK(!stats.spend(game, "stamina", 30) && stats.canSpend("stamina", 20) &&
          !stats.canSpend("stamina", 20.5));
    CHECK_NEAR(stats.value("stamina"), 20.0);
    rig.step(30); // Half a second: the one second delay still holds it.
    CHECK_NEAR(stats.value("stamina"), 20.0);
    rig.step(60); // 1.5 s since the spend: 0.5 s of regeneration at 10 per second.
    CHECK(stats.value("stamina") > 24.0 && stats.value("stamina") < 26.5);
    rig.step(240);
    CHECK_NEAR(stats.value("stamina"), 50.0); // Back to the top; it stops there.
    CHECK(rig.count("stat.full") >= 1);
    // A stat with negative regeneration drains and a positive one without delay grows.
    CHECK(stats.value("hunger") > 5.0 &&
          stats.value("hunger") < 15.0); // About 2 per second over ~6.5 s.

    // Whole-number stats round what is added.
    CHECK_NEAR(stats.add(game, "money", 0.4), 0.0);
    CHECK_NEAR(stats.add(game, "money", 5.5), 6.0);
    CHECK_NEAR(stats.value("money"), 19.0);
    CHECK_NEAR(stats.add(game, "money", -100), -19.0);
}

void modifiers() {
    Rig rig;
    rig.hero = rig.add("Hero").add<StatSet>().entity().id();
    rig.start();
    StatSet &stats = rig.stats(rig.hero);
    GameContext &game = *rig.runtime;
    const double base = stats.value("strength");
    CHECK_NEAR(base, 10.0);
    // Adds are summed first, then the multipliers are applied.
    StatModifierSpec plus;
    plus.stat = "strength";
    plus.add = 5;
    StatModifierSpec twice;
    twice.stat = "strength";
    twice.mult = 2.0;
    stats.addModifier(game, plus, "equip:glove");
    CHECK_NEAR(stats.value("strength"), 15.0);
    stats.addModifier(game, twice, "effect:rage");
    CHECK_NEAR(stats.value("strength"), 30.0);
    CHECK_NEAR(stats.base("strength"), 10.0); // The stored number is untouched.
    CHECK(stats.removeModifiers(game, "effect:rage") == 1 &&
          stats.removeModifiers(game, "effect:rage") == 0);
    CHECK_NEAR(stats.value("strength"), 15.0);
    // `set` aims at what is read, through the modifiers.
    stats.set(game, "strength", 20.0);
    CHECK_NEAR(stats.value("strength"), 20.0);
    CHECK_NEAR(stats.base("strength"), 15.0);
    CHECK(stats.removeModifiers(game, "equip:glove") == 1);
    CHECK_NEAR(stats.value("strength"), 15.0);

    // The limit and the pace can be modified too; losing a higher limit pulls the value down.
    StatModifierSpec bigger;
    bigger.stat = "health";
    bigger.channel = StatChannel::Max;
    bigger.add = 20;
    stats.addModifier(game, bigger, "item:vest");
    CHECK_NEAR(stats.max("health"), 120.0);
    CHECK_NEAR(stats.value("health"), 100.0);
    CHECK_NEAR(stats.fraction("health"), 100.0 / 120.0);
    stats.add(game, "health", 50);
    CHECK_NEAR(stats.value("health"), 120.0);
    stats.removeModifiers(game, "item:vest");
    CHECK_NEAR(stats.max("health"), 100.0);
    CHECK_NEAR(stats.value("health"), 100.0);
    StatModifierSpec faster;
    faster.stat = "stamina";
    faster.channel = StatChannel::Regen;
    faster.mult = 3.0;
    stats.addModifier(game, faster, "effect:stim");
    CHECK_NEAR(stats.regen("stamina"), 30.0);
    stats.removeModifiers(game, "effect:stim");
    CHECK_NEAR(stats.regen("stamina"), 10.0);

    // A modifier with a duration ends by itself.
    stats.addModifier(game, plus, "effect:timed", 2.0);
    CHECK_NEAR(stats.value("strength"), 20.0);
    rig.step(100);
    CHECK_NEAR(stats.value("strength"), 20.0);
    rig.step(25);
    CHECK_NEAR(stats.value("strength"), 15.0);
    CHECK(stats.modifiers().empty());
    const auto revision = stats.revision();
    stats.addModifier(game, plus, "x");
    CHECK(stats.revision() > revision);
    // A modifier for a stat that does not exist is ignored.
    plus.stat = "luck";
    stats.addModifier(game, plus, "y");
    CHECK(stats.modifiers().size() == 1);
}

void statState() {
    Json saved;
    {
        Rig rig;
        auto &set = rig.add("Hero").add<StatSet>();
        rig.hero = set.entity().id();
        rig.start();
        StatSet &stats = rig.stats(rig.hero);
        GameContext &game = *rig.runtime;
        stats.add(game, "health", -35);
        stats.add(game, "money", 40);
        StatModifierSpec plus;
        plus.stat = "strength";
        plus.add = 7;
        stats.addModifier(game, plus, "equip:ring");
        stats.addModifier(game, plus, "effect:brief", 3.0);
        rig.step(60);
        saved = stats.saveState();
    }
    Rig rig;
    rig.hero = rig.add("Hero").add<StatSet>().entity().id();
    rig.start();
    StatSet &stats = rig.stats(rig.hero);
    CHECK(stats.loadState(*rig.runtime, saved));
    CHECK_NEAR(stats.value("health"), 65.0);
    CHECK_NEAR(stats.value("money"), 40.0);
    CHECK_NEAR(stats.value("strength"), 24.0); // 10 + 7 + 7
    CHECK(stats.modifiers().size() == 2);
    rig.step(130); // The timed one had about two seconds left.
    CHECK_NEAR(stats.value("strength"), 17.0);
    CHECK(has(stats.loadState(*rig.runtime, J(R"({"modifiers":[{"stat":"x"}]})")).error(),
              "stat modifier 1"));
}

// ---- Status effects
// ------------------------------------------------------------------------------
void effects() {
    Rig rig;
    Entity &hero = rig.add("Hero");
    hero.add<StatSet>();
    hero.add<StatusEffects>();
    rig.hero = hero.id();
    rig.start();
    StatSet &stats = rig.stats(rig.hero);
    auto &fx = *rig.entity(rig.hero).get<StatusEffects>();
    GameContext &game = *rig.runtime;

    CHECK(!fx.has("poisoned") && fx.factor("move.speed") == 1.0 && !fx.hasFlag("no_sprint"));
    CHECK(fx.apply(game, "poisoned", 0.0, rig.other));
    CHECK(fx.has("poisoned") && fx.stacks("poisoned") == 1 && fx.hasFlag("no_sprint") &&
          fx.hasTag("poison") && !fx.hasFlag("no_move") && !fx.grants("access:test"));
    CHECK_NEAR(fx.factor("move.speed"), 0.8);
    CHECK_NEAR(stats.value("strength"), 8.0); // 10 - 2
    CHECK(!fx.apply(game, "no_such_effect"));
    rig.runtime->events().dispatch();
    CHECK(rig.count("effect.applied") == 1);
    CHECK(rig.last("effect.applied")->data.get("effect").asString() == "poisoned");

    // It ticks: -2 health a second, for four seconds, then ends and gives the strength back.
    rig.step(58);
    CHECK_NEAR(stats.value("health"), 100.0);
    rig.step(2);
    CHECK_NEAR(stats.value("health"), 98.0);
    rig.step(60);
    CHECK_NEAR(stats.value("health"), 96.0);
    rig.step(120);
    CHECK_NEAR(stats.value("health"), 92.0);
    CHECK(!fx.has("poisoned") && rig.count("effect.expired") == 1);
    CHECK_NEAR(stats.value("strength"), 10.0);
    CHECK_NEAR(fx.factor("move.speed"), 1.0);

    // Stacking: each application adds a stack up to the limit, scaling modifiers and factors.
    fx.apply(game, "poisoned");
    fx.apply(game, "poisoned");
    CHECK(fx.stacks("poisoned") == 2);
    CHECK_NEAR(stats.value("strength"), 6.0);
    CHECK_NEAR(fx.factor("move.speed"), 0.64);
    fx.apply(game, "poisoned");
    fx.apply(game, "poisoned");
    CHECK(fx.stacks("poisoned") == 3);
    CHECK_NEAR(stats.value("strength"), 4.0);
    CHECK(fx.active().size() == 1);
    stats.set(game, "health", 100);
    rig.step(61);
    CHECK_NEAR(stats.value("health"), 94.0); // Three stacks: -6 a second.
    CHECK(fx.remove(game, "poisoned") && !fx.remove(game, "poisoned"));
    CHECK_NEAR(stats.value("strength"), 10.0);
    rig.step();
    CHECK(rig.count("effect.removed") == 1);

    // Flags, grants and a duration of the effect's own.
    fx.apply(game, "stunned");
    CHECK(fx.hasFlag("no_move") && fx.hasFlag("no_attack") && fx.grants("access:test") &&
          fx.hasTag("debuff"));
    rig.step(80);
    CHECK(fx.has("stunned"));
    rig.step(15);
    CHECK(!fx.has("stunned") && !fx.hasFlag("no_move"));
    // A duration given at the call replaces it.
    fx.apply(game, "stunned", 0.5);
    rig.step(35);
    CHECK(!fx.has("stunned"));

    // Refresh (the default for effects that say so), extend and ignore.
    fx.apply(game, "tonic");
    rig.step(60);
    fx.apply(game, "tonic"); // Extends: about 1 + 2 seconds left.
    rig.step(100);
    CHECK(fx.has("tonic"));
    rig.step(85);
    CHECK(!fx.has("tonic"));
    CHECK(fx.apply(game, "ward"));
    rig.step(60);
    CHECK(!fx.apply(game, "ward")); // Ignored while it is on.
    rig.step(250);
    CHECK(!fx.has("ward") && fx.apply(game, "ward"));
    fx.remove(game, "ward");

    // Actions run when it starts and ends; modifiers on the limit work like items'.
    CHECK(!rig.runtime->blackboard().has("boosted"));
    fx.apply(game, "boost");
    CHECK(rig.runtime->blackboard().flag("boosted"));
    CHECK_NEAR(stats.value("strength"), 20.0);
    CHECK_NEAR(stats.max("health"), 120.0);
    rig.step(185);
    CHECK(!fx.has("boost") && rig.runtime->blackboard().has("boosted") &&
          !rig.runtime->blackboard().flag("boosted"));
    CHECK_NEAR(stats.value("strength"), 10.0);
    CHECK_NEAR(stats.max("health"), 100.0);

    // Removing a group by tag; saved effects come back with their stacks and time.
    fx.apply(game, "poisoned");
    fx.apply(game, "poisoned");
    fx.apply(game, "stunned");
    CHECK(fx.removeTagged(game, "debuff") == 2 && fx.active().empty());
    fx.apply(game, "poisoned");
    fx.apply(game, "poisoned");
    rig.step(60);
    const Json saved = fx.saveState();
    CHECK(saved.get("active").size() == 1);

    Rig again;
    Entity &twin = again.add("Hero");
    twin.add<StatSet>();
    twin.add<StatusEffects>();
    again.hero = twin.id();
    again.start();
    auto &loaded = *again.entity(again.hero).get<StatusEffects>();
    CHECK(loaded.loadState(*again.runtime, saved));
    CHECK(loaded.stacks("poisoned") == 2 && loaded.active().front().remaining > 2.5 &&
          loaded.active().front().remaining < 3.1);
    CHECK_NEAR(again.stats(again.hero).value("strength"), 6.0);
    setLogStderrEnabled(false);
    CHECK(loaded.loadState(*again.runtime, J(R"({"active":[{"effect":"gone"}]})")) &&
          loaded.active().empty());
    CHECK_NEAR(again.stats(again.hero).value("strength"), 10.0);
}

void immunities() {
    Rig rig;
    Entity &hero = rig.add("Hero");
    hero.add<StatSet>();
    auto &fx = hero.add<StatusEffects>();
    fx.immunities = {"poison", "stunned"};
    fx.initial = {"knocked_out", "boost"};
    rig.hero = hero.id();
    rig.start();
    auto &live = *rig.entity(rig.hero).get<StatusEffects>();
    GameContext &game = *rig.runtime;
    CHECK(live.has("knocked_out") && live.has("boost") && live.hasFlag("no_act"));
    CHECK(!live.apply(game, "poisoned")); // By tag.
    CHECK(!live.apply(game, "stunned"));  // By id.
    CHECK(live.apply(game, "fragile") && live.factor("damage.taken") == 2.0);
}

// ---- Health
// ---------------------------------------------------------------------------------------
void health() {
    Rig rig;
    Entity &hero = rig.add("Hero");
    hero.add<StatSet>();
    hero.add<StatusEffects>();
    auto &h = hero.add<Health>();
    h.atZero = Health::AtZero::KnockOut;
    h.invulnerableSeconds = 0.5F;
    h.resistances = J(R"({"fire":0.5,"poison":0.5,"acid":-1})");
    h.knockedOutSeconds = 2.0F;
    h.recoveringSeconds = 1.0F;
    h.recoverFraction = 0.4F;
    rig.hero = hero.id();
    rig.other = rig.add("Attacker").id();
    rig.start();
    Health &health = *rig.entity(rig.hero).get<Health>();
    GameContext &game = *rig.runtime;
    CHECK(health.alive() && health.active() && !health.knockedOut() && health.current() == 100.0 &&
          health.maximum() == 100.0);

    DamageInfo hit;
    hit.amount = 10;
    hit.type = "blunt";
    hit.source = rig.other;
    CHECK_NEAR(health.damage(game, hit), 10.0);
    CHECK_NEAR(health.current(), 90.0);
    rig.step();
    const GameEvent *damaged = rig.last("damaged");
    CHECK(damaged && damaged->source == rig.hero && damaged->other == rig.other &&
          damaged->data.get("amount").asNumber() == 10 &&
          damaged->data.get("type").asString() == "blunt" &&
          damaged->data.get("health").asNumber() == 90);
    // The invulnerability window: nothing gets through, unless the damage ignores it.
    CHECK(health.invulnerable(game));
    CHECK_NEAR(health.damage(game, hit), 0.0);
    CHECK_NEAR(health.current(), 90.0);
    hit.ignoresInvulnerability = true;
    CHECK_NEAR(health.damage(game, hit), 10.0);
    hit.ignoresInvulnerability = false;
    rig.step(35);
    CHECK(!health.invulnerable(game));
    // Resistances by type; weaknesses (negative) take extra; unknown types are full.
    hit.type = "fire";
    CHECK_NEAR(health.damage(game, hit), 5.0);
    rig.step(35);
    hit.type = "acid";
    CHECK_NEAR(health.damage(game, hit), 20.0);
    rig.step(35);
    CHECK_NEAR(health.current(), 55.0);
    // A status effect can scale all damage taken.
    rig.entity(rig.hero).get<StatusEffects>()->apply(game, "fragile");
    hit.type = "blunt";
    CHECK_NEAR(health.damage(game, hit), 20.0);
    rig.entity(rig.hero).get<StatusEffects>()->remove(game, "fragile");
    CHECK_NEAR(health.resisted(hit), 10.0);
    CHECK_NEAR(health.damage(game, DamageInfo{}), 0.0); // No amount, no damage.
    // Healing.
    rig.step(35);
    CHECK_NEAR(health.heal(game, 10, rig.other), 10.0);
    CHECK_NEAR(health.heal(game, 1000), 55.0);
    CHECK_NEAR(health.heal(game, -5), 0.0);
    rig.step();
    CHECK(rig.count("healed") == 2);

    // Knockout: down for a time, then recovering, then up again with some health, and the
    // knocked_out effect is on for as long as it lasts.
    rig.heard.clear();
    hit.amount = 1000;
    hit.ignoresInvulnerability = true;
    CHECK_NEAR(health.damage(game, hit), 100.0);
    CHECK(health.state() == Health::State::KnockedOut && health.knockedOut() && !health.active() &&
          health.alive());
    rig.step();
    CHECK(rig.count("knocked_out") == 1 && rig.count("died") == 0);
    auto &fx = *rig.entity(rig.hero).get<StatusEffects>();
    CHECK(fx.has("knocked_out") && fx.hasFlag("no_act"));
    rig.step(110);
    CHECK(health.state() == Health::State::KnockedOut);
    rig.step(20);
    CHECK(health.state() == Health::State::Recovering && health.knockedOut() &&
          rig.count("recovering") == 1);
    rig.step(40);
    CHECK(health.state() == Health::State::Recovering);
    rig.step(20);
    CHECK(health.active() && rig.count("recovered") == 1 && !fx.has("knocked_out"));
    CHECK_NEAR(health.current(), 40.0); // recoverFraction of the maximum.
    // Revive wakes it early.
    hit.amount = 1000;
    health.damage(game, hit);
    CHECK(health.knockedOut());
    health.revive(game, 0.75);
    CHECK(health.active() && !fx.has("knocked_out"));
    CHECK_NEAR(health.current(), 75.0);

    // Saved state.
    health.damage(game, hit);
    rig.step(30);
    const Json saved = health.saveState();
    health.revive(game);
    CHECK(health.active());
    CHECK(health.loadState(game, saved) && health.state() == Health::State::KnockedOut);
    CHECK(has(health.loadState(game, J(R"({"state":9})")).error(), "not valid"));
}

void death() {
    Rig rig;
    Entity &doomed = rig.add("Doomed");
    doomed.add<StatSet>();
    auto &h = doomed.add<Health>();
    h.atZero = Health::AtZero::Die;
    h.destroyOnDeath = true;
    rig.hero = doomed.id();
    Entity &immortal = rig.add("Immortal");
    immortal.add<StatSet>();
    immortal.add<Health>().atZero = Health::AtZero::Nothing;
    rig.other = immortal.id();
    Entity &downAt = rig.add("DownAt");
    downAt.add<StatSet>();
    auto &d = downAt.add<Health>();
    d.downAt = 30.0F;
    d.atZero = Health::AtZero::Die;
    const EntityId downId = downAt.id();
    rig.start();
    GameContext &game = *rig.runtime;
    DamageInfo kill;
    kill.amount = 500;
    rig.entity(rig.other).get<Health>()->damage(game, kill);
    CHECK(rig.entity(rig.other).get<Health>()->active() &&
          rig.entity(rig.other).get<Health>()->current() == 0.0);
    // Dies at the level it was told to go down at, not only at the bottom.
    DamageInfo wound;
    wound.amount = 69;
    rig.entity(downId).get<Health>()->damage(game, wound);
    CHECK(rig.entity(downId).get<Health>()->alive());
    wound.amount = 2;
    rig.entity(downId).get<Health>()->damage(game, wound);
    CHECK(!rig.entity(downId).get<Health>()->alive());
    // Dead is dead: no more damage or healing, until it is revived.
    CHECK_NEAR(rig.entity(downId).get<Health>()->damage(game, wound), 0.0);
    CHECK_NEAR(rig.entity(downId).get<Health>()->heal(game, 50), 0.0);
    rig.entity(downId).get<Health>()->revive(game, 0.5);
    CHECK(rig.entity(downId).get<Health>()->alive() &&
          rig.entity(downId).get<Health>()->current() >= 50.0);
    rig.entity(rig.hero).get<Health>()->damage(game, kill);
    CHECK(!rig.entity(rig.hero).get<Health>()->alive());
    rig.step();
    CHECK(rig.count("died") == 2);
    CHECK(rig.runtime->scene().find(rig.hero) == nullptr); // destroyOnDeath.
}

void damageOverTime() {
    Rig rig;
    Entity &hero = rig.add("Hero");
    hero.add<StatSet>();
    hero.add<StatusEffects>();
    auto &h = hero.add<Health>();
    h.resistances = J(R"({"poison":0.5})");
    h.invulnerableSeconds = 5.0F; // Poison ignores it.
    rig.hero = hero.id();
    rig.start();
    auto &fx = *rig.entity(rig.hero).get<StatusEffects>();
    fx.apply(*rig.runtime, "poisoned", 0.0, rig.other);
    rig.step(125);
    // Two ticks of 2, halved by the poison resistance, through the health model.
    CHECK_NEAR(rig.entity(rig.hero).get<Health>()->current(), 98.0);
    CHECK(rig.count("damaged") == 2);
    CHECK(rig.last("damaged")->data.get("type").asString() == "poison");
}

// ---- Rules over stats
// ----------------------------------------------------------------------------
void rules() {
    Rig rig;
    Entity &hero = rig.add("Hero");
    hero.add<StatSet>();
    hero.add<StatusEffects>();
    hero.add<Health>();
    rig.hero = hero.id();
    Entity &console = rig.add("Console");
    rig.other = console.id();
    auto &set = console.add<RuleSet>();
    CHECK(set.setRulesJson(J(R"([
      {"id":"strong","when":"try","if":{"type":"StatAtLeast","stat":"strength","value":30},
       "then":{"type":"SetVariable","name":"strong","value":true},
       "else":{"type":"SetVariable","name":"strong","value":false}},
      {"id":"train","when":"train","then":[{"type":"ModifyStat","stat":"strength","amount":25},
                                           {"type":"ModifyStat","stat":"money","set":50}]},
      {"id":"hurt","when":"hurt","then":[{"type":"Damage","amount":60,"damageType":"blunt"},
                                         {"type":"ApplyEffect","effect":"stunned"}]},
      {"id":"facts","when":"look","then":[
         {"type":"SetVariable","name":"pct","value":"$actor.stat.health.pct"},
         {"type":"SetVariable","name":"max","value":"$actor.stat.health.max"},
         {"type":"SetVariable","name":"stunned","value":"$actor.effect.stunned"},
         {"type":"SetVariable","name":"cannot_move","value":"$actor.effect.flag.no_move"},
         {"type":"SetVariable","name":"alive","value":"$actor.health.alive"},
         {"type":"SetVariable","name":"stacks","value":"$actor.effect.stunned.stacks"}]},
      {"id":"low","when":"check","if":{"all":[{"fact":"actor.stat.health.pct","op":"<","value":50},
                                               {"type":"HasEffect","effect":"stunned"},{"type":"IsAlive"},
                                               {"not":{"type":"IsKnockedOut"}}]},
       "then":{"type":"SetVariable","name":"low","value":true}},
      {"id":"cure","when":"cure","then":[{"type":"RemoveEffect","tag":"debuff"},{"type":"Heal","amount":15}]},
      {"id":"wake","when":"wake","then":{"type":"Revive","fraction":0.5}}])")));
    rig.start();
    const auto fire = [&](const char *name) {
        rig.runtime->events().emit(GameEvent(name, rig.other, rig.hero));
        rig.step();
    };
    GameContext &game = *rig.runtime;
    Blackboard &board = rig.runtime->blackboard();
    fire("try");
    CHECK(board.has("strong") && !board.flag("strong"));
    fire("train");
    fire("try");
    CHECK(board.flag("strong"));
    CHECK_NEAR(rig.stats(rig.hero).value("money"), 50.0);
    fire("hurt");
    CHECK_NEAR(rig.stats(rig.hero).value("health"), 40.0);
    CHECK(rig.entity(rig.hero).get<StatusEffects>()->has("stunned"));
    fire("look");
    CHECK_NEAR(board.number("pct"), 40.0);
    CHECK_NEAR(board.number("max"), 100.0);
    CHECK(board.flag("stunned") && board.flag("cannot_move") && board.flag("alive") &&
          board.integer("stacks") == 1);
    fire("check");
    CHECK(board.flag("low"));
    fire("cure");
    CHECK(!rig.entity(rig.hero).get<StatusEffects>()->has("stunned"));
    CHECK_NEAR(rig.stats(rig.hero).value("health"), 55.0);
    // A hit on something that has no health fails and says so; others still work.
    RuleContext ctx(game);
    ctx.actor = rig.other;
    ctx.origin = "test";
    CHECK(execute(Action::fromJson(J(R"({"type":"Damage","amount":5})")).value(), ctx) ==
          ActionResult::Failed);
    CHECK(
        execute(
            Action::fromJson(
                J(R"({"type":"ModifyStat","stat":"nonexistent","amount":5,"entity":"name:Hero"})"))
                .value(),
            ctx) == ActionResult::Failed);
    rig.entity(rig.hero).get<Health>()->damage(game, DamageInfo{500, "", {}, {}, 0, true, true});
    CHECK(!rig.entity(rig.hero).get<Health>()->alive());
    fire("wake");
    CHECK(rig.entity(rig.hero).get<Health>()->alive() &&
          rig.entity(rig.hero).get<Health>()->current() >= 50.0);

    // Validation of the rules and the components against what the project defines.
    const GameData &data = gameData(game);
    class Collect final : public RuleReport {
      public:
        std::vector<std::string> errors;
        const GameData *data{};
        void error(const std::string &message) override {
            errors.push_back(message);
        }
        void warning(const std::string &) override {}
        bool known(std::string_view kind, std::string_view id) const override {
            return data->known(kind, id);
        }
    } report;
    report.data = &data;
    const auto catalog = rig.registry.extension<RuleCatalog>();
    auto bad =
        rulesFromJson(J(R"([{"when":"x","then":[{"type":"ModifyStat","stat":"charm","amount":1},
        {"type":"ApplyEffect","effect":"hexed"},{"type":"ModifyStat","stat":"health"},
        {"type":"RemoveEffect"}],"if":{"type":"StatAtLeast","stat":"wit","value":3}}])"));
    CHECK(bad);
    check(*catalog, bad.value().front(), report);
    CHECK(report.errors.size() == 5);
    CHECK(std::any_of(report.errors.begin(), report.errors.end(),
                      [](const std::string &m) { return has(m, "no stat 'charm'"); }));
    CHECK(std::any_of(report.errors.begin(), report.errors.end(),
                      [](const std::string &m) { return has(m, "no effect 'hexed'"); }));
    CHECK(std::any_of(report.errors.begin(), report.errors.end(),
                      [](const std::string &m) { return has(m, "needs 'amount'"); }));
    CHECK(std::any_of(report.errors.begin(), report.errors.end(),
                      [](const std::string &m) { return has(m, "needs an 'effect' or a 'tag'"); }));

    // The components' own checks.
    auto &statSet = *rig.entity(rig.hero).get<StatSet>();
    statSet.start = J(R"({"strength":"strong","charm":3})");
    std::vector<std::string> problems;
    CheckContext context;
    context.known = [&](std::string_view kind, std::string_view id) {
        return data.known(kind, id);
    };
    statSet.type().check(rig.entity(rig.hero), statSet, context, problems);
    CHECK(problems.size() == 2 && has(problems[0], "'strength' is not a number") &&
          has(problems[1], "'charm'"));
    problems.clear();
    auto &healthComponent = *rig.entity(rig.hero).get<Health>();
    healthComponent.stat = "vigor";
    healthComponent.type().check(rig.entity(rig.hero), healthComponent, context, problems);
    CHECK(problems.size() == 1 && has(problems[0], "'vigor' is not defined"));
}
} // namespace

int main() {
    setLogStderrEnabled(false);
    definitions();
    gameDataFiles();
    statSet();
    modifiers();
    statState();
    effects();
    immunities();
    health();
    death();
    damageOverTime();
    rules();
    return yk::test::finish("stats");
}
