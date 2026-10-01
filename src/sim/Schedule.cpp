#include "yk/sim/Schedule.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/sim/Clock.hpp"
#include "yk/sim/Identity.hpp"
#include "yk/sim/Zones.hpp"
#include <algorithm>
#include <set>

namespace yk {
// ---- Destinations
// --------------------------------------------------------------------------------
Result<ScheduleDestination> ScheduleDestination::fromJson(const Json &json) {
    ScheduleDestination destination;
    const auto named = [&](Kind kind, const std::string &name) -> Result<ScheduleDestination> {
        if (name.empty())
            return Error{"a destination needs the name of a zone, room, purpose or entity"};
        destination.kind = kind;
        destination.name = name;
        return destination;
    };
    if (json.isString()) {
        const std::string &text = json.asString();
        if (text == "home") {
            destination.kind = Kind::Home;
            return destination;
        }
        const std::size_t colon = text.find(':');
        if (colon == std::string::npos)
            return Error{"a destination is \"home\", \"zone:<id>\", \"room:<id>\", "
                         "\"purpose:<name>\", \"entity:<spec>\", or an object"};
        const std::string kind = text.substr(0, colon), name = text.substr(colon + 1);
        if (kind == "zone")
            return named(Kind::Zone, name);
        if (kind == "room")
            return named(Kind::Room, name);
        if (kind == "purpose")
            return named(Kind::Purpose, name);
        if (kind == "entity")
            return named(Kind::Entity, name);
        return Error{"'" + kind + "' is not a kind of destination (zone, room, purpose, entity)"};
    }
    if (!json.isObject())
        return Error{"a destination is a string or an object"};
    int ways = 0;
    for (const char *key : {"zone", "room", "purpose", "entity", "point", "home"})
        ways += json.contains(key) ? 1 : 0;
    if (ways != 1)
        return Error{"a destination names exactly one of zone, room, purpose, entity, point, home"};
    if (json.contains("zone"))
        return named(Kind::Zone, json.get("zone").asString());
    if (json.contains("room"))
        return named(Kind::Room, json.get("room").asString());
    if (json.contains("purpose"))
        return named(Kind::Purpose, json.get("purpose").asString());
    if (json.contains("entity"))
        return named(Kind::Entity, json.get("entity").asString());
    if (json.contains("point")) {
        const Json &point = json.get("point");
        if (!point.isArray() || point.size() != 2 || !point.at(0).isNumber() ||
            !point.at(1).isNumber())
            return Error{"'point' must be [x, y]"};
        destination.kind = Kind::Point;
        destination.point = {static_cast<float>(point.at(0).asNumber()),
                             static_cast<float>(point.at(1).asNumber())};
        return destination;
    }
    destination.kind = Kind::Home;
    return destination;
}

Json ScheduleDestination::toJson() const {
    Json json = Json::object();
    switch (kind) {
    case Kind::None:
        break;
    case Kind::Zone:
        json.set("zone", name);
        break;
    case Kind::Room:
        json.set("room", name);
        break;
    case Kind::Purpose:
        json.set("purpose", name);
        break;
    case Kind::Entity:
        json.set("entity", name);
        break;
    case Kind::Point: {
        Json pair = Json::array();
        pair.push(point.x);
        pair.push(point.y);
        json.set("point", std::move(pair));
        break;
    }
    case Kind::Home:
        json.set("home", true);
        break;
    }
    return json;
}

std::string ScheduleDestination::text() const {
    switch (kind) {
    case Kind::None:
        return {};
    case Kind::Zone:
        return "zone:" + name;
    case Kind::Room:
        return "room:" + name;
    case Kind::Purpose:
        return "purpose:" + name;
    case Kind::Entity:
        return "entity:" + name;
    case Kind::Point:
        return "point:(" + std::to_string(point.x) + ", " + std::to_string(point.y) + ")";
    case Kind::Home:
        return "home";
    }
    return {};
}

// ---- Blocks and schedules
// ----------------------------------------------------------------------------
bool ScheduleBlock::covers(int minute, int weekday) const {
    const auto dayOk = [&](int day) {
        return days.empty() ||
               std::find(days.begin(), days.end(), ((day % 7) + 7) % 7) != days.end();
    };
    if (from == to)
        return dayOk(weekday);
    if (from < to)
        return minute >= from && minute < to && dayOk(weekday);
    if (minute >= from)
        return dayOk(weekday);
    if (minute < to)
        return dayOk(weekday + 6); // Started yesterday.
    return false;
}

int ScheduleBlock::length() const {
    if (from == to)
        return minutesPerDay;
    return from < to ? to - from : minutesPerDay - from + to;
}

namespace {
Result<int> timeField(const Json &json, const char *key) {
    const Json &value = json.get(key);
    if (value.isNumber() && value.asNumber() >= 0.0 && value.asNumber() < 24.0 &&
        value.asNumber() == static_cast<double>(static_cast<int>(value.asNumber())))
        return static_cast<int>(value.asNumber()) * 60;
    if (!value.isString())
        return Error{std::string("'") + key + "' is needed: a time like \"06:30\""};
    const auto parsed = parseTimeOfDay(value.asString());
    if (!parsed)
        return Error{std::string("'") + key + "' is not a time (\"" + value.asString() +
                     "\"): use HH:MM"};
    return *parsed;
}

Result<ScheduleBlock> blockFromJson(const Json &json, std::size_t index,
                                    std::vector<std::string> &warnings) {
    const std::string place = "block " + std::to_string(index + 1);
    if (!json.isObject())
        return Error{place + ": must be an object"};
    ScheduleBlock block;
    auto id = data::requiredString(json, "id");
    if (!id)
        return Error{place + ": " + id.error()};
    block.id = id.value();
    if (!data::validId(block.id))
        return Error{place + ": '" + block.id +
                     "' is not a usable id (letters, digits, '_' and '-')"};
    const std::string where = "block '" + block.id + "': ";
    auto from = timeField(json, "from");
    if (!from)
        return Error{where + from.error()};
    auto to = timeField(json, "to");
    if (!to)
        return Error{where + to.error()};
    block.from = from.value();
    block.to = to.value();
    block.activity = data::optionalString(json, "activity", block.id);
    block.behavior = data::optionalString(json, "behavior", "idle");
    if (json.contains("destination")) {
        auto destination = ScheduleDestination::fromJson(json.get("destination"));
        if (!destination)
            return Error{where + "destination: " + destination.error()};
        block.destination = destination.value();
    }
    auto tolerance = data::number(json, "tolerance", 5.0, 0.0, 1440.0);
    if (!tolerance)
        return Error{where + tolerance.error()};
    block.tolerance = tolerance.value();
    block.priority = static_cast<int>(json.get("priority").asInt(0));
    if (json.contains("days")) {
        if (!json.get("days").isArray())
            return Error{where + "'days' must be a list of weekday numbers (0-6)"};
        for (const Json &day : json.get("days").items()) {
            if (!day.isNumber() || day.asNumber() < 0.0 || day.asNumber() > 6.0 ||
                day.asNumber() != static_cast<double>(static_cast<int>(day.asNumber())))
                return Error{where + "'days' must be a list of weekday numbers (0-6)"};
            const int number = static_cast<int>(day.asNumber());
            if (std::find(block.days.begin(), block.days.end(), number) == block.days.end())
                block.days.push_back(number);
        }
        std::sort(block.days.begin(), block.days.end());
    }
    if (json.contains("requires")) {
        const Json &needs = json.get("requires");
        if (!needs.isObject() || needs.size() == 0)
            return Error{where + "'requires' must be an object with enter, stay and/or action"};
        ScheduleRequirement requirement;
        requirement.enter = data::optionalString(needs, "enter");
        requirement.action = data::optionalString(needs, "action");
        auto stay = data::number(needs, "stay", 0.0, 0.0, 86400.0);
        if (!stay)
            return Error{where + "requires: " + stay.error()};
        requirement.stay = stay.value();
        if (!requirement.enter.empty()) {
            auto target = ScheduleDestination::fromJson(Json(requirement.enter));
            if (!target || target.value().kind == ScheduleDestination::Kind::Home)
                return Error{where + "requires: 'enter' must be \"zone:<id>\", \"room:<id>\" or "
                                     "\"purpose:<name>\""};
        }
        data::warnUnknown(needs, {"enter", "stay", "action"}, warnings);
        block.requirement = requirement;
    }
    for (const char *key : {"onStart", "onEnd"}) {
        auto actions = Action::listFromJson(json.get(key));
        if (!actions)
            return Error{where + key + ": " + actions.error()};
        (std::string(key) == "onStart" ? block.onStart : block.onEnd) = std::move(actions.value());
    }
    block.tags = data::stringList(json, "tags");
    if (json.contains("data")) {
        if (!json.get("data").isObject())
            return Error{where + "'data' must be an object"};
        block.data = json.get("data");
    }
    std::vector<std::string> unknown;
    data::warnUnknown(json,
                      {"id", "from", "to", "activity", "behavior", "destination", "tolerance",
                       "priority", "days", "requires", "onStart", "onEnd", "tags", "data"},
                      unknown);
    for (const std::string &warning : unknown)
        warnings.push_back(where + warning);
    return block;
}
} // namespace

Result<ScheduleDefinition> ScheduleDefinition::fromJson(const Json &json,
                                                        std::vector<std::string> &warnings) {
    if (!json.isObject())
        return Error{"a schedule must be an object"};
    ScheduleDefinition schedule;
    auto id = data::requiredString(json, "id");
    if (!id)
        return Error{id.error()};
    schedule.id = id.value();
    if (!data::validId(schedule.id))
        return Error{"'" + schedule.id + "' is not a usable id (letters, digits, '_' and '-')"};
    schedule.name = data::optionalString(json, "name", schedule.id);
    schedule.roles = data::stringList(json, "roles");
    const Json &blocks = json.get("blocks");
    if (!blocks.isArray() || blocks.size() == 0)
        return Error{"'blocks' must be a list with at least one block"};
    std::set<std::string> ids;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        auto block = blockFromJson(blocks.at(i), i, warnings);
        if (!block)
            return Error{block.error()};
        if (!ids.insert(block.value().id).second)
            return Error{"two blocks are called '" + block.value().id + "'"};
        schedule.blocks.push_back(std::move(block.value()));
    }
    data::warnUnknown(json, {"id", "name", "roles", "blocks"}, warnings);
    return schedule;
}

