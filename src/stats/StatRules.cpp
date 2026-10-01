#include "yk/data/GameData.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/stats/Stats.hpp"
#include <cmath>

namespace yk {
namespace {
using Kind = ParamSpec::Kind;
ParamSpec param(const char *name, Kind kind, bool required = false, const char *description = "",
                const char *refKind = "") {
    return ParamSpec::make(name, kind, required, description, refKind);
}

std::optional<Value> statFact(RuleContext &, Entity *subject, std::string_view rest) {
    const auto *stats = subject ? subject->get<StatSet>() : nullptr;
    if (!stats)
        return std::nullopt;
    const std::string_view id = rest.substr(0, rest.find('.'));
    const std::string_view field =
        id.size() < rest.size() ? rest.substr(id.size() + 1) : std::string_view{};
    if (!stats->has(id))
        return std::nullopt;
    if (field.empty() || field == "value")
        return Value{stats->value(id)};
    if (field == "max")
        return Value{stats->max(id)};
    if (field == "min")
        return Value{stats->min(id)};
    if (field == "base")
        return Value{stats->base(id)};
    if (field == "regen")
        return Value{stats->regen(id)};
    if (field == "pct" || field == "fraction")
        return Value{stats->fraction(id) * (field == "pct" ? 100.0 : 1.0)};
    return std::nullopt;
}

std::optional<Value> effectFact(RuleContext &, Entity *subject, std::string_view rest) {
    const auto *effects = subject ? subject->get<StatusEffects>() : nullptr;
    if (!effects)
        return Value{false};
    if (rest.starts_with("flag."))
        return Value{effects->hasFlag(rest.substr(5))};
    if (rest.starts_with("tag."))
        return Value{effects->hasTag(rest.substr(4))};
    if (rest.starts_with("grants."))
        return Value{effects->grants(rest.substr(7))};
    const std::string_view id = rest.substr(0, rest.find('.'));
    const std::string_view field =
        id.size() < rest.size() ? rest.substr(id.size() + 1) : std::string_view{};
    if (field == "stacks")
        return Value{static_cast<std::int64_t>(effects->stacks(id))};
    if (field.empty() || field == "active")
        return Value{effects->has(id)};
    return std::nullopt;
}

std::optional<Value> healthFact(RuleContext &, Entity *subject, std::string_view rest) {
    const auto *health = subject ? subject->get<Health>() : nullptr;
    if (!health)
        return std::nullopt;
    if (rest == "alive")
        return Value{health->alive()};
    if (rest == "knocked_out")
        return Value{health->knockedOut()};
    if (rest == "active")
        return Value{health->active()};
    if (rest == "value")
        return Value{health->current()};
    if (rest == "max")
        return Value{health->maximum()};
    if (rest == "fraction")
        return Value{health->maximum() > 0.0 ? health->current() / health->maximum() : 0.0};
    return std::nullopt;
}

StatSet *statsOf(const Json &args, RuleContext &context) {
    const auto found = context.entitiesFrom(args, "entity", "actor");
    return found.empty() ? nullptr : found.front()->get<StatSet>();
}
} // namespace

void registerStatRules(RuleCatalog &catalog) {
    catalog.addFacts("stat", statFact, true);
    catalog.addFacts("effect", effectFact, true);
    catalog.addFacts("health", healthFact, true);

    catalog.addPredicate(
        {"StatAtLeast",
         "Stats",
         "True when a stat of the character is at least a value.",
         {param("stat", Kind::Ref, true, "", "stat"), param("value", Kind::Number, true),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             const StatSet *stats = statsOf(args, context);
             return stats && stats->value(args.get("stat").asString()) + 1e-9 >=
                                 toNumber(context.argument(args.get("value")));
         },
         nullptr});
    catalog.addPredicate(
        {"StatAtMost",
         "Stats",
         "True when a stat of the character is at most a value.",
         {param("stat", Kind::Ref, true, "", "stat"), param("value", Kind::Number, true),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             const StatSet *stats = statsOf(args, context);
             return stats && stats->value(args.get("stat").asString()) <=
                                 toNumber(context.argument(args.get("value"))) + 1e-9;
         },
         nullptr});
    catalog.addPredicate({"HasEffect",
                          "Stats",
                          "True when the character has the status effect.",
                          {param("effect", Kind::Ref, true, "", "effect"),
                           param("entity", Kind::Entity, false, "default the actor")},
                          [](const Json &args, RuleContext &context) {
                              const auto found = context.entitiesFrom(args, "entity", "actor");
                              const auto *effects =
                                  found.empty() ? nullptr : found.front()->get<StatusEffects>();
                              return effects && effects->has(args.get("effect").asString());
                          },
                          nullptr});
    catalog.addPredicate({"IsAlive",
                          "Stats",
                          "True when the character has health and has not died.",
                          {param("entity", Kind::Entity, false, "default the actor")},
                          [](const Json &args, RuleContext &context) {
                              const auto found = context.entitiesFrom(args, "entity", "actor");
                              const auto *health =
                                  found.empty() ? nullptr : found.front()->get<Health>();
                              return health && health->alive();
                          },
                          nullptr});
    catalog.addPredicate({"IsKnockedOut",
                          "Stats",
                          "True when the character is knocked out or recovering.",
                          {param("entity", Kind::Entity, false, "default the actor")},
                          [](const Json &args, RuleContext &context) {
                              const auto found = context.entitiesFrom(args, "entity", "actor");
                              const auto *health =
                                  found.empty() ? nullptr : found.front()->get<Health>();
                              return health && health->knockedOut();
                          },
                          nullptr});

    catalog.addAction(
        {"ModifyStat",
         "Stats",
         "Adds to a stat of the character (negative to take away), or sets it.",
         {param("stat", Kind::Ref, true, "", "stat"), param("amount", Kind::Number, false, "added"),
          param("set", Kind::Number, false, "set to this instead"),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             bool done = false;
             for (Entity *entity : context.entitiesFrom(args, "entity", "actor"))
                 if (auto *stats = entity->get<StatSet>();
                     stats && stats->has(args.get("stat").asString())) {
                     if (args.contains("set"))
                         stats->set(context.game, args.get("stat").asString(),
                                    toNumber(context.argument(args.get("set"))), "rule");
                     else
                         stats->add(context.game, args.get("stat").asString(),
                                    toNumber(context.argument(args.get("amount"))), "rule");
                     done = true;
                 }
             return done ? ActionResult::Done : ActionResult::Failed;
         },
         [](const Json &args, RuleReport &report) {
             if (!args.contains("amount") && !args.contains("set"))
                 report.error("action 'ModifyStat' needs 'amount' (to add) or 'set'");
         }});
    catalog.addAction({"ApplyEffect",
                       "Stats",
                       "Puts a status effect on the character.",
                       {param("effect", Kind::Ref, true, "", "effect"),
                        param("duration", Kind::Number, false, "seconds; default the effect's"),
                        param("entity", Kind::Entity, false, "default the actor")},
                       [](const Json &args, RuleContext &context) {
                           bool done = false;
                           for (Entity *entity : context.entitiesFrom(args, "entity", "actor"))
                               if (auto *effects = entity->get<StatusEffects>())
                                   done = effects->apply(
                                              context.game, args.get("effect").asString(),
                                              toNumber(context.argument(args.get("duration")), 0.0),
                                              context.self) ||
                                          done;
                           return done ? ActionResult::Done : ActionResult::Failed;
                       },
                       nullptr});
    catalog.addAction(
        {"RemoveEffect",
         "Stats",
         "Takes a status effect (or every effect with a tag) off the character.",
         {param("effect", Kind::Ref, false, "", "effect"), param("tag", Kind::String, false),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             bool done = false;
             for (Entity *entity : context.entitiesFrom(args, "entity", "actor"))
                 if (auto *effects = entity->get<StatusEffects>()) {
                     if (args.contains("effect"))
                         done =
                             effects->remove(context.game, args.get("effect").asString()) || done;
                     if (args.contains("tag"))
                         done =
                             effects->removeTagged(context.game, args.get("tag").asString()) > 0 ||
                             done;
                 }
             return done ? ActionResult::Done : ActionResult::Failed;
         },
         [](const Json &args, RuleReport &report) {
             if (!args.contains("effect") && !args.contains("tag"))
                 report.error("action 'RemoveEffect' needs an 'effect' or a 'tag'");
         }});
    catalog.addAction(
        {"Damage",
         "Stats",
         "Hurts the character (through its Health: resistances, invulnerability, knockout).",
         {param("amount", Kind::Number, true),
          param("damageType", Kind::String, false, "blunt, fire, ..."),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             bool done = false;
             for (Entity *entity : context.entitiesFrom(args, "entity", "actor"))
                 if (auto *health = entity->get<Health>()) {
                     DamageInfo info;
                     info.amount = toNumber(context.argument(args.get("amount")));
                     info.type = args.get("damageType").asString();
                     info.source = context.self;
                     info.blockable = false;
                     health->damage(context.game, info);
                     done = true;
                 }
             return done ? ActionResult::Done : ActionResult::Failed;
         },
         nullptr});
    catalog.addAction({"Heal",
                       "Stats",
                       "Restores health to the character.",
                       {param("amount", Kind::Number, true),
                        param("entity", Kind::Entity, false, "default the actor")},
                       [](const Json &args, RuleContext &context) {
                           bool done = false;
                           for (Entity *entity : context.entitiesFrom(args, "entity", "actor"))
                               if (auto *health = entity->get<Health>()) {
                                   health->heal(context.game,
                                                toNumber(context.argument(args.get("amount"))),
                                                context.self);
                                   done = true;
                               }
                           return done ? ActionResult::Done : ActionResult::Failed;
                       },
                       nullptr});
    catalog.addAction(
        {"Revive",
         "Stats",
         "Brings a knocked-out or dead character back.",
         {param("fraction", Kind::Number, false, "of the maximum health; default 0.25"),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             bool done = false;
             for (Entity *entity : context.entitiesFrom(args, "entity", "actor"))
                 if (auto *health = entity->get<Health>()) {
                     health->revive(context.game,
                                    args.contains("fraction")
                                        ? toNumber(context.argument(args.get("fraction")))
                                        : 0.25);
                     done = true;
                 }
             return done ? ActionResult::Done : ActionResult::Failed;
         },
         nullptr});
}

void registerStatComponents(ComponentRegistry &registry) {
    registerStatRules(registry.extend<RuleCatalog>());
    registry.add<StatSet>("StatSet");
    registry.add<StatusEffects>("StatusEffects");
    registry.add<Health>("Health");
}
} // namespace yk
