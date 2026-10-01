// The world clock and routines: time of day arithmetic, the clock running, pausing, being set and
// skipped, callbacks and rule actions on it; schedule files (blocks, wrapping past midnight,
// weekdays, priority, destinations), the checks made of them, and a ScheduleAgent following one:
// blocks starting and ending, actions, arrival, lateness, excuses and saved state.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/sim/Clock.hpp"
#include "yk/sim/Identity.hpp"
#include "yk/sim/Schedule.hpp"
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

const char *scheduleText = R"({
  "format": "yk.data", "version": 1,
  "schedules": [
    {"id": "inmate", "name": "Inmate routine", "roles": ["inmate"], "blocks": [
      {"id": "wake", "from": "06:00", "to": "07:00", "activity": "Wake up", "destination": "home",
       "behavior": "idle"},
      {"id": "roll", "from": "07:00", "to": "08:00", "activity": "Roll call",
       "destination": {"zone": "lineup"}, "behavior": "stand", "tolerance": 5,
       "requires": {"enter": "zone:lineup", "stay": 30}, "tags": ["mandatory"],
       "onStart": [{"type": "SetVariable", "name": "roll_started", "value": true}],
       "onEnd": [{"type": "SetVariable", "name": "roll_ended", "value": true}]},
      {"id": "lunch", "from": "12:00", "to": "13:00", "activity": "Lunch",
       "destination": "purpose:dining", "behavior": "eat"},
      {"id": "visits", "from": "09:00", "to": "10:00", "activity": "Visiting hour", "days": [5, 6],
       "behavior": "visit"},
      {"id": "night", "from": "22:00", "to": "06:00", "activity": "Lights out",
       "destination": {"point": [2, 3]}, "behavior": "sleep"}]},
    {"id": "guard", "roles": ["guard"], "blocks": [
      {"id": "patrol", "from": "00:00", "to": "00:00", "activity": "Patrol", "behavior": "patrol"}]},
    {"id": "tangled", "blocks": [
      {"id": "a", "from": "08:00", "to": "10:00"},
      {"id": "b", "from": "09:00", "to": "11:00"},
      {"id": "c", "from": "09:30", "to": "10:30", "priority": 2}]}
  ]
})";

struct Rig {
    ComponentRegistry registry;
    MemoryAssets assets;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<GameRuntime> runtime;
    std::vector<GameEvent> heard;