Json ScheduleDefinition::toJson() const {
    Json json = Json::object();
    json.set("id", id);
    json.set("name", name);
    if (!roles.empty())
        json.set("roles", data::toJsonList(roles));
    Json list = Json::array();
    for (const ScheduleBlock &block : blocks) {
        Json item = Json::object();
        item.set("id", block.id);
        item.set("from", formatTimeOfDay(block.from));
        item.set("to", formatTimeOfDay(block.to));
        item.set("activity", block.activity);
        item.set("behavior", block.behavior);
        if (block.destination.kind != ScheduleDestination::Kind::None)
            item.set("destination", block.destination.toJson());
        item.set("tolerance", block.tolerance);
        if (block.priority != 0)
            item.set("priority", block.priority);
        if (!block.days.empty()) {
            Json days = Json::array();
            for (const int day : block.days)
                days.push(day);
            item.set("days", std::move(days));
        }
        if (block.requirement) {
            Json needs = Json::object();
            if (!block.requirement->enter.empty())
                needs.set("enter", block.requirement->enter);
            if (block.requirement->stay > 0.0)
                needs.set("stay", block.requirement->stay);
            if (!block.requirement->action.empty())
                needs.set("action", block.requirement->action);
            item.set("requires", std::move(needs));
        }
        if (!block.onStart.empty()) {
            Json actions = Json::array();
            for (const Action &action : block.onStart)
                actions.push(action.toJson());
            item.set("onStart", std::move(actions));
        }
        if (!block.onEnd.empty()) {
            Json actions = Json::array();
            for (const Action &action : block.onEnd)
                actions.push(action.toJson());
            item.set("onEnd", std::move(actions));
        }
        if (!block.tags.empty())
            item.set("tags", data::toJsonList(block.tags));
        if (block.data.isObject())
            item.set("data", block.data);
        list.push(std::move(item));
    }
    json.set("blocks", std::move(list));
    return json;
}

