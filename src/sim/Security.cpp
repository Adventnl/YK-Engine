#include "yk/sim/Security.hpp"
#include "yk/data/GameData.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/sim/Identity.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
Result<LockdownDefinition> LockdownDefinition::fromJson(const Json &json,
                                                        std::vector<std::string> &warnings) {
    if (!json.isObject())
        return Error{"a lockdown must be an object"};
    LockdownDefinition def;
    auto id = data::requiredString(json, "id");
    if (!id)
        return Error{id.error()};
    def.id = id.value();
    if (!data::validId(def.id))
        return Error{"'" + def.id + "' is not a usable id (letters, digits, '_' and '-')"};
    def.name = data::optionalString(json, "name", def.id);
    def.level = data::optionalString(json, "level");
    auto count = data::number(json, "countdown", 0.0, 0.0, 1.0e6);
    if (!count)
        return Error{count.error()};
    def.countdown = count.value();
    for (const char *key : {"onStart", "onEnd", "onFail"}) {
        auto actions = Action::listFromJson(json.get(key));
        if (!actions)
            return Error{std::string(key) + ": " + actions.error()};
        (std::string(key) == "onStart" ? def.onStart
         : std::string(key) == "onEnd" ? def.onEnd
                                       : def.onFail) = std::move(actions.value());
    }
    data::warnUnknown(json, {"id", "name", "level", "countdown", "onStart", "onEnd", "onFail"},
                      warnings);
    return def;
}

void SecurityCatalog::load(const Json &document, const std::string &fileName,
                           std::vector<DataProblem> &problems) {
    if (!document.contains("security"))
        return;
    const Json &section = document.get("security");
    file = fileName;
    if (!section.isObject()) {
        problems.push_back(
            {fileName, "'security' must be an object with levels and lockdowns", true});
        return;
    }
    if (section.contains("levels")) {
        if (!section.get("levels").isArray()) {
            problems.push_back({fileName, "security 'levels' must be a list", true});
        } else {
            levels.clear();
            for (std::size_t i = 0; i < section.get("levels").size(); ++i) {
                const Json &item = section.get("levels").at(i);
                const std::string place = "security level " + std::to_string(i + 1);
                SecurityLevel level;
                auto id = data::requiredString(item, "id");
                if (!item.isObject() || !id || !data::validId(id.value())) {
                    problems.push_back(
                        {fileName, place + ": needs an 'id' (letters, digits, '_' and '-')", true});
                    continue;
                }
                level.id = id.value();
                level.name = data::optionalString(item, "name", level.id);
                level.decay = item.get("decay").asNumber(0.0);
                bool ok = true;
                for (const char *key : {"onEnter", "onExit"}) {
                    auto actions = Action::listFromJson(item.get(key));
                    if (!actions) {
                        problems.push_back(
                            {fileName, place + " " + key + ": " + actions.error(), true});
                        ok = false;
                        break;
                    }
                    (std::string(key) == "onEnter" ? level.onEnter : level.onExit) =
                        std::move(actions.value());
                }
                if (ok)
                    levels.push_back(std::move(level));
            }
        }
    }
    if (section.contains("lockdowns"))
        lockdowns.load(section.get("lockdowns"), fileName, problems, "lockdown");
}

int SecurityCatalog::indexOf(std::string_view id) const {
    for (std::size_t i = 0; i < levels.size(); ++i)
        if (levels[i].id == id)
            return static_cast<int>(i);
    return -1;
}

void SecurityCatalog::check(std::vector<DataProblem> &problems) const {
    for (const LockdownDefinition &def : lockdowns.all())
        if (!def.level.empty() && indexOf(def.level) < 0)
            problems.push_back({def.file,
                                "lockdown '" + def.id + "' puts the facility in the level '" +
                                    def.level + "', which is not defined",
                                true});
}

void SecurityCatalog::visitRules(const RuleSourceVisitor &visit) const {
    for (const SecurityLevel &level : levels) {
        if (!level.onEnter.empty())
            visit({file, "security level '" + level.id + "' onEnter", nullptr, &level.onEnter});
        if (!level.onExit.empty())
            visit({file, "security level '" + level.id + "' onExit", nullptr, &level.onExit});
    }
    for (const LockdownDefinition &def : lockdowns.all())
        for (const auto &[label, list] :
             {std::pair<const char *, const std::vector<Action> *>{"onStart", &def.onStart},
              {"onEnd", &def.onEnd},
              {"onFail", &def.onFail}})
            if (!list->empty())
                visit({def.file, "lockdown '" + def.id + "' " + label, nullptr, list});
}