    // A clock that turns a minute per tick (60 minutes per second).
    explicit Rig(float scale = 60.0F, const char *startTime = "06:00", int startDay = 1,
                 bool paused = false, bool withClockEntity = true) {
        registerEngineComponents(registry);
        assets.files["data/world.ykdata"] = scheduleText;
        scene = std::make_unique<Scene>(registry, 13);
        if (withClockEntity) {
            auto &settings = scene->createEntity("Clock").add<ClockSettings>();
            settings.minutesPerSecond = scale;
            settings.startTime = startTime;
            settings.startDay = startDay;
            settings.startPaused = paused;
        }
    }
    Entity &person(const char *name, const char *role) {
        Entity &entity = scene->createEntity(name);
        auto &who = entity.add<Identity>();
        who.id = std::string("npc.") + name;
        who.role = role;
        entity.add<ScheduleAgent>();
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
    // Runs until the clock reads `time` (at most a day).
    void until(const char *time, int limit = 1500) {
        const int wanted = parseTimeOfDay(time).value();
        for (int i = 0; i < limit && clock().minutesOfDay() != wanted; ++i)
            step();
    }
    WorldClock &clock() {
        return runtime->services().get<WorldClock>();
    }
    ScheduleAgent &agent(const char *name) {
        return *runtime->scene().findByName(name)->get<ScheduleAgent>();
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

// ---- Times of day
// -----------------------------------------------------------------------------------
void times() {
    CHECK(parseTimeOfDay("06:30") == 390 && parseTimeOfDay("6:30") == 390 &&
          parseTimeOfDay("0:00") == 0 && parseTimeOfDay("23:59") == 1439 &&
          parseTimeOfDay("6") == 360 && parseTimeOfDay("12") == 720);
    for (const char *bad : {"", "24:00", "12:60", "ab:cd", "6:3x", "6:", ":30", "123:00", "1:2:3",
                            "06:300", "-1:00", "6 :30"})
        CHECK(!parseTimeOfDay(bad));
    CHECK(formatTimeOfDay(390) == "06:30" && formatTimeOfDay(0) == "00:00" &&
          formatTimeOfDay(1439) == "23:59" && formatTimeOfDay(1440) == "00:00" &&
          formatTimeOfDay(-30) == "23:30");
    CHECK(timeInRange(100, 60, 200) && !timeInRange(200, 60, 200) && timeInRange(60, 60, 200));
    CHECK(timeInRange(23 * 60 + 30, 22 * 60, 6 * 60) && timeInRange(2 * 60, 22 * 60, 6 * 60) &&
          !timeInRange(12 * 60, 22 * 60, 6 * 60) && !timeInRange(6 * 60, 22 * 60, 6 * 60) &&
          timeInRange(22 * 60, 22 * 60, 6 * 60));
    CHECK(timeInRange(0, 5, 5) && timeInRange(1000, 5, 5)); // from == to: the whole day.
}

// ---- The clock running
// ----------------------------------------------------------------------------
void running() {
    Rig rig;
    rig.start();
    WorldClock &clock = rig.clock();
    // One tick turned over the first minute after 06:00.
    CHECK(clock.day() == 1 && clock.hour() == 6 && clock.minute() == 1 && clock.weekday() == 0 &&
          clock.time() == "06:01" && clock.daylight() && !clock.paused() && clock.scale() == 60.0);
    rig.step(59);
    CHECK(clock.time() == "07:00");
    CHECK(rig.count("clock.minute") == 60 && rig.count("clock.hour") == 1);
    const GameEvent *hour = rig.last("clock.hour");
    CHECK(hour && hour->data.get("hour").asInt() == 7 &&
          hour->data.get("time").asString() == "07:00" && hour->data.get("day").asInt() == 1);
    const GameEvent *minute = rig.last("clock.minute");
    CHECK(minute && minute->data.get("minute").asInt() == 0 && !minute->data.contains("skipped"));
    // Dusk, midnight and the new day.
    rig.until("20:00");
    CHECK(rig.count("clock.daylight") == 1 &&
          !rig.last("clock.daylight")->data.get("daylight").asBool(true) && !clock.daylight());
    rig.until("00:00");
    CHECK(clock.day() == 2 && clock.weekday() == 1 && rig.count("clock.day") == 1 &&
          rig.last("clock.day")->data.get("day").asInt() == 2);
    rig.until("06:00");
    CHECK(rig.count("clock.daylight") == 2 &&
          rig.last("clock.daylight")->data.get("daylight").asBool(false));
    CHECK(clock.totalMinutes() >= 2 * 1440 - 1440 + 360);
    CHECK(clock.inRange(5 * 60, 7 * 60) && !clock.inRange(7 * 60, 9 * 60) &&
          clock.inRange(22 * 60, 7 * 60));
    // When will it next be...: 24 hours when it reads that now.
    CHECK(clock.minutesUntil(7 * 60) == 60 && clock.minutesUntil(6 * 60) == 1440 &&
          clock.minutesUntil(5 * 60) == 23 * 60);

    // A slow clock turns minutes slowly; scale 0 stands still.
    Rig slow(1.0F);
    slow.start();
    slow.step(120); // Two real seconds: two minutes.
    CHECK(slow.clock().minutesOfDay() >= 6 * 60 + 2 && slow.clock().minutesOfDay() <= 6 * 60 + 3);
    slow.clock().setScale(0.0);
    const int stopped = slow.clock().minutesOfDay();
    slow.step(300);
    CHECK(slow.clock().minutesOfDay() == stopped);
    slow.clock().setScale(-5.0);
    CHECK(slow.clock().scale() == 0.0);
    slow.clock().setScale(std::nan(""));
    CHECK(slow.clock().scale() == 0.0);
    // No settings at all: day 1, 06:00, a minute per second.
    Rig defaults(60.0F, "06:00", 1, false, false);
    defaults.scene->createEntity("Anything");
    defaults.start();
    CHECK(defaults.clock().day() == 1 && defaults.clock().scale() == 1.0 &&
          defaults.clock().minutesOfDay() == 6 * 60);
    // Another day, another time to start.
    Rig late(60.0F, "23:58", 3);
    late.start();
    CHECK(late.clock().day() == 3 && late.clock().weekday() == 2 && late.clock().time() == "23:59");
    late.step(2);
    CHECK(late.clock().day() == 4 && late.clock().time() == "00:01");
}

void pausing() {
    Rig rig(60.0F, "06:00", 1, true);
    rig.start();
    CHECK(rig.clock().paused() && rig.clock().time() == "06:00");
    rig.step(30);
    CHECK(rig.clock().time() == "06:00" && rig.count("clock.minute") == 0);
    // Holds by name: it runs when none is left.
    rig.clock().pause(*rig.runtime, "start", false);
    CHECK(!rig.clock().paused() && rig.count("clock.resumed") == 0);
    rig.step(); // The event is delivered.
    CHECK(rig.count("clock.resumed") == 1 && rig.clock().time() == "06:01");
    rig.clock().pause(*rig.runtime, "menu", true);
    rig.clock().pause(*rig.runtime, "cutscene", true);
    rig.clock().pause(*rig.runtime, "cutscene", true); // Again: still one hold.
    rig.step(10);
    CHECK(rig.clock().paused() && rig.clock().time() == "06:01" && rig.count("clock.paused") == 1);
    rig.clock().pause(*rig.runtime, "menu", false);
    CHECK(rig.clock().paused());
    rig.clock().pause(*rig.runtime, "cutscene", false);
    rig.clock().pause(*rig.runtime, "cutscene", false); // Nothing to release.
    CHECK(!rig.clock().paused());
    rig.step(3);
    CHECK(rig.clock().time() == "06:04" && rig.count("clock.resumed") == 2);
}

void jumping() {
    Rig rig;
    rig.start();
    WorldClock &clock = rig.clock();
    const int before = rig.count("clock.minute");
    // Setting the clock forward turns every minute on the way, flagged as skipped.
    clock.set(*rig.runtime, 1, 8 * 60);
    rig.step();
    CHECK(clock.time() == "08:01"); // And then a tick of running.
    CHECK(rig.count("clock.minute") - before == 119 + 1);
    CHECK(rig.count("clock.hour") == 2 && rig.count("clock.set") == 1);
    int skipped = 0;
    for (const GameEvent &event : rig.heard)
        if (event.name == "clock.minute" && event.data.get("skipped").asBool(false))
            ++skipped;
    CHECK(skipped == 119);
    CHECK(rig.last("clock.set")->data.get("time").asString() == "08:00" &&
          rig.last("clock.set")->data.get("minutes").asNumber() > 118.0);
    // Skipping on: sleeping to the morning.
    clock.skipTo(*rig.runtime, 6 * 60);
    CHECK(clock.day() == 2 && clock.time() == "06:00");
    rig.step();
    CHECK(rig.count("clock.skipped") == 1 &&
          rig.last("clock.skipped")->data.get("minutes").asNumber() == 22 * 60 - 1);
    CHECK(rig.count("clock.day") == 1 && rig.count("clock.daylight") == 2); // Dusk and dawn passed.
    clock.skip(*rig.runtime, 30);
    clock.skip(*rig.runtime, -5); // Not backwards, not nothing.
    clock.skip(*rig.runtime, 0);
    CHECK(clock.time() == "06:31");
    rig.step(); // Delivers what the skips announced.
    // Backwards is allowed for set(): no minute events, but the world is told.
    const int minutes = rig.count("clock.minute");
    clock.set(*rig.runtime, 1, 7 * 60);
    CHECK(clock.day() == 1 && clock.time() == "07:00");
    rig.step();
    CHECK(rig.count("clock.minute") - minutes == 1 && clock.minuteCounter() > 0);
    // Absurd jumps are bounded.
    clock.skip(*rig.runtime, 1.0e9);
    CHECK(clock.day() <= 12 && clock.day() >= 10);
}

void callbacks() {
    Rig rig;
    rig.start();
    WorldClock &clock = rig.clock();
    std::vector<std::string> log;
    clock.at(6 * 60 + 5, [&](GameContext &) { log.push_back("once"); });
    clock.at(6 * 60 + 10, [&](GameContext &) { log.push_back("daily"); }, true);
    clock.after(7.0, [&](GameContext &) { log.push_back("after"); });
    const auto cancelled = clock.at(6 * 60 + 6, [&](GameContext &) { log.push_back("never"); });
    clock.cancel(cancelled);
    clock.cancel(9999); // Nothing to cancel.
    rig.step(20);
    CHECK((log == std::vector<std::string>{"once", "after", "daily"}));
    // The daily one is back tomorrow, the others are gone.
    log.clear();
    rig.until("06:09");
    CHECK(log.empty());
    rig.step(2);
    CHECK((log == std::vector<std::string>{"daily"}));
    // A callback may schedule more.
    log.clear();
    clock.after(2.0, [&](GameContext &) {
        log.push_back("first");
        rig.clock().after(2.0, [&](GameContext &) { log.push_back("second"); });
    });
    rig.step(6);
    CHECK((log == std::vector<std::string>{"first", "second"}));
    // A time that is "now" means tomorrow.
    log.clear();
    clock.at(clock.minutesOfDay(), [&](GameContext &) { log.push_back("tomorrow"); });
    rig.step(30);
    CHECK(log.empty());
}

void saving() {
    Rig rig;
    rig.start();
    rig.step(100);
    WorldClock &clock = rig.clock();
    clock.pause(*rig.runtime, "menu", true);
    clock.setScale(12.0);
    CHECK(clock.saveKey() == "clock");
    const Json state = clock.saveState();
    clock.pause(*rig.runtime, "menu", false);
    clock.set(*rig.runtime, 5, 100);
    clock.setScale(1.0);
    CHECK(clock.loadState(*rig.runtime, state));
    CHECK(clock.time() == formatTimeOfDay(static_cast<int>(state.get("total").asNumber()) % 1440) &&
          clock.paused() && clock.scale() == 12.0 && clock.day() == 1);
    Json broken = Json::object();
    broken.set("total", "soon");
    CHECK(!clock.loadState(*rig.runtime, broken));
    broken.set("total", -4);
    CHECK(!clock.loadState(*rig.runtime, broken));
    std::vector<std::pair<std::string, std::string>> rows;
    clock.describe(rows);
    CHECK(rows.size() == 4 && rows[1].second.find("(paused)") != std::string::npos);
}

void clockRules() {
    Rig rig;
    Entity &panel = rig.scene->createEntity("Panel");
    CHECK(panel.add<RuleSet>().setRulesJson(J(R"([
      {"id": "at_seven", "when": "clock.minute", "data": {"time": "07:00"},
       "then": {"type": "SetVariable", "name": "seven", "value": true}},
      {"id": "night", "when": "ask", "if": {"type": "TimeBetween", "from": "22:00", "to": "06:00"},
       "then": {"type": "SetVariable", "name": "night", "value": true},
       "else": {"type": "SetVariable", "name": "night", "value": false}},
      {"id": "light", "when": "ask", "if": {"type": "IsDaylight"},
       "then": {"type": "SetVariable", "name": "light", "value": true},
       "else": {"type": "SetVariable", "name": "light", "value": false}},
      {"id": "late", "when": "ask", "if": {"fact": "clock.hour", "op": ">=", "value": 18},
       "then": {"type": "SetVariable", "name": "evening", "value": true}},
      {"id": "facts", "when": "ask",
       "then": [{"type": "SetVariable", "name": "f_hour", "value": "$clock.hour"},
                {"type": "SetVariable", "name": "f_minute", "value": "$clock.minute"},
                {"type": "SetVariable", "name": "f_day", "value": "$clock.day"},
                {"type": "SetVariable", "name": "f_weekday", "value": "$clock.weekday"},
                {"type": "SetVariable", "name": "f_minutes", "value": "$clock.minutes"},
                {"type": "SetVariable", "name": "f_time", "value": "$clock.time"},
                {"type": "SetVariable", "name": "f_total", "value": "$clock.total"},
                {"type": "SetVariable", "name": "f_paused", "value": "$clock.paused"},
                {"type": "SetVariable", "name": "f_scale", "value": "$clock.scale"},
                {"type": "SetVariable", "name": "f_daylight", "value": "$clock.daylight"}]},
      {"id": "set", "when": "set", "then": {"type": "SetTime", "time": "21:30", "day": 3}},
      {"id": "set_bad", "when": "set_bad", "then": {"type": "SetTime", "time": "later"}},
      {"id": "skip", "when": "skip", "then": {"type": "SkipTime", "minutes": 45}},
      {"id": "until", "when": "until", "then": {"type": "SkipTime", "until": "06:00"}},
      {"id": "skip_bad", "when": "skip_bad", "then": {"type": "SkipTime", "minutes": 0}},
      {"id": "pause", "when": "pause", "then": {"type": "PauseClock", "reason": "conversation"}},
      {"id": "resume", "when": "resume", "then": {"type": "ResumeClock", "reason": "conversation"}},
      {"id": "pause_default", "when": "pause_default", "then": {"type": "PauseClock"}},
      {"id": "resume_default", "when": "resume_default", "then": {"type": "ResumeClock"}},
      {"id": "scale", "when": "scale", "then": {"type": "SetClockScale", "scale": 120}}
    ])")));
    const EntityId panelId = panel.id();
    rig.start();
    GameContext &game = *rig.runtime;
    const auto fire = [&](const char *name) {
        game.events().emit(GameEvent(name, panelId));
        rig.step();
    };
    rig.step(60); // 07:00: the rule that waits for it ran.
    CHECK(game.blackboard().flag("seven"));
    fire("ask");
    CHECK(game.blackboard().has("night") && !game.blackboard().flag("night") &&
          game.blackboard().flag("light") && !game.blackboard().flag("evening"));
    CHECK(game.blackboard().number("f_hour") == 7.0 && game.blackboard().number("f_day") == 1.0 &&
          game.blackboard().number("f_weekday") == 0.0 &&
          game.blackboard().text("f_time").size() == 5 &&
          game.blackboard().number("f_minutes") >= 7 * 60 && game.blackboard().flag("f_daylight") &&
          !game.blackboard().flag("f_paused") && game.blackboard().number("f_scale") == 60.0 &&
          game.blackboard().number("f_total") >= 7 * 60);
    fire("set");
    CHECK(rig.clock().day() == 3 && rig.clock().hour() == 21 && rig.clock().weekday() == 2);
    fire("ask");
    CHECK(game.blackboard().flag("evening") && !game.blackboard().flag("light") &&
          !game.blackboard().flag("night")); // 21:31: dark, but not yet the curfew.
    rig.until("22:00");
    fire("ask");
    CHECK(game.blackboard().flag("night") && !game.blackboard().flag("light"));
    fire("set_bad"); // Not a time: nothing happens.
    CHECK(rig.clock().day() == 3);
    const double total = rig.clock().totalMinutes();
    fire("skip");
    CHECK(rig.clock().totalMinutes() > total + 44.0);
    fire("until");
    CHECK(rig.clock().day() == 4 && rig.clock().hour() == 6);
    const double before = rig.clock().totalMinutes();
    fire("skip_bad");
    CHECK(rig.clock().totalMinutes() < before + 2.0);
    fire("pause");
    CHECK(rig.clock().paused());
    fire("pause_default");
    fire("resume");
    CHECK(rig.clock().paused()); // The default reason still holds it.
    fire("resume_default");
    CHECK(!rig.clock().paused());
    fire("scale");
    CHECK(rig.clock().scale() == 120.0);

    // The validator checks times in rules.
    GameData data;
    std::vector<DataProblem> problems;
    data.add(J(scheduleText), "d.ykdata", problems);
    const RuleCatalog &catalog = *rig.registry.extension<RuleCatalog>();
    std::vector<DataProblem> found;
    const auto check = [&](const char *text) {
        auto action = Action::fromJson(J(text));
        std::vector<Action> list;
        if (action)
            list.push_back(action.value());
        data.checkRules(catalog, RuleSource{"x.ykdata", "t", nullptr, &list}, found);
    };
    check(R"({"type": "SetTime", "time": "25:00"})");
    check(R"({"type": "SkipTime", "until": "soon"})");
    check(R"({"type": "SkipTime"})");
    check(R"({"type": "SetTime", "time": "07:00"})");
    CHECK(found.size() == 3 && has(found[0].message, "'time' must be a time like 06:30") &&
          has(found[1].message, "'until' must be a time") &&
          has(found[2].message, "needs 'minutes' or 'until'"));
    found.clear();
    auto condition = Condition::fromJson(J(R"({"type": "TimeBetween", "from": "9", "to": "x"})"));
    CHECK(condition);
    data.checkRules(catalog, RuleSource{"x.ykdata", "t", &condition.value(), nullptr}, found);
    CHECK(found.size() == 1 && has(found[0].message, "'to' must be a time"));
}

void settingsChecks() {
    Rig rig;
    rig.start();
    const ComponentType *type = rig.registry.find("ClockSettings");
    CHECK(type && type->check);
    if (!type || !type->check)
        return;
    ClockSettings settings;
    std::vector<std::string> problems;
    type->check(*rig.runtime->scene().findByName("Clock"), settings, CheckContext{}, problems);
    CHECK(problems.empty());
    settings.startTime = "never";
    settings.nightStarts = "25:61";
    settings.minutesPerSecond = -1.0F;
    type->check(*rig.runtime->scene().findByName("Clock"), settings, CheckContext{}, problems);
    CHECK(problems.size() == 3 && has(problems[0], "'startTime' must be a time") &&
          has(problems[1], "'nightStarts' must be a time") &&
          has(problems[2], "cannot be negative"));
}

// ---- Schedule files
// ---------------------------------------------------------------------------------
void definitions() {
    std::vector<DataProblem> problems;
    MemoryAssets assets;
    assets.files["data/world.ykdata"] = scheduleText;
    GameData data = GameData::load(assets, problems);
    data.check(nullptr, problems);
    // The only thing said is about the blocks that overlap with nothing to choose between them.
    CHECK(problems.size() == 1 && !problems[0].error &&
          has(problems[0].message, "'a' and 'b' overlap at 09:00"));
    const ScheduleCatalog &catalog = data.schedules;
    CHECK(catalog.schedules.size() == 3 && data.known("schedule", "inmate") &&
          !data.known("schedule", "nope"));
    const ScheduleDefinition &inmate = *catalog.schedules.find("inmate");
    CHECK(inmate.blocks.size() == 5 && inmate.name == "Inmate routine" &&
          catalog.forRole("inmate") == &inmate &&
          catalog.forRole("guard") == catalog.schedules.find("guard") &&
          !catalog.forRole("medic") && !catalog.forRole(""));
    const ScheduleBlock &rollCall = inmate.blocks[1];
    CHECK(rollCall.from == 420 && rollCall.to == 480 && rollCall.activity == "Roll call" &&
          rollCall.behavior == "stand" && rollCall.tolerance == 5.0 &&
          rollCall.destination.kind == ScheduleDestination::Kind::Zone &&
          rollCall.destination.name == "lineup" && rollCall.requirement &&
          rollCall.requirement->enter == "zone:lineup" && rollCall.requirement->stay == 30.0 &&
          rollCall.onStart.size() == 1 && rollCall.onEnd.size() == 1 && rollCall.tags.size() == 1);
    CHECK(inmate.blocks[0].destination.kind == ScheduleDestination::Kind::Home &&
          inmate.blocks[2].destination.kind == ScheduleDestination::Kind::Purpose &&
          inmate.blocks[2].destination.text() == "purpose:dining" &&
          inmate.blocks[4].destination.kind == ScheduleDestination::Kind::Point &&
          inmate.blocks[4].destination.point == (Vec2{2.0F, 3.0F}) &&
          inmate.blocks[3].days.size() == 2);
    CHECK(inmate.blocks[0].behavior == "idle" && inmate.blocks[3].behavior == "visit" &&
          inmate.blocks[3].destination.kind == ScheduleDestination::Kind::None &&
          inmate.blocks[0].tolerance == 5.0);
    // Writing it back and reading it again gives the same schedule.
    std::vector<std::string> warnings;
    auto again = ScheduleDefinition::fromJson(inmate.toJson(), warnings);
    CHECK(again && again.value().toJson() == inmate.toJson() && warnings.empty());

    // What is on when: wrapping past midnight, weekdays, priority.
    CHECK(inmate.blockAt(6 * 60, 0)->id == "wake" && inmate.blockAt(7 * 60 + 30, 3)->id == "roll" &&
          !inmate.blockAt(8 * 60, 0) && inmate.blockAt(12 * 60 + 59, 0)->id == "lunch" &&
          !inmate.blockAt(13 * 60, 0));
    CHECK(inmate.blockAt(23 * 60, 0)->id == "night" && inmate.blockAt(2 * 60, 0)->id == "night" &&
          inmate.blockAt(5 * 60 + 59, 6)->id == "night" &&
          inmate.blockAt(22 * 60, 4)->id == "night");
    CHECK(!inmate.blockAt(9 * 60 + 30, 0) && inmate.blockAt(9 * 60 + 30, 5)->id == "visits" &&
          inmate.blockAt(9 * 60 + 30, 6)->id == "visits" && !inmate.blockAt(9 * 60 + 30, 4));
    const ScheduleDefinition &tangled = *catalog.schedules.find("tangled");
    CHECK(tangled.blockAt(8 * 60 + 30, 0)->id == "a" &&
          tangled.blockAt(9 * 60 + 15, 0)->id == "b" && // The later one.
          tangled.blockAt(9 * 60 + 45, 0)->id == "c" &&
          tangled.blockAt(10 * 60 + 15, 0)->id == "c" &&
          tangled.blockAt(10 * 60 + 45, 0)->id == "b" && !tangled.blockAt(11 * 60, 0));
    CHECK(catalog.schedules.find("guard")->blockAt(12345 % 1440, 3)->id == "patrol"); // All day.
    // What starts next.
    int until = 0;
    CHECK(inmate.next(6 * 60 + 30, 0, &until)->id == "roll" && until == 30);
    CHECK(inmate.next(8 * 60, 0, &until)->id == "lunch" && until == 4 * 60);
    CHECK(inmate.next(8 * 60 + 30, 5, &until)->id == "visits" && until == 30);
    CHECK(inmate.next(10 * 60, 4, &until)->id == "lunch" && until == 120);
    CHECK(inmate.next(13 * 60, 0, &until)->id == "night" && until == 9 * 60);
    CHECK(inmate.next(23 * 60, 0, &until)->id == "wake" && until == 7 * 60);
    CHECK(inmate.next(9 * 60 + 30, 4, &until)->id == "lunch" && until == 150);
    ScheduleDefinition empty;
    CHECK(!empty.next(0, 0, &until) && until == -1 && !empty.blockAt(0, 0));
    // Blocks and their lengths.
    CHECK(inmate.blocks[4].length() == 8 * 60 && rollCall.length() == 60 &&
          catalog.schedules.find("guard")->blocks[0].length() == 1440);

    // A file with a single schedule is that schedule.
    MemoryAssets single;
    single.files["data/one.ykschedule"] =
        R"({"id": "solo", "blocks": [{"id": "all", "from": "00:00", "to": "12:00", "activity": "Morning"}]})";
    problems.clear();
    GameData solo = GameData::load(single, problems);
    CHECK(problems.empty() && solo.schedules.schedules.size() == 1 &&
          solo.schedules.schedules.find("solo")->blocks[0].to == 720);
    const auto rows = solo.summary();
    CHECK(std::any_of(rows.begin(), rows.end(), [](const auto &row) {
        return row.first == "Schedules" && row.second == "1";
    }));

    // What is wrong is said.
    const auto error = [&](const char *text) {
        auto parsed = ScheduleDefinition::fromJson(J(text), warnings);
        return parsed ? std::string() : parsed.error();
    };
    CHECK(has(error("[]"), "must be an object"));
    CHECK(has(error(R"({"id": "x b"})"), "not a usable id"));
    CHECK(has(error(R"({"id": "x"})"), "'blocks' must be a list with at least one block"));
    CHECK(has(error(R"({"id": "x", "blocks": [3]})"), "block 1: must be an object"));
    CHECK(has(error(R"({"id": "x", "blocks": [{"from": "1:00"}]})"), "block 1: 'id' is needed"));
    CHECK(has(error(R"({"id": "x", "blocks": [{"id": "a", "to": "1:00"}]})"),
              "block 'a': 'from' is needed"));
    CHECK(has(error(R"({"id": "x", "blocks": [{"id": "a", "from": "1:00", "to": "late"}]})"),
              "'to' is not a time"));
    CHECK(has(
        error(
            R"({"id": "x", "blocks": [{"id": "a", "from": "1:00", "to": "2:00"},{"id": "a", "from": "3:00", "to": "4:00"}]})"),
        "two blocks are called 'a'"));
    CHECK(has(
        error(
            R"({"id": "x", "blocks": [{"id": "a", "from": "1:00", "to": "2:00", "destination": 5}]})"),
        "destination: a destination is a string or an object"));
    CHECK(has(
        error(
            R"({"id": "x", "blocks": [{"id": "a", "from": "1:00", "to": "2:00", "destination": "somewhere"}]})"),
        "destination: a destination is \"home\""));
    CHECK(has(
        error(
            R"({"id": "x", "blocks": [{"id": "a", "from": "1:00", "to": "2:00", "destination": "moon:base"}]})"),
        "'moon' is not a kind of destination"));
    CHECK(has(
        error(
            R"({"id": "x", "blocks": [{"id": "a", "from": "1:00", "to": "2:00", "destination": {"zone": "a", "room": "b"}}]})"),
        "exactly one of"));
    CHECK(has(
        error(
            R"({"id": "x", "blocks": [{"id": "a", "from": "1:00", "to": "2:00", "destination": {"point": [1]}}]})"),
        "'point' must be [x, y]"));
    CHECK(has(
        error(
            R"({"id": "x", "blocks": [{"id": "a", "from": "1:00", "to": "2:00", "destination": {"zone": ""}}]})"),
        "needs the name of a zone"));
    CHECK(has(
        error(R"({"id": "x", "blocks": [{"id": "a", "from": "1:00", "to": "2:00", "days": [7]}]})"),
        "weekday numbers (0-6)"));
    CHECK(has(
        error(R"({"id": "x", "blocks": [{"id": "a", "from": "1:00", "to": "2:00", "days": 3}]})"),
        "weekday numbers (0-6)"));
    CHECK(has(
        error(
            R"({"id": "x", "blocks": [{"id": "a", "from": "1:00", "to": "2:00", "tolerance": -1}]})"),
        "'tolerance' must be a number"));
    CHECK(has(
        error(
            R"({"id": "x", "blocks": [{"id": "a", "from": "1:00", "to": "2:00", "requires": {}}]})"),
        "'requires' must be an object"));
    CHECK(has(
        error(
            R"({"id": "x", "blocks": [{"id": "a", "from": "1:00", "to": "2:00", "requires": {"enter": "home"}}]})"),
        "'enter' must be \"zone:<id>\""));
    CHECK(has(
        error(
            R"({"id": "x", "blocks": [{"id": "a", "from": "1:00", "to": "2:00", "onStart": [{"nope": 1}]}]})"),
        "onStart: "));
    CHECK(has(
        error(R"({"id": "x", "blocks": [{"id": "a", "from": "1:00", "to": "2:00", "data": 3}]})"),
        "'data' must be an object"));
    warnings.clear();
    CHECK(ScheduleDefinition::fromJson(J(R"({"id": "x", "extra": 1, "blocks": [
        {"id": "a", "from": 6, "to": 7.0, "actvity": "typo", "requires": {"enter": "zone:z", "oops": 1}}]})"),
                                       warnings));
    CHECK(warnings.size() == 3 && has(warnings[0], "unknown field 'oops'") &&
          has(warnings[1], "block 'a': unknown field 'actvity'") &&
          has(warnings[2], "unknown field 'extra'"));
}

// ---- A character following one
// ---------------------------------------------------------------------------------
void following() {
    Rig rig;
    rig.person("Dave", "inmate");
    rig.person("Guard", "guard");
    Entity &stranger = rig.scene->createEntity("Stranger");
    stranger.add<Identity>().role = "medic";
    stranger.add<ScheduleAgent>(); // No schedule for a medic.
    Entity &named = rig.scene->createEntity("Named");
    named.add<ScheduleAgent>().schedule = "guard"; // Names one; needs no role.
    rig.start();
    ScheduleAgent &dave = rig.agent("Dave");
    // By role, and by name; the stranger has none.
    CHECK(dave.definition() && dave.definition()->id == "inmate" &&
          rig.agent("Guard").definition()->id == "guard" &&
          rig.agent("Named").definition()->id == "guard" && !rig.agent("Stranger").definition() &&
          !rig.agent("Stranger").current());
    // At 06:01 it is in "wake": started on the first look.
    CHECK(dave.current() && dave.current()->id == "wake" && dave.minutesInto() == 1 &&
          dave.minutesLeft() == 59 && dave.upcoming()->id == "roll" &&
          dave.minutesUntilNext() == 59 && !dave.arrived() && !dave.late());
    CHECK(rig.count("schedule.block_started") >= 1);
    const GameEvent *started = nullptr;
    for (const GameEvent &event : rig.heard)
        if (event.name == "schedule.block_started" && event.source == dave.entity().id())
            started = &event;
    CHECK(started && started->data.get("block").asString() == "wake" &&
          started->data.get("destination").asString() == "home" &&
          started->data.get("from").asString() == "06:00" &&
          started->data.get("to").asString() == "07:00" &&
          started->data.get("schedule").asString() == "inmate" &&
          started->data.get("behavior").asString() == "idle");
    // The agent did not get there in time for the first block of the game: it is late at 06:06.
    rig.step(4);
    CHECK(!dave.late());
    rig.step(1); // 06:06
    CHECK(dave.late());
    int lates = 0;
    for (const GameEvent &event : rig.heard)
        if (event.name == "schedule.late" && event.source == dave.entity().id())
            ++lates;
    CHECK(lates == 1 && rig.last("schedule.late")->data.get("minutesLate").asNumber() == 1.0);
    rig.step(10);
    int again = 0;
    for (const GameEvent &event : rig.heard)
        again += event.name == "schedule.late" && event.source == dave.entity().id() ? 1 : 0;
    CHECK(again == 1); // Said once per block.

    // 07:00: wake ends, roll call starts and its actions run.
    rig.until("07:00");
    rig.step();
    CHECK(dave.current()->id == "roll" && rig.runtime->blackboard().flag("roll_started") &&
          !rig.runtime->blackboard().flag("roll_ended"));
    const GameEvent *ended = rig.last("schedule.block_ended");
    CHECK(ended && ended->data.get("block").asString() == "wake" &&
          ended->source == dave.entity().id());
    CHECK(dave.minutesLeft() < 60 && dave.upcoming()->id == "lunch");
    // Arriving in time: no lateness.
    dave.reportArrived(*rig.runtime);
    dave.reportArrived(*rig.runtime); // Once.
    rig.step(8);
    CHECK(dave.arrived() && !dave.late());
    int arrivals = 0;
    for (const GameEvent &event : rig.heard)
        arrivals += event.name == "schedule.arrived" && event.source == dave.entity().id() ? 1 : 0;
    CHECK(arrivals == 1 && rig.last("schedule.arrived")->data.get("block").asString() == "roll");
    // 08:00: a gap in the routine.
    rig.until("08:00");
    rig.step();
    CHECK(!dave.current() && rig.runtime->blackboard().flag("roll_ended") &&
          dave.minutesLeft() == -1 && dave.minutesInto() == -1 && dave.upcoming()->id == "lunch" &&
          dave.minutesUntilNext() > 230);
    // Lunch: purpose dining; excused, a character is not late; when excuses end it can be.
    rig.until("12:00");
    rig.step();
    CHECK(dave.current()->id == "lunch" &&
          dave.current()->destination.kind == ScheduleDestination::Kind::Purpose);
    dave.excuse(*rig.runtime, "alarm", true);
    dave.excuse(*rig.runtime, "alarm", true);
    CHECK(dave.excused());
    rig.step(20);
    CHECK(!dave.late());
    dave.excuse(*rig.runtime, "alarm", false);
    dave.excuse(*rig.runtime, "alarm", false);
    CHECK(!dave.excused());
    rig.step(2);
    CHECK(dave.late());
    // A block without a destination has nowhere to be late for; weekdays pick blocks.
    rig.until("21:59");
    rig.step(2);
    CHECK(dave.current()->id == "night" && !dave.arrived());
    CHECK(rig.agent("Guard").current()->id == "patrol" &&
          rig.agent("Guard").current()->destination.kind == ScheduleDestination::Kind::None &&
          rig.agent("Guard").arrived() && !rig.agent("Guard").late());
    // The night block wraps past midnight and is still on at 03:00 of the next day.
    rig.until("03:00");
    CHECK(dave.current()->id == "night" && rig.clock().day() == 2);
    rig.until("06:00");
    rig.step();
    CHECK(dave.current()->id == "wake" && rig.clock().day() == 2 && !dave.arrived() &&
          !dave.late());
    // Setting the clock changes the block at once.
    rig.clock().set(*rig.runtime, 6, 9 * 60 + 30); // Day 6 is weekday 5: visiting hour.
    rig.step();
    CHECK(dave.current() && dave.current()->id == "visits" && dave.current()->behavior == "visit");
    rig.clock().set(*rig.runtime, 2, 9 * 60 + 30);
    rig.step();
    CHECK(!dave.current());
}

void facts() {
    Rig rig;
    Entity &dave = rig.person("Dave", "inmate");
    CHECK(dave.add<RuleSet>().setRulesJson(J(R"([
      {"id": "ask", "when": "ask", "then": [
        {"type": "SetVariable", "name": "block", "value": "$self.schedule.block"},
        {"type": "SetVariable", "name": "activity", "value": "$self.schedule.activity"},
        {"type": "SetVariable", "name": "behavior", "value": "$self.schedule.behavior"},
        {"type": "SetVariable", "name": "destination", "value": "$self.schedule.destination"},
        {"type": "SetVariable", "name": "schedule", "value": "$self.schedule.schedule"},
        {"type": "SetVariable", "name": "next", "value": "$self.schedule.next"},
        {"type": "SetVariable", "name": "next_activity", "value": "$self.schedule.nextActivity"},
        {"type": "SetVariable", "name": "left", "value": "$self.schedule.minutesLeft"},
        {"type": "SetVariable", "name": "into", "value": "$self.schedule.minutesInto"},
        {"type": "SetVariable", "name": "until", "value": "$self.schedule.minutesUntilNext"},
        {"type": "SetVariable", "name": "arrived", "value": "$self.schedule.arrived"},
        {"type": "SetVariable", "name": "late", "value": "$self.schedule.late"}]},
      {"id": "is_wake", "when": "ask", "if": {"type": "ScheduleBlockIs", "block": "wake"},
       "then": {"type": "SetVariable", "name": "is_wake", "value": true}},
      {"id": "is_idle", "when": "ask", "if": {"type": "ScheduleBlockIs", "behavior": "idle", "activity": "Wake up"},
       "then": {"type": "SetVariable", "name": "is_idle", "value": true}},
      {"id": "is_mandatory", "when": "ask", "if": {"type": "ScheduleBlockIs", "tag": "mandatory"},
       "then": {"type": "SetVariable", "name": "is_mandatory", "value": true}},
      {"id": "arrive", "when": "arrive", "then": {"type": "ReportArrival"}},
      {"id": "excuse", "when": "excuse", "then": {"type": "ExcuseSchedule", "reason": "fight"}},
      {"id": "release", "when": "release", "then": {"type": "ExcuseSchedule", "reason": "fight", "on": false}}
    ])")));
    rig.scene->createEntity("Plain");
    rig.start();
    GameContext &game = *rig.runtime;
    const EntityId self = rig.runtime->scene().findByName("Dave")->id();
    const auto fire = [&](const char *name) {
        game.events().emit(GameEvent(name, self));
        rig.step();
    };
    fire("ask");
    CHECK(game.blackboard().text("block") == "wake" &&
          game.blackboard().text("activity") == "Wake up" &&
          game.blackboard().text("behavior") == "idle" &&
          game.blackboard().text("destination") == "home" &&
          game.blackboard().text("schedule") == "inmate" &&
          game.blackboard().text("next") == "roll" &&
          game.blackboard().text("next_activity") == "Roll call" &&
          game.blackboard().number("left") == 58.0 && game.blackboard().number("into") == 2.0 &&
          game.blackboard().number("until") == 58.0 && !game.blackboard().flag("arrived") &&
          !game.blackboard().flag("late"));
    CHECK(game.blackboard().flag("is_wake") && game.blackboard().flag("is_idle") &&
          !game.blackboard().flag("is_mandatory"));
    fire("arrive");
    CHECK(rig.agent("Dave").arrived());
    fire("excuse");
    CHECK(rig.agent("Dave").excused());
    fire("release");
    CHECK(!rig.agent("Dave").excused());
    rig.until("07:00");
    fire("ask");
    CHECK(game.blackboard().flag("is_mandatory"));

    // Rules inside blocks are checked against the catalog.
    GameData data;
    std::vector<DataProblem> problems;
    data.add(J(R"({"schedules": [{"id": "s", "blocks": [{"id": "a", "from": "6:00", "to": "7:00",
        "onStart": [{"type": "Explode"}], "onEnd": [{"type": "GiveItem", "item": "unicorn"}]}]}]})"),
             "s.ykdata", problems);
    CHECK(problems.empty());
    data.check(rig.registry.extension<RuleCatalog>(), problems);
    CHECK(problems.size() == 2 &&
          has(problems[0].message, "schedule 's' block 'a' onStart: unknown action 'Explode'") &&
          has(problems[1].message,
              "schedule 's' block 'a' onEnd: action 'GiveItem': there is no item 'unicorn'"));
    // A predicate with nothing to test is pointed out.
    std::vector<DataProblem> found;
    auto vague = Condition::fromJson(J(R"({"type": "ScheduleBlockIs"})"));
    data.checkRules(*rig.registry.extension<RuleCatalog>(),
                    RuleSource{"x", "t", &vague.value(), nullptr}, found);
    CHECK(found.size() == 1 && !found[0].error &&
          has(found[0].message, "says nothing about the block"));
}