const ScheduleBlock *ScheduleDefinition::blockAt(int minute, int weekday) const {
    const ScheduleBlock *best = nullptr;
    for (const ScheduleBlock &block : blocks)
        if (block.covers(minute, weekday) && (!best || block.priority >= best->priority))
            best = &block;
    return best;
}

const ScheduleBlock *ScheduleDefinition::next(int minute, int weekday, int *minutesUntil) const {
    const ScheduleBlock *best = nullptr;
    int bestDistance = 0;
    for (const ScheduleBlock &block : blocks)
        for (int offset = 0; offset <= 7; ++offset) {
            const int day = (weekday + offset) % 7;
            if (!block.days.empty() &&
                std::find(block.days.begin(), block.days.end(), day) == block.days.end())
                continue;
            const int distance = offset * minutesPerDay + block.from - minute;
            if (distance <= 0)
                continue;
            if (!best || distance < bestDistance) {
                best = &block;
                bestDistance = distance;
            }
            break; // Later days only start later.
        }
    if (minutesUntil)
        *minutesUntil = best ? bestDistance : -1;
    return best;
}

// ---- The catalog
// -----------------------------------------------------------------------------------
void ScheduleCatalog::load(const Json &document, const std::string &file,
                           std::vector<DataProblem> &problems) {
    if (document.contains("schedules"))
        schedules.load(document.get("schedules"), file, problems, "schedule");
}

