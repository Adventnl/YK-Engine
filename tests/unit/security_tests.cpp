// Security levels and lockdowns as data: entering and leaving levels runs actions, levels decay,
// lockdowns count down and fail, state saves, bad definitions are reported, and an AccessPolicy
// lets factions, roles, conditions and lockdowns decide who may use a thing.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/sim/Identity.hpp"
#include "yk/sim/Security.hpp"
#include <algorithm>
#include <string>
#include <vector>

using namespace yk;

namespace {
const char *dataText = R"({
  "format": "yk.data", "version": 1,
  "factions": [{"id": "staff"}, {"id": "inmates"}],
  "security": {
    "levels": [
      {"id": "normal", "name": "Normal"},
      {"id": "alert", "decay": 10,
       "onEnter": [{"type": "SetVariable", "name": "alerted", "value": true}],
       "onExit": [{"type": "SetVariable", "name": "calmed", "value": true}]},
      {"id": "lockdown"}],
    "lockdowns": [
      {"id": "riot", "countdown": 5, "level": "lockdown",
       "onStart": [{"type": "SetVariable", "name": "riot_on", "value": true}],
       "onEnd": [{"type": "SetVariable", "name": "riot_over", "value": true}],
       "onFail": [{"type": "SetVariable", "name": "riot_lost", "value": true}]}]}
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
        scene = std::make_unique<Scene>(registry, 3);
    }
    Entity &person(const char *name, const char *faction, const char *role) {
        Entity &entity = scene->createEntity(name);
        auto &who = entity.add<Identity>();
        who.id = std::string("npc.") + name;
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
        runtime->events().subscribe("*", [this](const GameEvent &e) { heard.push_back(e); });
        step();
    }
    void step(int ticks = 1) {
        for (int i = 0; i < ticks; ++i)
            runtime->stepOnce(Keyboard{});
    }
    SecurityService &security() {
        return runtime->services().get<SecurityService>();
    }
    int count(const char *name) const {
        return static_cast<int>(std::count_if(heard.begin(), heard.end(),
                                              [&](const GameEvent &e) { return e.name == name; }));
    }
    bool flag(const char *name) {
        return runtime->blackboard().flag(name);
    }
};

void levels() {
    Rig rig;
    rig.start();
    SecurityService &s = rig.security();
    CHECK(s.level() == 0 && s.levelId(*rig.runtime) == "normal");
    CHECK(s.setLevel(*rig.runtime, "alert", "test"));
    CHECK(rig.flag("alerted") && s.level() == 1 && !s.setLevel(*rig.runtime, "alert"));
    CHECK(!s.setLevel(*rig.runtime, "nonsense"));
    rig.step();
    CHECK(rig.count("security.changed") == 1);
    CHECK(s.raise(*rig.runtime, 10, "clamped") && s.levelId(*rig.runtime) == "lockdown");
    CHECK(rig.flag("calmed"));
    CHECK(s.raise(*rig.runtime, -10) && s.level() == 0);
}

void decay() {
    Rig rig;
    rig.start();
    SecurityService &s = rig.security();
    s.setLevel(*rig.runtime, "alert");
    rig.step(60 * 5);
    CHECK(s.level() == 1); // Five seconds: still up.
    rig.step(60 * 6);
    CHECK(s.level() == 0);
}

void lockdowns() {
    Rig rig;
    rig.start();
    SecurityService &s = rig.security();
    CHECK(s.startLockdown(*rig.runtime, "riot") && !s.startLockdown(*rig.runtime, "riot"));
    CHECK(rig.flag("riot_on") && s.levelId(*rig.runtime) == "lockdown" && s.lockdown() == "riot");
    rig.step(60 * 2);
    CHECK(s.lockdownRemaining() < 4.0 && rig.count("lockdown.countdown") >= 1);
    CHECK(s.endLockdown(*rig.runtime));
    CHECK(rig.flag("riot_over") && !rig.flag("riot_lost") && s.lockdown().empty());
    rig.step();
    CHECK(rig.count("lockdown.ended") == 1);

    CHECK(s.startLockdown(*rig.runtime, "riot"));
    rig.step(60 * 6);
    CHECK(rig.flag("riot_lost") && s.lockdown().empty());
    const GameEvent *ended = nullptr;
    for (const GameEvent &e : rig.heard)
        if (e.name == "lockdown.ended")
            ended = &e;
    CHECK(ended && ended->data.get("failed").asBool(false));
}

void saving() {
    Rig rig;
    rig.start();
    SecurityService &s = rig.security();
    s.startLockdown(*rig.runtime, "riot");
    rig.step(60);
    const Json state = s.saveState();
    Rig other;
    other.start();
    CHECK(other.security().loadState(*other.runtime, state));
    CHECK(other.security().lockdown() == "riot" && other.security().level() == 2);
    CHECK(other.security().lockdownRemaining() > 3.0 && other.security().lockdownRemaining() < 5.0);
}

void validation() {
    ComponentRegistry registry;
    registerEngineComponents(registry);
    MemoryAssets assets;
    assets.files["data/bad.ykdata"] = R"({"format": "yk.data", "version": 1,
      "security": {"levels": [{"id": "a", "onEnter": [{"type": "NoSuchAction"}]}],
                   "lockdowns": [{"id": "x", "level": "ghost"}]}})";
    std::vector<DataProblem> problems;
    const GameData game = GameData::load(assets, problems);
    game.check(registry.extension<RuleCatalog>(), problems);
    const auto mentions = [&](const char *text) {
        return std::any_of(problems.begin(), problems.end(), [&](const DataProblem &p) {
            return p.message.find(text) != std::string::npos;
        });
    };
    CHECK(mentions("ghost"));
    CHECK(mentions("NoSuchAction"));
}

void access() {
    Rig rig;
    Entity &guard = rig.person("guard", "staff", "guard");
    Entity &inmate = rig.person("inmate", "inmates", "inmate");
    Entity &door = rig.scene->createEntity("Door");
    auto &policy = door.add<AccessPolicy>();
    policy.allowedFactions = {"staff"};
    policy.lockedDuringLockdown = true;
    policy.deniedMessage = "Staff only";
    Entity &gate = rig.scene->createEntity("Gate");
    auto &calm = gate.add<AccessPolicy>();
    calm.allowedRoles = {"inmate", "guard"};
    calm.access = Json::parse(R"({"type": "SecurityLevel", "atMost": 0})").value();
    rig.start();
    GameRuntime &game = *rig.runtime;
    const Entity *g = game.scene().findByName("guard");
    const Entity *i = game.scene().findByName("inmate");
    const auto *doorPolicy = game.scene().findByName("Door")->get<AccessPolicy>();
    const auto *gatePolicy = game.scene().findByName("Gate")->get<AccessPolicy>();
    std::string why;
    CHECK(doorPolicy->allows(game, *g) && !doorPolicy->allows(game, *i, &why) &&
          why == "Staff only");
    CHECK(gatePolicy->allows(game, *i));
    rig.security().setLevel(game, "alert");
    CHECK(!gatePolicy->allows(game, *i));
    rig.security().setLevel(game, "normal");
    rig.security().startLockdown(game, "riot");
    CHECK(!doorPolicy->allows(game, *g, &why) && why == "Staff only");
    (void)guard;
    (void)inmate;
}
} // namespace

int main() {
    setLogStderrEnabled(false);
    levels();
    decay();
    lockdowns();
    saving();
    validation();
    access();
    return yk::test::finish("security");
}