void agentSaving() {
    Rig rig;
    rig.person("Dave", "inmate");
    rig.start();
    ScheduleAgent &dave = rig.agent("Dave");
    rig.step(10); // Late for the first block.
    CHECK(dave.late());
    dave.reportArrived(*rig.runtime);
    const Json state = dave.saveState();
    CHECK(state.get("block").asString() == "wake" && state.get("arrived").asBool(false) &&
          state.get("late").asBool(false));
    // Loaded in the same block it keeps them; in another it does not.
    Json fresh = Json::object();
    fresh.set("block", "wake");
    fresh.set("arrived", false);
    fresh.set("late", false);
    CHECK(dave.loadState(*rig.runtime, fresh));
    CHECK(!dave.arrived() && !dave.late());
    Json other = Json::object();
    other.set("block", "lunch");
    other.set("arrived", true);
    other.set("late", true);
    CHECK(dave.loadState(*rig.runtime, other));
    CHECK(!dave.arrived() && !dave.late());
    // A role that changes picks another schedule.
    rig.runtime->scene().findByName("Dave")->get<Identity>()->setRole(*rig.runtime, "guard");
    dave.refresh(*rig.runtime);
    CHECK(dave.definition()->id == "guard" && dave.current()->id == "patrol");
    // A schedule that does not exist is reported and leaves the character without one.
    dave.schedule = "ghosts";
    dave.refresh(*rig.runtime);
    CHECK(!dave.definition() && !dave.current());
    const ComponentType *type = rig.registry.find("ScheduleAgent");
    CHECK(type && type->check);
    if (type && type->check) {
        GameData data;
        std::vector<DataProblem> problems;
        data.add(J(scheduleText), "d.ykdata", problems);
        CheckContext context;
        context.known = [&](std::string_view kind, std::string_view id) {
            return data.known(kind, id);
        };
        std::vector<std::string> found;
        type->check(dave.entity(), dave, context, found);
        CHECK(found.size() == 1 &&
              has(found[0], "follows the schedule 'ghosts', which is not defined"));
        dave.schedule = "guard";
        found.clear();
        type->check(dave.entity(), dave, context, found);
        CHECK(found.empty());
    }
}
} // namespace

int main() {
    setLogStderrEnabled(false);
    times();
    running();
    pausing();
    jumping();
    callbacks();
    saving();
    clockRules();
    settingsChecks();
    definitions();
    following();
    facts();
    agentSaving();
    return yk::test::finish("clock");
}
