#pragma once
#include "yk/rules/Rules.hpp"
#include "yk/runtime/Services.hpp"
#include "yk/scene/Registry.hpp"
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

// The time of the world: a day, an hour and a minute that advance at their own pace (minutes of the
// world per real second), pause for cutscenes and menus, and can be set or skipped (sleeping
// through the night). It is not the game's real time (GameContext::time()), which keeps counting.
// Everything that has a schedule, a curfew, a shop that closes or a guard shift asks this clock.
//
// Events (source: none; the data names the time): clock.minute, clock.hour, clock.day,
// clock.daylight (data: daylight) and clock.paused / clock.resumed. Rules can run something at a
// time of day with an event filter: {"when": "clock.minute", "data": {"time": "06:00"}, "then":
// ...}. Facts: clock.hour, clock.minute, clock.day, clock.weekday (0-6), clock.minutes (since
// midnight), clock.time ("06:00"), clock.total (minutes since day 1 began), clock.paused,
// clock.scale, clock.daylight.
namespace yk {
class GameContext;

constexpr int minutesPerDay = 24 * 60;
// "06:30" (or "6:30", or just an hour: "6") as minutes since midnight; nothing for anything else.
std::optional<int> parseTimeOfDay(std::string_view text);
// 390 -> "06:30" (wraps at midnight).
std::string formatTimeOfDay(int minutes);
// Whether `minute` (since midnight) is in [from, to), the clock wrapping at midnight: 22:00-06:00
// holds 23:30 and 02:00. from == to is the whole day.
bool timeInRange(int minute, int from, int to);

// Where the clock starts and how it runs: a scene may carry one (an entity of its own is the usual
// place); without it the clock starts on day 1 at 06:00 and runs a minute of the world per second.
class ClockSettings final : public Component {
  public:
    float minutesPerSecond{1.0F};
    int startDay{1};
    std::string startTime{"06:00"};
    bool startPaused{false};
    std::string dayStarts{"06:00"};   // Daylight lasts from here...
    std::string nightStarts{"20:00"}; // ...to here.
    static void describe(TypeBuilder<ClockSettings> &type);
    void onStart(GameContext &context) override;
};

class WorldClock final : public Service {
  public:
    using Callback = std::function<void(GameContext &)>;
    const char *name() const override {
        return "clock";
    }
    UpdatePhase phase() const override {
        return UpdatePhase::Clock;
    }
    void onFixedUpdate(GameContext &context, float seconds) override;
    std::string saveKey() const override {
        return "clock";
    }
    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;
    void describe(std::vector<std::pair<std::string, std::string>> &rows) const override;

    // Takes the scene's ClockSettings (the component does this when it starts).
    void configure(const ClockSettings &settings);

    // ---- Reading -------------------------------------------------------------------------------
    double totalMinutes() const {
        return total_;
    }
    int day() const {
        return static_cast<int>(total_ / minutesPerDay) + 1;
    }
    int minutesOfDay() const {
        return static_cast<int>(total_) % minutesPerDay;
    }
    int hour() const {
        return minutesOfDay() / 60;
    }
    int minute() const {
        return minutesOfDay() % 60;
    }
    int weekday() const {
        return (day() - 1) % 7;
    }
    std::string time() const {
        return formatTimeOfDay(minutesOfDay());
    }
    bool daylight() const;
    // How many whole minutes the clock has ever turned over (for things that look once a minute).
    std::uint64_t minuteCounter() const {
        return minuteCounter_;
    }
    bool inRange(int from, int to) const {
        return timeInRange(minutesOfDay(), from, to);
    }
    // Minutes until the clock next reads `minuteOfDay` (24 hours when it reads it now).
    int minutesUntil(int minuteOfDay) const;

    // ---- Running -------------------------------------------------------------------------------
    double scale() const {
        return scale_;
    }
    void setScale(double minutesPerSecond);
    // Named holds, so that a cutscene and a menu can both pause it: it runs when none is held.
    void pause(GameContext &context, const std::string &reason, bool on);
    bool paused() const {
        return !pauses_.empty();
    }
    // Moves the clock without it running (a minute event for every minute passed, flagged
    // `skipped`).
    void set(GameContext &context, int day, int minuteOfDay);
    void skip(GameContext &context, double minutes);
    // To the next time the clock reads `minuteOfDay`.
    void skipTo(GameContext &context, int minuteOfDay);

    // ---- Callbacks for code that wants to run at a time ----------------------------------------
    // At the next time the clock reads `minuteOfDay` (every day after that if `daily`).
    std::uint64_t at(int minuteOfDay, Callback callback, bool daily = false);
    // After the world has run `gameMinutes` more minutes.
    std::uint64_t after(double gameMinutes, Callback callback);
    void cancel(std::uint64_t id);

  private:
    struct Scheduled {
        std::uint64_t id{};
        std::int64_t dueMinute{}; // Absolute minute (total) it runs at.
        int dailyAt{-1};          // >= 0: runs again every day at this minute of day.
        Callback callback;
    };
    void advanceTo(GameContext &context, double newTotal, bool skipped);
    void turnMinute(GameContext &context, std::int64_t minute, bool skipped, bool announceMinute);
    bool daylightAt(int minuteOfDay) const;

    double total_{6.0 * 60.0};
    double scale_{1.0};
    std::set<std::string> pauses_;
    int dayStart_{6 * 60};
    int nightStart_{20 * 60};
    bool daylight_{true};
    std::uint64_t minuteCounter_{0};
    std::vector<Scheduled> scheduled_;
    std::uint64_t nextId_{1};
    bool configured_{false};
};

void registerClockRules(RuleCatalog &catalog);
} // namespace yk
