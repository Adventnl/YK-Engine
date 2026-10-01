#include "yk/sim/Clock.hpp"
#include "yk/core/Log.hpp"
#include "yk/runtime/GameContext.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
std::optional<int> parseTimeOfDay(std::string_view text) {
    if (text.empty() || text.size() > 5)
        return std::nullopt;
    int hours = 0, minutes = 0;
    std::size_t at = 0;
    const auto digits = [&](int &into, std::size_t most) {
        std::size_t used = 0;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9' && used < most) {
            into = into * 10 + (text[at] - '0');
            ++at;
            ++used;
        }
        return used > 0;
    };
    if (!digits(hours, 2))
        return std::nullopt;
    if (at < text.size()) {
        if (text[at] != ':')
            return std::nullopt;
        ++at;
        if (!digits(minutes, 2) || at != text.size())
            return std::nullopt;
    }
    if (hours > 23 || minutes > 59)
        return std::nullopt;
    return hours * 60 + minutes;
}

std::string formatTimeOfDay(int minutes) {
    minutes = ((minutes % minutesPerDay) + minutesPerDay) % minutesPerDay;
    const int hours = minutes / 60;
    const int rest = minutes % 60;
    return std::string(hours < 10 ? "0" : "") + std::to_string(hours) + ":" +
           (rest < 10 ? "0" : "") + std::to_string(rest);
}

bool timeInRange(int minute, int from, int to) {
    if (from == to)
        return true;
    if (from < to)
        return minute >= from && minute < to;
    return minute >= from || minute < to;
}

// ---- Settings
// ---------------------------------------------------------------------------------------
void ClockSettings::describe(TypeBuilder<ClockSettings> &type) {
    type.category("Simulation")
        .description("Where the world clock starts and how fast it runs. Optional: without one it "
                     "starts on day 1 at 06:00 and runs a minute of the world per second.");
    type.field("minutesPerSecond", &ClockSettings::minutesPerSecond)
        .range(0, 3600, 0.1)
        .tooltip("Minutes of the world per real second (1: a day lasts 24 real minutes).");
    type.field("startDay", &ClockSettings::startDay).range(1, 100000, 1);
    type.field("startTime", &ClockSettings::startTime).tooltip("HH:MM");
    type.field("startPaused", &ClockSettings::startPaused);
    type.field("dayStarts", &ClockSettings::dayStarts).tooltip("HH:MM: when daylight begins.");
    type.field("nightStarts", &ClockSettings::nightStarts).tooltip("HH:MM: when it ends.");
    type.check([](const Entity &, const ClockSettings &settings, const CheckContext &,
                  std::vector<std::string> &problems) {
        for (const auto &[label, text] :
             {std::pair<const char *, const std::string &>{"startTime", settings.startTime},
              {"dayStarts", settings.dayStarts},
              {"nightStarts", settings.nightStarts}})
            if (!parseTimeOfDay(text))
                problems.push_back(std::string("'") + label + "' must be a time like 06:30");
        if (!(settings.minutesPerSecond >= 0.0F))
            problems.push_back("'minutesPerSecond' cannot be negative");
    });
}

void ClockSettings::onStart(GameContext &context) {
    context.services().get<WorldClock>().configure(*this);
}

// ---- The clock
// ------------------------------------------------------------------------------------
void WorldClock::configure(const ClockSettings &settings) {
    scale_ = std::max(0.0, static_cast<double>(settings.minutesPerSecond));
    const int start = parseTimeOfDay(settings.startTime).value_or(6 * 60);
    total_ = static_cast<double>(std::max(settings.startDay, 1) - 1) * minutesPerDay + start;
    dayStart_ = parseTimeOfDay(settings.dayStarts).value_or(6 * 60);
    nightStart_ = parseTimeOfDay(settings.nightStarts).value_or(20 * 60);
    pauses_.clear();
    if (settings.startPaused)
        pauses_.insert("start");
    daylight_ = daylightAt(minutesOfDay());
    ++minuteCounter_;
    configured_ = true;
}

bool WorldClock::daylightAt(int minuteOfDay) const {
    return timeInRange(minuteOfDay, dayStart_, nightStart_);
}
bool WorldClock::daylight() const {
    return daylightAt(minutesOfDay());
}

int WorldClock::minutesUntil(int minuteOfDay) const {
    const int wanted = ((minuteOfDay % minutesPerDay) + minutesPerDay) % minutesPerDay;
    const int distance = (wanted - minutesOfDay() + minutesPerDay) % minutesPerDay;
    return distance == 0 ? minutesPerDay : distance;
}