// ---- Service
std::string SecurityService::levelId(GameContext &context) const {
    const auto &levels = gameData(context).security.levels;
    return level_ >= 0 && level_ < static_cast<int>(levels.size())
               ? levels[static_cast<std::size_t>(level_)].id
               : std::to_string(level_);
}

void SecurityService::run(GameContext &context, const std::vector<Action> &actions,
                          const std::string &origin) {
    if (actions.empty())
        return;
    RuleContext rc(context);
    rc.origin = origin;
    execute(actions, rc);
}

bool SecurityService::setLevel(GameContext &context, int index, const std::string &cause) {
    const auto &levels = gameData(context).security.levels;
    const int top = std::max(0, static_cast<int>(levels.size()) - 1);
    index = std::clamp(index, 0, top);
    if (index == level_ || inChange_)
        return false;
    inChange_ = true;
    const int before = level_;
    if (before < static_cast<int>(levels.size()))
        run(context, levels[static_cast<std::size_t>(before)].onExit, "security level exit");
    level_ = index;
    sinceRaise_ = 0.0;
    if (index < static_cast<int>(levels.size()))
        run(context, levels[static_cast<std::size_t>(index)].onEnter, "security level enter");
    inChange_ = false;
    Json data = Json::object();
    data.set("from", before);
    data.set("to", index);
    data.set("level", levelId(context));
    data.set("cause", cause);
    context.events().emit(GameEvent("security.changed", {}, {}, std::move(data)));
    return true;
}
bool SecurityService::setLevel(GameContext &context, const std::string &id,
                               const std::string &cause) {
    const int index = gameData(context).security.indexOf(id);
    return index >= 0 && setLevel(context, index, cause);
}
bool SecurityService::raise(GameContext &context, int by, const std::string &cause) {
    if (by > 0)
        sinceRaise_ = 0.0;
    return setLevel(context, level_ + by, cause);
}

bool SecurityService::startLockdown(GameContext &context, const std::string &id) {
    const LockdownDefinition *def = gameData(context).security.lockdowns.find(id);
    if (!def || !lockdown_.empty())
        return false;
    lockdown_ = id;
    remaining_ = def->countdown;
    secondCarry_ = 0.0;
    if (!def->level.empty())
        setLevel(context, def->level, "lockdown:" + id);
    run(context, def->onStart, "lockdown '" + id + "' onStart");
    Json data = Json::object();
    data.set("lockdown", id);
    context.events().emit(GameEvent("lockdown.started", {}, {}, std::move(data)));
    return true;
}

bool SecurityService::endLockdown(GameContext &context, bool failed) {
    if (lockdown_.empty())
        return false;
    const std::string id = lockdown_;
    lockdown_.clear();
    remaining_ = 0.0;
    if (const LockdownDefinition *def = gameData(context).security.lockdowns.find(id))
        run(context, failed ? def->onFail : def->onEnd,
            "lockdown '" + id + (failed ? "' onFail" : "' onEnd"));
    Json data = Json::object();
    data.set("lockdown", id);
    data.set("failed", failed);
    context.events().emit(GameEvent("lockdown.ended", {}, {}, std::move(data)));
    return true;
}

void SecurityService::onFixedUpdate(GameContext &context, float seconds) {
    const auto &levels = gameData(context).security.levels;
    if (level_ > 0 && level_ < static_cast<int>(levels.size()) && lockdown_.empty()) {
        const double decay = levels[static_cast<std::size_t>(level_)].decay;
        sinceRaise_ += static_cast<double>(seconds);
        if (decay > 0.0 && sinceRaise_ >= decay)
            setLevel(context, level_ - 1, "decay");
    }
    if (!lockdown_.empty()) {
        const LockdownDefinition *def = gameData(context).security.lockdowns.find(lockdown_);
        if (def && def->countdown > 0.0) {
            remaining_ -= static_cast<double>(seconds);
            secondCarry_ += static_cast<double>(seconds);
            if (secondCarry_ >= 1.0) {
                secondCarry_ -= 1.0;
                Json data = Json::object();
                data.set("lockdown", lockdown_);
                data.set("remaining", std::max(0.0, remaining_));
                context.events().emit(GameEvent("lockdown.countdown", {}, {}, std::move(data)));
            }
            if (remaining_ <= 0.0)
                endLockdown(context, true);
        }
    }
}

