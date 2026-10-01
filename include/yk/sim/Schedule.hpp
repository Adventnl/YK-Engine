#pragma once
#include "yk/data/Table.hpp"
#include "yk/rules/Rules.hpp"
#include "yk/scene/Registry.hpp"
#include <optional>
#include <string>
#include <vector>

// Routines as data. A schedule is a list of blocks over the day: from a time to a time, an activity
// (a label for people and rules), where to be, and what to do there (a behavior word that the
// character's AI maps to its own states). Different kinds of character have different schedules;
// nothing here says what an inmate or a shopkeeper does.
//
//   "schedules": [
//     {"id": "inmate", "roles": ["inmate"], "blocks": [
//        {"id": "wake",  "from": "06:00", "to": "07:00", "activity": "Wake up",
//         "destination": {"purpose": "sleep"}, "behavior": "idle"},
//        {"id": "roll",  "from": "07:00", "to": "08:00", "activity": "Roll call",
//         "destination": {"zone": "yard_lineup"}, "behavior": "stand", "tolerance": 5,
//         "requires": {"enter": "zone:yard_lineup", "stay": 30}},
//        {"id": "lunch", "from": "12:00", "to": "13:00", "activity": "Lunch",
//         "destination": "purpose:dining", "behavior": "eat"},
//        {"id": "curfew", "from": "22:00", "to": "06:00", "activity": "Lights out",
//         "destination": "home", "behavior": "sleep", "days": [0, 1, 2, 3, 4, 5, 6]}]}]
//
// A block that ends before it starts wraps over midnight. Blocks may name the weekdays they apply
// to (0-6, counting from the first day of the game). Where blocks overlap the higher `priority`
// wins, then the later one in the file. Times with no block are free time: the character's AI
// decides.
namespace yk {
class GameContext;
class GameData;

struct ScheduleDestination {
    enum class Kind { None, Zone, Room, Purpose, Entity, Point, Home };
    Kind kind{Kind::None};
    std::string
        name; // The zone, room or purpose; an entity spec ("name:Bed_3"); empty for the others.
    Vec2 point{};

    static Result<ScheduleDestination> fromJson(const Json &json);
    Json toJson() const;
    std::string text() const; // "purpose:dining", "home", "point:(3, 4)", ""
};

// What a character must do in a block, for a routine that is enforced (the player's): be in a
// place, stay a while, do something that raises an event.
struct ScheduleRequirement {
    std::string enter;  // "zone:yard", "room:cafeteria", "purpose:dining": where; empty: anywhere
    double stay{0.0};   // Seconds in it
    std::string action; // An event the character must raise during the block (a named action)
};

struct ScheduleBlock {
    std::string id;
    std::string activity;
    std::string behavior;
    int from{0}; // Minutes since midnight.
    int to{0};
    std::vector<int> days; // Weekdays it applies to; none: every day.
    ScheduleDestination destination;
    double tolerance{5.0}; // Minutes it may take to get there before it counts as late.
    int priority{0};
    std::optional<ScheduleRequirement> requirement;
    std::vector<Action> onStart; // Rule actions, run for the character (self and actor).
    std::vector<Action> onEnd;
    std::vector<std::string> tags;
    Json data; // Whatever else rules and brains want to read.

    // Whether the block is on at `minute` of a day that is `weekday`.
    bool covers(int minute, int weekday) const;
    // How long it lasts, in minutes (a whole day when from == to).
    int length() const;
};

struct ScheduleDefinition {
    std::string id;
    std::string file;
    std::string name;
    std::vector<std::string>
        roles; // Characters with one of these roles use it when they name none.
    std::vector<ScheduleBlock> blocks;