void WorldClock::setScale(double minutesPerSecond) {
    scale_ = std::isfinite(minutesPerSecond) ? std::max(0.0, minutesPerSecond) : scale_;
}

void WorldClock::pause(GameContext &context, const std::string &reason, bool on) {
    const bool before = paused();
    if (on)
        pauses_.insert(reason);
    else
        pauses_.erase(reason);
    if (before != paused()) {
        Json data = Json::object();
        data.set("time", time());
        context.events().emit(
            GameEvent(paused() ? "clock.paused" : "clock.resumed", {}, {}, std::move(data)));
    }
}

void WorldClock::onFixedUpdate(GameContext &context, float seconds) {
    if (paused() || !(scale_ > 0.0))
        return;
    advanceTo(context, total_ + static_cast<double>(seconds) * scale_, false);
}

void WorldClock::advanceTo(GameContext &context, double newTotal, bool skipped) {
    constexpr std::int64_t most = 10 * minutesPerDay; // One call never turns more than ten days.
    const std::int64_t from = static_cast<std::int64_t>(std::floor(total_));
    std::int64_t to = static_cast<std::int64_t>(std::floor(newTotal));
    if (to - from > most) {
        log(LogLevel::Warning, "clock", "The clock was moved by more than ten days at once.");
        to = from + most;
        newTotal = static_cast<double>(to);
    }
    total_ = newTotal;
    // A long skip announces every minute of its last day (a rule that waits for 06:00 still sees
    // it), but not the thousands before: the hours, days and callbacks all still happen.
    const std::int64_t announceFrom = std::max<std::int64_t>(from + 1, to - minutesPerDay + 1);
    for (std::int64_t minute = from + 1; minute <= to; ++minute)
        turnMinute(context, minute, skipped, minute >= announceFrom);
}

void WorldClock::turnMinute(GameContext &context, std::int64_t minute, bool skipped,
                            bool announceMinute) {
    const int inDay = static_cast<int>(minute % minutesPerDay);
    const int dayNumber = static_cast<int>(minute / minutesPerDay) + 1;
    ++minuteCounter_;
    Json data = Json::object();
    data.set("day", dayNumber);
    data.set("hour", inDay / 60);
    data.set("minute", inDay % 60);
    data.set("time", formatTimeOfDay(inDay));
    data.set("weekday", (dayNumber - 1) % 7);
    if (skipped)
        data.set("skipped", true);
    if (announceMinute)
        context.events().emit(GameEvent("clock.minute", {}, {}, data));
    if (inDay % 60 == 0)
        context.events().emit(GameEvent("clock.hour", {}, {}, data));
    if (inDay == 0)
        context.events().emit(GameEvent("clock.day", {}, {}, data));
    const bool light = daylightAt(inDay);
    if (light != daylight_) {
        daylight_ = light;
        Json change = data;
        change.set("daylight", light);
        context.events().emit(GameEvent("clock.daylight", {}, {}, std::move(change)));
    }
    // Callbacks due now (a callback may add or cancel others, so work from the ids).
    std::vector<std::uint64_t> due;
    for (const Scheduled &item : scheduled_)
        if (item.dueMinute <= minute)
            due.push_back(item.id);
    for (const std::uint64_t id : due) {
        const auto found = std::find_if(scheduled_.begin(), scheduled_.end(),
                                        [&](const Scheduled &item) { return item.id == id; });
        if (found == scheduled_.end())
            continue;
        Callback callback = found->callback;
        if (found->dailyAt >= 0)
            found->dueMinute += minutesPerDay;
        else
            scheduled_.erase(found);
        if (callback)
            callback(context);
    }
}

void WorldClock::set(GameContext &context, int dayNumber, int minuteOfDay) {
    dayNumber = std::max(dayNumber, 1);
    minuteOfDay = ((minuteOfDay % minutesPerDay) + minutesPerDay) % minutesPerDay;
    const double target = static_cast<double>(dayNumber - 1) * minutesPerDay + minuteOfDay;
    const double before = total_;
    if (target > total_) {
        advanceTo(context, target, true);
    } else {
        total_ = target;
        daylight_ = daylightAt(minuteOfDay);
        ++minuteCounter_;
    }
    Json data = Json::object();
    data.set("time", time());
    data.set("day", day());
    data.set("minutes", target - before);
    context.events().emit(GameEvent("clock.set", {}, {}, std::move(data)));
}