Json SecurityService::saveState() const {
    Json state = Json::object();
    state.set("level", level_);
    state.set("sinceRaise", sinceRaise_);
    state.set("lockdown", lockdown_);
    state.set("remaining", remaining_);
    return state;
}
Status SecurityService::loadState(GameContext &, const Json &state) {
    level_ = std::max(0, static_cast<int>(state.get("level").asInt(0)));
    sinceRaise_ = state.get("sinceRaise").asNumber(0.0);
    lockdown_ = state.get("lockdown").asString();
    remaining_ = state.get("remaining").asNumber(0.0);
    return success();
}
void SecurityService::describe(std::vector<std::pair<std::string, std::string>> &rows) const {
    rows.push_back({"Security level", std::to_string(level_)});
    rows.push_back({"Lockdown", lockdown_.empty() ? "none" : lockdown_});
}

// ---- Rules
void registerSecurityRules(RuleCatalog &catalog) {
    using Kind = ParamSpec::Kind;
    const auto param = [](const char *n, Kind k, bool r = false, const char *d = "") {
        return ParamSpec::make(n, k, r, d);
    };
    catalog.addFacts(
        "security",
        [](RuleContext &context, Entity *, std::string_view rest) -> std::optional<Value> {
            const auto &service = context.game.services().get<SecurityService>();
            if (rest == "level")
                return Value{static_cast<std::int64_t>(service.level())};
            if (rest == "levelId")
                return Value{service.levelId(context.game)};
            if (rest == "lockdown")
                return Value{service.lockdown()};
            if (rest == "locked")
                return Value{!service.lockdown().empty()};
            if (rest == "remaining")
                return Value{service.lockdownRemaining()};
            return std::nullopt;
        },
        false);
    catalog.addPredicate({"SecurityLevel",
                          "Security",
                          "True when the security level is within [atLeast, atMost].",
                          {param("atLeast", Kind::Int), param("atMost", Kind::Int)},
                          [](const Json &args, RuleContext &context) {
                              const int level =
                                  context.game.services().get<SecurityService>().level();
                              return level >= args.get("atLeast").asInt(0) &&
                                     level <= args.get("atMost").asInt(1 << 20);
                          },
                          nullptr});
    catalog.addPredicate({"LockdownActive",
                          "Security",
                          "True during a lockdown (of this id, if given).",
                          {param("lockdown", Kind::String)},
                          [](const Json &args, RuleContext &context) {
                              const std::string &now =
                                  context.game.services().get<SecurityService>().lockdown();
                              return !now.empty() && (!args.contains("lockdown") ||
                                                      now == args.get("lockdown").asString());
                          },
                          nullptr});
    catalog.addPredicate(
        {"AccessAllowed",
         "Security",
         "True when the entity passes the AccessPolicy of the target.",
         {param("target", Kind::Entity, true),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             const Entity *who = context.resolveOne(
                 args.contains("entity") ? args.get("entity").asString() : "actor");
             const Entity *thing = context.resolveOne(args.get("target").asString());
             const auto *policy = thing ? thing->get<AccessPolicy>() : nullptr;
             return who && policy && policy->allows(context.game, *who);
         },
         nullptr});
    catalog.addAction(
        {"SetSecurityLevel",
         "Security",
         "Moves the facility to a security level (by id or number).",
         {param("level", Kind::Any, true), param("cause", Kind::String)},
         [](const Json &args, RuleContext &context) {
             auto &service = context.game.services().get<SecurityService>();
             const std::string cause = args.get("cause").asString();
             const Json &level = args.get("level");
             return (level.isNumber()
                         ? service.setLevel(context.game, static_cast<int>(level.asInt()), cause)
                         : service.setLevel(context.game, level.asString(), cause))
                        ? ActionResult::Done
                        : ActionResult::Failed;
         },
         nullptr});
    catalog.addAction({"RaiseSecurity",
                       "Security",
                       "Raises (or with a negative amount lowers) the level.",
                       {param("by", Kind::Int, false, "default 1"), param("cause", Kind::String)},
                       [](const Json &args, RuleContext &context) {
                           return context.game.services().get<SecurityService>().raise(
                                      context.game, static_cast<int>(args.get("by").asInt(1)),
                                      args.get("cause").asString())
                                      ? ActionResult::Done
                                      : ActionResult::Failed;
                       },
                       nullptr});
    catalog.addAction({"StartLockdown",
                       "Security",
                       "Starts a lockdown defined in the project's data.",
                       {ParamSpec::make("lockdown", Kind::Ref, true, "", "lockdown")},
                       [](const Json &args, RuleContext &context) {
                           return context.game.services().get<SecurityService>().startLockdown(
                                      context.game, args.get("lockdown").asString())
                                      ? ActionResult::Done
                                      : ActionResult::Failed;
                       },
                       nullptr});
    catalog.addAction({"EndLockdown",
                       "Security",
                       "Ends the lockdown that is running.",
                       {},
                       [](const Json &, RuleContext &context) {
                           return context.game.services().get<SecurityService>().endLockdown(
                                      context.game)
                                      ? ActionResult::Done
                                      : ActionResult::Failed;
                       },
                       nullptr});
}