    static Result<ScheduleDefinition> fromJson(const Json &json,
                                               std::vector<std::string> &warnings);
    Json toJson() const;
    // The block on at that time: the highest priority, then the latest in the file; null in a gap.
    const ScheduleBlock *blockAt(int minute, int weekday) const;
    // The block that starts next after `minute`, and in how many minutes; null when none ever does.
    const ScheduleBlock *next(int minute, int weekday, int *minutesUntil = nullptr) const;
};
using ScheduleTable = DefinitionTable<ScheduleDefinition>;

struct ScheduleCatalog {
    ScheduleTable schedules;
    void load(const Json &document, const std::string &file, std::vector<DataProblem> &problems);
    // Blocks that fight over a time with nothing to choose between them.
    void check(std::vector<DataProblem> &problems) const;
    void visitRules(const RuleSourceVisitor &visit) const;
    // The schedule meant for a role, if one lists it.
    const ScheduleDefinition *forRole(std::string_view role) const;
};

// Follows a schedule on the world clock for one character: which block it is in, which comes next,
// whether it got there in time. Raises (source: the character)
//   schedule.block_started / schedule.block_ended   data: schedule, block, activity, behavior,
//                                                   destination, from, to
//   schedule.arrived      the character reached the block's destination (found by itself when the
//                         destination is a zone, room, purpose, entity or point; or reported)
//   schedule.late         the block's tolerance ran out before it did
//   schedule.requirement_met / schedule.requirement_missed   (when `enforce`: the player's routine)
//                         the block's `requires` was done during the block, or was not by its end
// Whatever drives the character (an AI brain) reads current() and goes where it says; rules react
// to the events. While `excuse`d (in a fight, during an alarm) the character is not counted late.
class ScheduleAgent final : public Component {
  public:
    std::string schedule;     // A schedule id; empty: the one that lists the character's role.
    bool detectArrival{true}; // Notice by itself when it is at the destination.
    bool enforce{false};      // Track the blocks' `requires` (the player's routine).
    static void describe(TypeBuilder<ScheduleAgent> &type);

    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onDestroy(GameContext &context) override;
    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;

    const ScheduleDefinition *definition() const {
        return definition_;
    }
    const ScheduleBlock *current() const {
        return current_;
    }
    // The block that starts next, and in how many minutes of the world (-1: none).
    const ScheduleBlock *upcoming() const {
        return upcoming_;
    }
    int minutesUntilNext() const {
        return minutesUntilNext_;
    }
    // Minutes since the current block started / until it ends (-1 outside a block).
    int minutesInto() const {
        return minutesInto_;
    }
    int minutesLeft() const;
    bool arrived() const {
        return arrived_;
    }
    bool late() const {
        return late_;
    }
    // How the current block's requirement stands: done, and how far (0..1) the time to stay is.
    bool requirementMet() const {
        return requirementMet_;
    }
    double requirementProgress() const;
    void reportArrived(GameContext &context);
    void excuse(GameContext &context, const std::string &reason, bool on);
    bool excused() const {
        return !excuses_.empty();
    }
    // Picks the schedule again (a role changed) and works out the block from the clock.
    void refresh(GameContext &context);

  private:
    void evaluate(GameContext &context);
    void track(GameContext &context, float seconds);
    void switchTo(GameContext &context, const ScheduleBlock *block);
    void listenForAction(GameContext &context);
    void stopListening(GameContext &context);
    void announce(GameContext &context, const char *event, const ScheduleBlock &block);
    void run(GameContext &context, const std::vector<Action> &actions, const ScheduleBlock &block);

    const ScheduleDefinition *definition_{nullptr};
    const ScheduleBlock *current_{nullptr};
    const ScheduleBlock *upcoming_{nullptr};
    int minutesUntilNext_{-1};
    int minutesInto_{-1};
    bool arrived_{false};
    bool late_{false};
    // Requirement progress in the current block.
    std::optional<ScheduleDestination> requirementPlace_;
    bool entered_{false};
    double stayed_{0.0};
    bool actionDone_{false};
    bool requirementMet_{false};
    bool excusedInBlock_{false};
    EventBus::Subscription subscription_{0};
    float sinceLook_{0.0F};
    std::vector<std::string> excuses_;
    std::uint64_t seenMinute_{~std::uint64_t{0}};
};

void registerScheduleRules(RuleCatalog &catalog);
// Registers the world clock and schedules: ClockSettings, ScheduleAgent and their rules.
void registerScheduleComponents(ComponentRegistry &registry);
} // namespace yk