void ScheduleCatalog::check(std::vector<DataProblem> &problems) const {
    for (const ScheduleDefinition &schedule : schedules.all()) {
        std::set<std::pair<std::string, std::string>> reported;
        for (int weekday = 0; weekday < 7; ++weekday)
            for (int minute = 0; minute < minutesPerDay; ++minute) {
                const ScheduleBlock *first = nullptr;
                for (const ScheduleBlock &block : schedule.blocks) {
                    if (!block.covers(minute, weekday))
                        continue;
                    if (!first) {
                        first = &block;
                        continue;
                    }
                    if (first->priority != block.priority)
                        continue;
                    if (reported.insert({first->id, block.id}).second)
                        problems.push_back({schedule.file,
                                            "schedule '" + schedule.id + "': blocks '" + first->id +
                                                "' and '" + block.id + "' overlap at " +
                                                formatTimeOfDay(minute) +
                                                " with the same priority (the later one wins)",
                                            false});
                }
            }
    }
}

void ScheduleCatalog::visitRules(const RuleSourceVisitor &visit) const {
    for (const ScheduleDefinition &schedule : schedules.all())
        for (const ScheduleBlock &block : schedule.blocks) {
            const std::string where = "schedule '" + schedule.id + "' block '" + block.id + "' ";
            if (!block.onStart.empty())
                visit({schedule.file, where + "onStart", nullptr, &block.onStart});
            if (!block.onEnd.empty())
                visit({schedule.file, where + "onEnd", nullptr, &block.onEnd});
        }
}

const ScheduleDefinition *ScheduleCatalog::forRole(std::string_view role) const {
    if (role.empty())
        return nullptr;
    for (const ScheduleDefinition &schedule : schedules.all())
        if (data::has(schedule.roles, role))
            return &schedule;
    return nullptr;
}

