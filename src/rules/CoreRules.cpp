#include "yk/core/Log.hpp"
#include "yk/rules/RuleService.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/runtime/Random.hpp"
#include <cmath>

namespace yk {
namespace {
using Kind = ParamSpec::Kind;

ParamSpec param(const char *name, Kind kind, bool required = false, const char *description = "",
                const char *refKind = "") {
    return ParamSpec::make(name, kind, required, description, refKind);
}

// Entity arguments name entities with a spec ("self", "actor", "name:Gate", "tag:guard").
std::vector<Entity *> entitiesOf(const Json &args, const char *key, RuleContext &context,
                                 const char *fallback = "self") {
    const std::string spec = args.contains(key) ? args.get(key).asString() : fallback;
    return context.resolve(spec);
}
Vec2 pointOf(const Json &json) {
    if (json.isArray() && json.size() == 2)
        return {static_cast<float>(json.at(0).asNumber()),
                static_cast<float>(json.at(1).asNumber())};
    return {};
}
} // namespace

void registerCoreRules(RuleCatalog &catalog) {
    // ---- Facts ---------------------------------------------------------------------------------
    catalog.addFacts(
        "var",
        [](RuleContext &context, Entity *, std::string_view rest) -> std::optional<Value> {
            const std::string key(rest);
            return context.game.blackboard().get(key);
        },
        false);
    catalog.addFacts(
        "event",
        [](RuleContext &context, Entity *, std::string_view rest) -> std::optional<Value> {
            if (!context.event)
                return std::nullopt;
            if (rest == "name")
                return Value{context.event->name};
            if (rest == "source")
                return Value{context.event->source};
            if (rest == "other")
                return Value{context.event->other};
            if (rest.starts_with("data.")) {
                auto value = valueFromJson(context.event->data.get(std::string(rest.substr(5))));
                return value ? std::optional<Value>(value.value()) : std::nullopt;
            }
            return std::nullopt;
        },
        false);
    catalog.addFacts(
        "time",
        [](RuleContext &context, Entity *, std::string_view rest) -> std::optional<Value> {
            if (rest == "seconds")
                return Value{context.game.time()};
            if (rest == "tick")
                return Value{static_cast<std::int64_t>(context.game.tick())};
            return std::nullopt;
        },
        false);
    // What any entity has: its name, tags, place and whether it is active ("actor.entity.name").
    catalog.addFacts(
        "entity",
        [](RuleContext &, Entity *subject, std::string_view rest) -> std::optional<Value> {
            if (!subject)
                return std::nullopt;
            if (rest == "name")
                return Value{subject->name()};
            if (rest == "id")
                return Value{subject->id()};
            if (rest == "active")
                return Value{subject->activeInHierarchy()};
            if (rest == "x")
                return Value{static_cast<double>(subject->worldPosition().x)};
            if (rest == "y")
                return Value{static_cast<double>(subject->worldPosition().y)};
            if (rest == "position")
                return Value{subject->worldPosition()};
            if (rest.starts_with("tag."))
                return Value{subject->hasTag(rest.substr(4))};
            return std::nullopt;
        },
        true);

    // ---- Conditions ----------------------------------------------------------------------------
    catalog.addPredicate({"Chance",
                          "Logic",
                          "True with the given probability (0..1), drawn from the game's dice.",
                          {param("probability", Kind::Number, true, "0 never, 1 always")},
                          [](const Json &args, RuleContext &context) {
                              return context.game.services().get<RandomService>().rng.chance(
                                  toNumber(context.argument(args.get("probability")), 0.0));
                          },
                          nullptr});
    catalog.addPredicate(
        {"EntityActive",
         "Entities",
         "True when the entity exists and is active.",
         {param("entity", Kind::Entity, true, "self, actor, target, name:..., id:...")},
         [](const Json &args, RuleContext &context) {
             const auto found = context.resolve(args.get("entity").asString());
             return !found.empty() && found.front()->activeInHierarchy();
         },
         nullptr});
    catalog.addPredicate({"HasTag",
                          "Entities",
                          "True when the entity carries the tag.",
                          {param("entity", Kind::Entity, false, "default: the actor"),
                           param("tag", Kind::String, true)},
                          [](const Json &args, RuleContext &context) {
                              const std::string spec =
                                  args.contains("entity") ? args.get("entity").asString() : "actor";
                              const auto found = context.resolve(spec);
                              return !found.empty() &&
                                     found.front()->hasTag(args.get("tag").asString());
                          },
                          nullptr});

    // ---- Actions -------------------------------------------------------------------------------
    catalog.addAction({"SetVariable",
                       "Variables",
                       "Sets a game variable to a value (a number, text, true/false, or a $fact).",
                       {param("name", Kind::String, true), param("value", Kind::Any, true),
                        param("keep", Kind::Bool, false, "Carry it into the next scene")},
                       [](const Json &args, RuleContext &context) {
                           const std::string name = args.get("name").asString();
                           context.game.blackboard().setValue(name,
                                                              context.argument(args.get("value")));
                           if (args.get("keep").asBool(false))
                               context.game.blackboard().keep(name);
                           return ActionResult::Done;
                       },
                       nullptr});
    catalog.addAction(
        {"AddVariable",
         "Variables",
         "Adds to a numeric game variable (negative to subtract).",
         {param("name", Kind::String, true), param("amount", Kind::Number, false, "default 1")},
         [](const Json &args, RuleContext &context) {
             context.game.blackboard().add(
                 args.get("name").asString(),
                 args.contains("amount") ? toNumber(context.argument(args.get("amount"))) : 1.0);
             return ActionResult::Done;
         },
         nullptr});
    catalog.addAction({"ToggleVariable",
                       "Variables",
                       "Flips a true/false game variable.",
                       {param("name", Kind::String, true)},
                       [](const Json &args, RuleContext &context) {
                           const std::string name = args.get("name").asString();
                           context.game.blackboard().setBool(name,
                                                             !context.game.blackboard().flag(name));
                           return ActionResult::Done;
                       },
                       nullptr});
    catalog.addAction({"ClearVariable",
                       "Variables",
                       "Removes a game variable.",
                       {param("name", Kind::String, true)},
                       [](const Json &args, RuleContext &context) {
                           context.game.blackboard().erase(args.get("name").asString());
                           return ActionResult::Done;
                       },
                       nullptr});
    catalog.addAction(
        {"EmitEvent",
         "Events",
         "Raises an event others can react to, optionally after a delay and with data.",
         {param("name", Kind::String, true), param("data", Kind::Any, false, "An object of values"),
          param("delay", Kind::Number, false, "Seconds"),
          param("source", Kind::Entity, false, "default self"),
          param("other", Kind::Entity, false, "default none")},
         [](const Json &args, RuleContext &context) {
             const std::string source =
                 args.contains("source") ? args.get("source").asString() : "self";
             const Entity *from = context.resolveOne(source);
             const Entity *to = args.contains("other")
                                    ? context.resolveOne(args.get("other").asString())
                                    : nullptr;
             Json data = args.get("data").isObject() ? args.get("data") : Json();
             GameEvent event(args.get("name").asString(), from ? from->id() : EntityId{},
                             to ? to->id() : EntityId{}, std::move(data));
             const double delay = toNumber(context.argument(args.get("delay")), 0.0);
             if (delay > 0.0)
                 context.game.events().emitAfter(std::move(event), delay);
             else
                 context.game.events().emit(std::move(event));
             return ActionResult::Done;
         },
         nullptr});
    catalog.addAction(
        {"Log",
         "Events",
         "Writes a message to the log ({variable} placeholders are filled in).",
         {param("message", Kind::String, true)},
         [](const Json &args, RuleContext &context) {
             log(LogLevel::Info, "rules",
                 (context.origin.empty() ? "" : context.origin + ": ") +
                     context.game.blackboard().format(args.get("message").asString()));
             return ActionResult::Done;
         },
         nullptr});
    catalog.addAction(
        {"If",
         "Control",
         "Runs one list of actions when a condition holds and another when it does not.",
         {param("if", Kind::Condition, true), param("then", Kind::Actions, false),
          param("else", Kind::Actions, false)},
         [](const Json &args, RuleContext &context) {
             auto condition = Condition::fromJson(args.get("if"));
             if (!condition)
                 return ActionResult::Failed;
             auto actions = Action::listFromJson(
                 args.get(evaluate(condition.value(), context) ? "then" : "else"));
             return actions ? execute(actions.value(), context) : ActionResult::Failed;
         },
         nullptr});
    catalog.addAction({"Sequence",
                       "Control",
                       "Runs a list of actions, one after the other, now.",
                       {param("actions", Kind::Actions, true)},
                       [](const Json &args, RuleContext &context) {
                           auto actions = Action::listFromJson(args.get("actions"));
                           return actions ? execute(actions.value(), context)
                                          : ActionResult::Failed;
                       },
                       nullptr});
    catalog.addAction({"Delay",
                       "Control",
                       "Runs a list of actions after some seconds of game time.",
                       {param("seconds", Kind::Number, true), param("then", Kind::Actions, true)},
                       [](const Json &args, RuleContext &context) {
                           auto actions = Action::listFromJson(args.get("then"));
                           if (!actions)
                               return ActionResult::Failed;
                           schedulerFor(context.game)
                               ->schedule(toNumber(context.argument(args.get("seconds")), 0.0),
                                          std::move(actions.value()), context);
                           return ActionResult::Done;
                       },
                       nullptr});
    catalog.addAction(
        {"Repeat",
         "Control",
         "Runs a list of actions several times, a fixed time apart (the first run is now).",
         {param("count", Kind::Int, true),
          param("every", Kind::Number, false, "Seconds between runs"),
          param("then", Kind::Actions, true)},
         [](const Json &args, RuleContext &context) {
             auto actions = Action::listFromJson(args.get("then"));
             if (!actions)
                 return ActionResult::Failed;
             const int count = std::clamp(static_cast<int>(args.get("count").asInt(1)), 0, 1000);
             const double every = toNumber(context.argument(args.get("every")), 0.0);
             for (int i = 0; i < count; ++i) {
                 if (i == 0 || every <= 0.0)
                     execute(actions.value(), context);
                 else
                     schedulerFor(context.game)
                         ->schedule(every * static_cast<double>(i), actions.value(), context);
             }
             return ActionResult::Done;
         },
         nullptr});
    catalog.addAction({"SetRuleEnabled",
                       "Logic",
                       "Switches one rule of an entity's rule set on or off.",
                       {param("rule", Kind::String, true, "The rule's id"),
                        param("enabled", Kind::Bool, false, "default true"),
                        param("entity", Kind::Entity, false, "default self")},
                       [](const Json &args, RuleContext &context) {
                           bool done = false;
                           for (Entity *entity : entitiesOf(args, "entity", context))
                               if (auto *set = entity->get<RuleSet>())
                                   done = set->setRuleEnabled(context.game,
                                                              args.get("rule").asString(),
                                                              args.get("enabled").asBool(true)) ||
                                          done;
                           return done ? ActionResult::Done : ActionResult::Failed;
                       },
                       nullptr});
    catalog.addAction(
        {"EnableEntity",
         "Entities",
         "Switches an entity (or every entity with a tag) on.",
         {param("entity", Kind::Entity, true, "name:Gate, tag:guard, id:..., self, actor, target")},
         [](const Json &args, RuleContext &context) {
             for (Entity *entity : entitiesOf(args, "entity", context))
                 entity->setActive(true);
             return ActionResult::Done;
         },
         nullptr});
    catalog.addAction({"DisableEntity",
                       "Entities",
                       "Switches an entity (or every entity with a tag) off.",
                       {param("entity", Kind::Entity, true)},
                       [](const Json &args, RuleContext &context) {
                           for (Entity *entity : entitiesOf(args, "entity", context))
                               entity->setActive(false);
                           return ActionResult::Done;
                       },
                       nullptr});
    catalog.addAction({"DespawnEntity",
                       "Entities",
                       "Removes an entity from the game.",
                       {param("entity", Kind::Entity, true)},
                       [](const Json &args, RuleContext &context) {
                           for (Entity *entity : entitiesOf(args, "entity", context))
                               context.game.destroyLater(entity->id());
                           return ActionResult::Done;
                       },
                       nullptr});
    catalog.addAction(
        {"SpawnEntity",
         "Entities",
         "Creates an entity from a prefab, at a point or at another entity.",
         {param("prefab", Kind::String, true), param("at", Kind::Vec2, false),
          param("atEntity", Kind::Entity, false, "Place it at this entity")},
         [](const Json &args, RuleContext &context) {
             Vec2 where = pointOf(args.get("at"));
             if (args.contains("atEntity"))
                 if (const Entity *place = context.resolveOne(args.get("atEntity").asString()))
                     where = place->worldPosition();
             auto spawned = context.game.spawnPrefab(args.get("prefab").asString(), where);
             if (!spawned) {
                 log(LogLevel::Warning, "rules", "SpawnEntity: " + spawned.error());
                 return ActionResult::Failed;
             }
             return ActionResult::Done;
         },
         nullptr});
    catalog.addAction({"Teleport",
                       "Entities",
                       "Moves an entity to a point or to another entity.",
                       {param("entity", Kind::Entity, false, "default the actor"),
                        param("to", Kind::Vec2, false), param("toEntity", Kind::Entity, false)},
                       [](const Json &args, RuleContext &context) {
                           Vec2 where = pointOf(args.get("to"));
                           if (args.contains("toEntity")) {
                               const Entity *place =
                                   context.resolveOne(args.get("toEntity").asString());
                               if (!place)
                                   return ActionResult::Failed;
                               where = place->worldPosition();
                           }
                           for (Entity *entity : entitiesOf(args, "entity", context, "actor"))
                               context.game.teleport(*entity, where);
                           return ActionResult::Done;
                       },
                       nullptr});
    catalog.addAction({"PlaySound",
                       "Audio",
                       "Plays a sound.",
                       {param("sound", Kind::String, true), param("volume", Kind::Number, false)},
                       [](const Json &args, RuleContext &context) {
                           context.game.audio().play(
                               args.get("sound").asString(),
                               static_cast<float>(args.get("volume").asNumber(1.0)), false);
                           return ActionResult::Done;
                       },
                       nullptr});
    catalog.addAction({"ChangeScene",
                       "Flow",
                       "Moves the game to another scene.",
                       {param("scene", Kind::String, true)},
                       [](const Json &args, RuleContext &context) {
                           context.game.requestSceneChange(args.get("scene").asString());
                           return ActionResult::Done;
                       },
                       nullptr});
    catalog.addAction({"RestartLevel",
                       "Flow",
                       "Starts the current scene over.",
                       {},
                       [](const Json &, RuleContext &context) {
                           context.game.requestRestart();
                           return ActionResult::Done;
                       },
                       nullptr});
}
} // namespace yk