void WorldClock::skip(GameContext &context, double minutes) {
    if (!(minutes > 0.0) || !std::isfinite(minutes))
        return;
    advanceTo(context, total_ + minutes, true);
    Json data = Json::object();
    data.set("time", time());
    data.set("day", day());
    data.set("minutes", minutes);
    context.events().emit(GameEvent("clock.skipped", {}, {}, std::move(data)));
}

void WorldClock::skipTo(GameContext &context, int minuteOfDay) {
    skip(context, static_cast<double>(minutesUntil(minuteOfDay)));
}

std::uint64_t WorldClock::at(int minuteOfDay, Callback callback, bool daily) {
    const int wanted = ((minuteOfDay % minutesPerDay) + minutesPerDay) % minutesPerDay;
    const std::int64_t now = static_cast<std::int64_t>(std::floor(total_));
    std::int64_t due = (now / minutesPerDay) * minutesPerDay + wanted;
    if (due <= now)
        due += minutesPerDay;
    Scheduled item;
    item.id = nextId_++;
    item.dueMinute = due;
    item.dailyAt = daily ? wanted : -1;
    item.callback = std::move(callback);
    scheduled_.push_back(std::move(item));
    return scheduled_.back().id;
}

std::uint64_t WorldClock::after(double gameMinutes, Callback callback) {
    const std::int64_t now = static_cast<std::int64_t>(std::floor(total_));
    Scheduled item;
    item.id = nextId_++;
    item.dueMinute =
        now + std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(gameMinutes)));
    item.callback = std::move(callback);
    scheduled_.push_back(std::move(item));
    return scheduled_.back().id;
}

void WorldClock::cancel(std::uint64_t id) {
    scheduled_.erase(std::remove_if(scheduled_.begin(), scheduled_.end(),
                                    [&](const Scheduled &item) { return item.id == id; }),
                     scheduled_.end());
}

Json WorldClock::saveState() const {
    Json state = Json::object();
    state.set("total", total_);
    state.set("scale", scale_);
    Json holds = Json::array();
    for (const std::string &reason : pauses_)
        holds.push(reason);
    state.set("pauses", std::move(holds));
    state.set("dayStarts", dayStart_);
    state.set("nightStarts", nightStart_);
    return state;
}

Status WorldClock::loadState(GameContext &, const Json &state) {
    if (!state.get("total").isNumber() || !(state.get("total").asNumber() >= 0.0) ||
        !std::isfinite(state.get("total").asNumber()))
        return Error{"clock: the saved time is not a number of minutes"};
    total_ = state.get("total").asNumber();
    scale_ = std::max(0.0, state.get("scale").asNumber(scale_));
    pauses_.clear();
    for (const Json &reason : state.get("pauses").items())
        if (reason.isString())
            pauses_.insert(reason.asString());
    dayStart_ = static_cast<int>(state.get("dayStarts").asInt(dayStart_));
    nightStart_ = static_cast<int>(state.get("nightStarts").asInt(nightStart_));
    daylight_ = daylightAt(minutesOfDay());
    ++minuteCounter_;
    return success();
}

void WorldClock::describe(std::vector<std::pair<std::string, std::string>> &rows) const {
    rows.push_back({"Day", std::to_string(day())});
    rows.push_back({"Time", time() + (paused() ? " (paused)" : "")});
    rows.push_back({"Speed", std::to_string(scale_) + " min/s"});
    rows.push_back({"Daylight", daylight() ? "yes" : "no"});
}

// ---- Rules
// ---------------------------------------------------------------------------------------
namespace {
std::optional<Value> clockFact(RuleContext &context, Entity *, std::string_view rest) {
    const WorldClock &clock = context.game.services().get<WorldClock>();
    if (rest == "hour")
        return Value{static_cast<std::int64_t>(clock.hour())};
    if (rest == "minute")
        return Value{static_cast<std::int64_t>(clock.minute())};
    if (rest == "day")
        return Value{static_cast<std::int64_t>(clock.day())};
    if (rest == "weekday")
        return Value{static_cast<std::int64_t>(clock.weekday())};
    if (rest == "minutes")
        return Value{static_cast<std::int64_t>(clock.minutesOfDay())};
    if (rest == "time")
        return Value{clock.time()};
    if (rest == "total")
        return Value{clock.totalMinutes()};
    if (rest == "paused")
        return Value{clock.paused()};
    if (rest == "scale")
        return Value{clock.scale()};
    if (rest == "daylight")
        return Value{clock.daylight()};
    return std::nullopt;
}
void checkTime(const Json &args, const char *key, RuleReport &report) {
    if (args.get(key).isString() && !parseTimeOfDay(args.get(key).asString()))
        report.error(std::string("'") + key + "' must be a time like 06:30");
}
} // namespace