// ---- The agent
// ------------------------------------------------------------------------------------
void ScheduleAgent::describe(TypeBuilder<ScheduleAgent> &type) {
    type.category("Simulation")
        .description("Follows a daily schedule on the world clock: which block this character is "
                     "in, which is next, whether it arrived in time. An AI brain goes where the "
                     "block says; rules react to its events.")
        .updatePhase(UpdatePhase::PreUpdate);
    type.field("schedule", &ScheduleAgent::schedule)
        .ref("schedule")
        .tooltip("A schedule id; empty: the schedule that lists this character's role.");
    type.field("detectArrival", &ScheduleAgent::detectArrival)
        .tooltip("Notice by itself when the character is at the block's destination.");
    type.field("enforce", &ScheduleAgent::enforce)
        .tooltip(
            "Track what the blocks require (be in a place, stay, do something) and say when it "
            "was not done: the player's routine.");
    type.check([](const Entity &, const ScheduleAgent &agent, const CheckContext &context,
                  std::vector<std::string> &problems) {
        if (!agent.schedule.empty() && context.known && !context.known("schedule", agent.schedule))
            problems.push_back("follows the schedule '" + agent.schedule +
                               "', which is not defined");
    });
}

void ScheduleAgent::onStart(GameContext &context) {
    refresh(context);
}

void ScheduleAgent::onDestroy(GameContext &context) {
    stopListening(context);
    current_ = nullptr;
}

void ScheduleAgent::stopListening(GameContext &context) {
    if (subscription_ != 0)
        context.events().unsubscribe(subscription_);
    subscription_ = 0;
}

// A block that wants an action done: any event of that name raised by (or to) this character.
void ScheduleAgent::listenForAction(GameContext &context) {
    stopListening(context);
    if (!enforce || !current_ || !current_->requirement || current_->requirement->action.empty())
        return;
    const std::string wanted = current_->requirement->action;
    subscription_ = context.events().subscribe(EventBus::anyEvent, [this, wanted](
                                                                       const GameEvent &event) {
        if (event.name == wanted && (event.source == entity().id() || event.other == entity().id()))
            actionDone_ = true;
    });
}

void ScheduleAgent::refresh(GameContext &context) {
    const GameData &data = gameData(context);
    definition_ = nullptr;
    if (!schedule.empty()) {
        definition_ = data.schedules.schedules.find(schedule);
        if (!definition_)
            log(LogLevel::Warning, "schedule",
                "'" + entity().name() + "' follows the schedule '" + schedule +
                    "', which is not defined");
    } else if (const auto *who = entity().get<Identity>()) {
        definition_ = data.schedules.forRole(who->role);
    }
    stopListening(context);
    current_ = nullptr;
    upcoming_ = nullptr;
    minutesUntilNext_ = -1;
    minutesInto_ = -1;
    seenMinute_ = ~std::uint64_t{0};
    evaluate(context);
}

void ScheduleAgent::onFixedUpdate(GameContext &context, float seconds) {
    if (context.services().get<WorldClock>().minuteCounter() != seenMinute_)
        evaluate(context);
    track(context, seconds);
}

double ScheduleAgent::requirementProgress() const {
    if (!current_ || !current_->requirement)
        return 0.0;
    if (requirementMet_)
        return 1.0;
    const ScheduleRequirement &need = *current_->requirement;
    if (need.stay > 0.0)
        return std::min(1.0, stayed_ / need.stay);
    return entered_ || need.enter.empty() ? 0.5 : 0.0;
}