void AccessPolicy::describe(TypeBuilder<AccessPolicy> &type) {
    type.category("Simulation")
        .description("Who may use this: factions, roles and a condition (keycard, quest, time of "
                     "day, security level). Doors, terminals and interactions ask it.");
    type.field("allowedFactions", &AccessPolicy::allowedFactions).ref("faction");
    type.field("allowedRoles", &AccessPolicy::allowedRoles);
    type.field("access", &AccessPolicy::access)
        .tooltip("A condition that must hold for the user ({\"type\": \"HasToken\", ...}); "
                 "empty: none.");
    type.field("countDisguise", &AccessPolicy::countDisguise)
        .tooltip("Faction checks go by what the user looks like.");
    type.field("lockedDuringLockdown", &AccessPolicy::lockedDuringLockdown)
        .tooltip("Nobody may use it while a lockdown runs.");
    type.field("deniedMessage", &AccessPolicy::deniedMessage);
}

bool AccessPolicy::allows(GameContext &context, const Entity &actor, std::string *why) const {
    const auto deny = [&](const std::string &reason) {
        if (why)
            *why = deniedMessage.empty() ? reason : deniedMessage;
        return false;
    };
    if (lockedDuringLockdown && !context.services().get<SecurityService>().lockdown().empty())
        return deny("locked down");
    const auto *identity = actor.get<Identity>();
    const auto listed = [](const std::vector<std::string> &list, const std::string &value) {
        return std::find(list.begin(), list.end(), value) != list.end();
    };
    if (!allowedFactions.empty()) {
        const std::string faction = countDisguise ? perceivedFaction(actor)
                                                  : (identity ? identity->faction : std::string());
        if (!listed(allowedFactions, faction))
            return deny("not for your faction");
    }
    if (!allowedRoles.empty() && !listed(allowedRoles, identity ? identity->role : std::string()))
        return deny("not for your role");
    if (!access.isNull() && !(access.isObject() && access.size() == 0)) {
        if (!(conditionSource_ == access)) {
            conditionSource_ = access;
            auto parsed = Condition::fromJson(access);
            conditionOk_ = static_cast<bool>(parsed);
            condition_ = parsed ? parsed.value() : Condition{};
        }
        if (conditionOk_) {
            RuleContext rc(context);
            rc.self = entity().id();
            rc.actor = actor.id();
            rc.target = entity().id();
            rc.origin = "access policy of '" + entity().name() + "'";
            if (!evaluate(condition_, rc))
                return deny("access is not granted");
        }
    }
    return true;
}

void registerSecurityComponents(ComponentRegistry &registry) {
    registry.add<AccessPolicy>("AccessPolicy");
    registerSecurityRules(registry.extend<RuleCatalog>());
}
} // namespace yk