void registerClockRules(RuleCatalog &catalog) {
    using Kind = ParamSpec::Kind;
    const auto param = [](const char *name, Kind kind, bool required = false,
                          const char *description = "") {
        return ParamSpec::make(name, kind, required, description);
    };
    catalog.addFacts("clock", clockFact, false);
    catalog.addPredicate(
        {"TimeBetween",
         "Clock",
         "True when the world's clock reads a time in [from, to) (it may wrap "
         "midnight: 22:00 to 06:00).",
         {param("from", Kind::String, true, "HH:MM"), param("to", Kind::String, true, "HH:MM")},
         [](const Json &args, RuleContext &context) {
             const auto from = parseTimeOfDay(args.get("from").asString());
             const auto to = parseTimeOfDay(args.get("to").asString());
             return from && to && context.game.services().get<WorldClock>().inRange(*from, *to);
         },
         [](const Json &args, RuleReport &report) {
             checkTime(args, "from", report);
             checkTime(args, "to", report);
         }});
    catalog.addPredicate({"IsDaylight",
                          "Clock",
                          "True between the clock's dawn and dusk.",
                          {},
                          [](const Json &, RuleContext &context) {
                              return context.game.services().get<WorldClock>().daylight();
                          },
                          nullptr});
    catalog.addAction(
        {"SetTime",
         "Clock",
         "Sets the world's clock (the minutes it passes are announced as skipped).",
         {param("time", Kind::String, true, "HH:MM"),
          param("day", Kind::Int, false, "default today")},
         [](const Json &args, RuleContext &context) {
             const auto time = parseTimeOfDay(args.get("time").asString());
             if (!time)
                 return ActionResult::Failed;
             WorldClock &clock = context.game.services().get<WorldClock>();
             clock.set(context.game,
                       args.contains("day")
                           ? static_cast<int>(toInt(context.argument(args.get("day")), clock.day()))
                           : clock.day(),
                       *time);
             return ActionResult::Done;
         },
         [](const Json &args, RuleReport &report) { checkTime(args, "time", report); }});
    catalog.addAction(
        {"SkipTime",
         "Clock",
         "Moves the clock on by some minutes, or to the next time it reads `until` (sleeping).",
         {param("minutes", Kind::Number), param("until", Kind::String, false, "HH:MM")},
         [](const Json &args, RuleContext &context) {
             WorldClock &clock = context.game.services().get<WorldClock>();
             if (args.contains("until")) {
                 const auto until = parseTimeOfDay(args.get("until").asString());
                 if (!until)
                     return ActionResult::Failed;
                 clock.skipTo(context.game, *until);
                 return ActionResult::Done;
             }
             const double minutes = toNumber(context.argument(args.get("minutes")), 0.0);
             if (!(minutes > 0.0))
                 return ActionResult::Failed;
             clock.skip(context.game, minutes);
             return ActionResult::Done;
         },
         [](const Json &args, RuleReport &report) {
             checkTime(args, "until", report);
             if (!args.contains("minutes") && !args.contains("until"))
                 report.error("action 'SkipTime' needs 'minutes' or 'until'");
         }});
    catalog.addAction({"PauseClock",
                       "Clock",
                       "Stops the world's clock until the same reason resumes it.",
                       {param("reason", Kind::String, false, "default \"rule\"")},
                       [](const Json &args, RuleContext &context) {
                           context.game.services().get<WorldClock>().pause(
                               context.game,
                               args.contains("reason") ? args.get("reason").asString() : "rule",
                               true);
                           return ActionResult::Done;
                       },
                       nullptr});
    catalog.addAction({"ResumeClock",
                       "Clock",
                       "Releases a pause of the world's clock (it runs when none is left).",
                       {param("reason", Kind::String, false, "default \"rule\"")},
                       [](const Json &args, RuleContext &context) {
                           context.game.services().get<WorldClock>().pause(
                               context.game,
                               args.contains("reason") ? args.get("reason").asString() : "rule",
                               false);
                           return ActionResult::Done;
                       },
                       nullptr});
    catalog.addAction({"SetClockScale",
                       "Clock",
                       "Changes how fast the world's clock runs (minutes per real second).",
                       {param("scale", Kind::Number, true)},
                       [](const Json &args, RuleContext &context) {
                           context.game.services().get<WorldClock>().setScale(
                               toNumber(context.argument(args.get("scale")), 1.0));
                           return ActionResult::Done;
                       },
                       nullptr});
}
} // namespace yk