// What happens between minutes: noticing arrival, and keeping count of what the block requires.
void ScheduleAgent::track(GameContext &context, float seconds) {
    if (!current_)
        return;
    const bool needsLooking =
        (!arrived_ && detectArrival) || (enforce && current_->requirement && !requirementMet_);
    if (!needsLooking)
        return;
    const ZoneService &zones = context.services().get<ZoneService>();
    sinceLook_ += seconds;
    if (!arrived_ && detectArrival && sinceLook_ >= 0.1F &&
        current_->destination.kind != ScheduleDestination::Kind::None &&
        zones.isAt(context, current_->destination, entity()))
        reportArrived(context);
    if (enforce && current_ && current_->requirement && !requirementMet_) {
        const ScheduleRequirement &need = *current_->requirement;
        const bool inside = !requirementPlace_ || zones.isAt(context, *requirementPlace_, entity());
        if (inside) {
            entered_ = true;
            stayed_ += seconds;
        } else {
            stayed_ = 0.0; // The stay is unbroken: leaving starts it over.
        }
        const bool placeOk =
            need.enter.empty() ? true : (need.stay > 0.0 ? stayed_ >= need.stay : entered_);
        const bool actionOk = need.action.empty() || actionDone_;
        if (placeOk && actionOk) {
            requirementMet_ = true;
            announce(context, "schedule.requirement_met", *current_);
        }
    }
    if (sinceLook_ >= 0.1F)
        sinceLook_ = 0.0F;
}

int ScheduleAgent::minutesLeft() const {
    return current_ && minutesInto_ >= 0 ? current_->length() - minutesInto_ : -1;
}

void ScheduleAgent::announce(GameContext &context, const char *event, const ScheduleBlock &block) {
    Json data = Json::object();
    data.set("schedule", definition_ ? definition_->id : std::string());
    data.set("block", block.id);
    data.set("activity", block.activity);
    data.set("behavior", block.behavior);
    data.set("destination", block.destination.text());
    data.set("from", formatTimeOfDay(block.from));
    data.set("to", formatTimeOfDay(block.to));
    context.events().emit(GameEvent(event, entity().id(), {}, std::move(data)));
}

void ScheduleAgent::run(GameContext &context, const std::vector<Action> &actions,
                        const ScheduleBlock &block) {
    if (actions.empty())
        return;
    RuleContext rc(context);
    rc.self = entity().id();
    rc.actor = entity().id();
    rc.origin = "schedule '" + (definition_ ? definition_->id : std::string()) + "' block '" +
                block.id + "' of '" + entity().name() + "'";
    execute(actions, rc);
}

void ScheduleAgent::switchTo(GameContext &context, const ScheduleBlock *block) {
    if (current_) {
        const ScheduleBlock *old = current_;
        if (enforce && old->requirement && !requirementMet_ && !excusedInBlock_) {
            Json missed = Json::object();
            missed.set("block", old->id);
            missed.set("activity", old->activity);
            missed.set("schedule", definition_ ? definition_->id : std::string());
            missed.set("entered", entered_);
            missed.set("stayed", stayed_);
            missed.set("action", actionDone_);
            missed.set("required", old->requirement->enter);
            context.events().emit(
                GameEvent("schedule.requirement_missed", entity().id(), {}, std::move(missed)));
        }
        run(context, old->onEnd, *old);
        announce(context, "schedule.block_ended", *old);
    }
    current_ = block;
    arrived_ = block && block->destination.kind == ScheduleDestination::Kind::None;
    late_ = false;
    entered_ = false;
    stayed_ = 0.0;
    actionDone_ = false;
    requirementMet_ = false;
    excusedInBlock_ = excused();
    requirementPlace_.reset();
    if (current_ && current_->requirement && !current_->requirement->enter.empty()) {
        auto place = ScheduleDestination::fromJson(Json(current_->requirement->enter));
        if (place)
            requirementPlace_ = place.value();
    }
    listenForAction(context);
    if (current_) {
        announce(context, "schedule.block_started", *current_);
        run(context, current_->onStart, *current_);
    }
}

void ScheduleAgent::evaluate(GameContext &context) {
    const WorldClock &clock = context.services().get<WorldClock>();
    seenMinute_ = clock.minuteCounter();
    if (!definition_) {
        current_ = nullptr;
        upcoming_ = nullptr;
        minutesUntilNext_ = -1;
        minutesInto_ = -1;
        return;
    }
    const int minute = clock.minutesOfDay(), weekday = clock.weekday();
    const ScheduleBlock *now = definition_->blockAt(minute, weekday);
    if (now != current_)
        switchTo(context, now);
    minutesInto_ = current_ ? (minute - current_->from + minutesPerDay) % minutesPerDay : -1;
    upcoming_ = definition_->next(minute, weekday, &minutesUntilNext_);
    if (current_ && !arrived_ && !late_ && !excused() &&
        static_cast<double>(minutesInto_) > current_->tolerance) {
        late_ = true;
        Json data = Json::object();
        data.set("block", current_->id);
        data.set("activity", current_->activity);
        data.set("schedule", definition_->id);
        data.set("minutesLate", static_cast<double>(minutesInto_) - current_->tolerance);
        context.events().emit(GameEvent("schedule.late", entity().id(), {}, std::move(data)));
    }
}

void ScheduleAgent::reportArrived(GameContext &context) {
    if (!current_ || arrived_)
        return;
    arrived_ = true;
    announce(context, "schedule.arrived", *current_);
}

void ScheduleAgent::excuse(GameContext &, const std::string &reason, bool on) {
    const auto found = std::find(excuses_.begin(), excuses_.end(), reason);
    if (on && found == excuses_.end()) {
        excuses_.push_back(reason);
        excusedInBlock_ = true; // What is missed while excused is not held against the character.
    } else if (!on && found != excuses_.end()) {
        excuses_.erase(found);
    }
}

Json ScheduleAgent::saveState() const {
    Json state = Json::object();
    state.set("block", current_ ? current_->id : std::string());
    state.set("arrived", arrived_);
    state.set("late", late_);
    state.set("entered", entered_);
    state.set("stayed", stayed_);
    state.set("actionDone", actionDone_);
    state.set("requirementMet", requirementMet_);
    state.set("excused", excusedInBlock_);
    return state;
}

Status ScheduleAgent::loadState(GameContext &, const Json &state) {
    // The agent has already found its block from the clock; what it had done in that block stays
    // done when a saved game puts it back in the same one.
    if (current_ && current_->id == state.get("block").asString()) {
        arrived_ = state.get("arrived").asBool(arrived_);
        late_ = state.get("late").asBool(late_);
        entered_ = state.get("entered").asBool(entered_);
        stayed_ = state.get("stayed").asNumber(stayed_);
        actionDone_ = state.get("actionDone").asBool(actionDone_);
        requirementMet_ = state.get("requirementMet").asBool(requirementMet_);
        excusedInBlock_ = state.get("excused").asBool(excusedInBlock_);
    }
    return success();
}

// ---- Rules
// ---------------------------------------------------------------------------------------
namespace {
std::optional<Value> scheduleFact(RuleContext &, Entity *subject, std::string_view rest) {
    const auto *agent = subject ? subject->get<ScheduleAgent>() : nullptr;
    if (!agent)
        return std::nullopt;
    const ScheduleBlock *block = agent->current();
    if (rest == "block")
        return Value{block ? block->id : std::string()};
    if (rest == "activity")
        return Value{block ? block->activity : std::string()};
    if (rest == "behavior")
        return Value{block ? block->behavior : std::string()};
    if (rest == "destination")
        return Value{block ? block->destination.text() : std::string()};
    if (rest == "schedule")
        return Value{agent->definition() ? agent->definition()->id : std::string()};
    if (rest == "next")
        return Value{agent->upcoming() ? agent->upcoming()->id : std::string()};
    if (rest == "nextActivity")
        return Value{agent->upcoming() ? agent->upcoming()->activity : std::string()};
    if (rest == "minutesLeft")
        return Value{static_cast<std::int64_t>(agent->minutesLeft())};
    if (rest == "minutesInto")
        return Value{static_cast<std::int64_t>(agent->minutesInto())};
    if (rest == "minutesUntilNext")
        return Value{static_cast<std::int64_t>(agent->minutesUntilNext())};
    if (rest == "arrived")
        return Value{agent->arrived()};
    if (rest == "late")
        return Value{agent->late()};
    if (rest == "requirementMet")
        return Value{agent->requirementMet()};
    if (rest == "requirementProgress")
        return Value{agent->requirementProgress()};
    return std::nullopt;
}
} // namespace

void registerScheduleRules(RuleCatalog &catalog) {
    using Kind = ParamSpec::Kind;
    const auto param = [](const char *name, Kind kind, bool required = false,
                          const char *description = "") {
        return ParamSpec::make(name, kind, required, description);
    };
    catalog.addFacts("schedule", scheduleFact, true);
    catalog.addPredicate(
        {"ScheduleBlockIs",
         "Schedule",
         "True when the entity's schedule is in a block with this id, activity, behavior or tag.",
         {param("block", Kind::String), param("activity", Kind::String),
          param("behavior", Kind::String), param("tag", Kind::String),
          param("entity", Kind::Entity, false, "default self")},
         [](const Json &args, RuleContext &context) {
             for (Entity *entity : context.entitiesFrom(args, "entity", "self")) {
                 const auto *agent = entity->get<ScheduleAgent>();
                 const ScheduleBlock *block = agent ? agent->current() : nullptr;
                 if (!block)
                     continue;
                 if (args.contains("block") && block->id != args.get("block").asString())
                     continue;
                 if (args.contains("activity") &&
                     block->activity != args.get("activity").asString())
                     continue;
                 if (args.contains("behavior") &&
                     block->behavior != args.get("behavior").asString())
                     continue;
                 if (args.contains("tag") && !data::has(block->tags, args.get("tag").asString()))
                     continue;
                 return true;
             }
             return false;
         },
         [](const Json &args, RuleReport &report) {
             if (!args.contains("block") && !args.contains("activity") &&
                 !args.contains("behavior") && !args.contains("tag"))
                 report.warning(
                     "condition 'ScheduleBlockIs' says nothing about the block, so it is "
                     "true whenever there is one");
         }});
    catalog.addAction(
        {"ExcuseSchedule",
         "Schedule",
         "While excused (in a fight, during an alarm) a character is not counted "
         "late.",
         {param("reason", Kind::String, true), param("on", Kind::Bool, false, "default true"),
          param("entity", Kind::Entity, false, "default self")},
         [](const Json &args, RuleContext &context) {
             bool done = false;
             for (Entity *entity : context.entitiesFrom(args, "entity", "self"))
                 if (auto *agent = entity->get<ScheduleAgent>()) {
                     agent->excuse(context.game, args.get("reason").asString(),
                                   args.get("on").asBool(true));
                     done = true;
                 }
             return done ? ActionResult::Done : ActionResult::Failed;
         },
         nullptr});
    catalog.addAction({"ReportArrival",
                       "Schedule",
                       "Says the character has reached the destination of its current block.",
                       {param("entity", Kind::Entity, false, "default self")},
                       [](const Json &args, RuleContext &context) {
                           bool done = false;
                           for (Entity *entity : context.entitiesFrom(args, "entity", "self"))
                               if (auto *agent = entity->get<ScheduleAgent>()) {
                                   agent->reportArrived(context.game);
                                   done = true;
                               }
                           return done ? ActionResult::Done : ActionResult::Failed;
                       },
                       nullptr});
}

void registerScheduleComponents(ComponentRegistry &registry) {
    registerClockRules(registry.extend<RuleCatalog>());
    registerScheduleRules(registry.extend<RuleCatalog>());
    registry.add<ClockSettings>("ClockSettings");
    registry.add<ScheduleAgent>("ScheduleAgent");
}
} // namespace yk
