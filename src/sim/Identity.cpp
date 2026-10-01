#include "yk/sim/Identity.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/rules/Rules.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/sim/Factions.hpp"
#include "yk/stats/Stats.hpp"
#include <algorithm>

namespace yk {
namespace {
bool validPersistentId(const std::string &id) {
    if (id.empty())
        return false;
    for (const char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.' || c == ':'))
            return false;
    return true;
}
std::string slug(const std::string &name) {
    std::string text;
    for (const char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            text += c;
        else if (c >= 'A' && c <= 'Z')
            text += static_cast<char>(c - 'A' + 'a');
        else if (!text.empty() && text.back() != '_')
            text += '_';
    }
    while (!text.empty() && text.back() == '_')
        text.pop_back();
    return text.empty() ? "actor" : text;
}
} // namespace

// ---- The component
// -------------------------------------------------------------------------------------
void Identity::describe(TypeBuilder<Identity> &type) {
    type.category("Simulation")
        .description("Who this character is for the rest of the game: a persistent id that "
                     "survives saves and scene changes, a name to show, a faction and a role.");
    type.field("id", &Identity::id)
        .tooltip("Persistent id, unique in the world (\"npc.warden\", \"player.1\"). Leave empty "
                 "on a prefab: one is made up for each instance.");
    type.field("displayName", &Identity::displayName)
        .tooltip("The name shown to the player; empty: the entity's name.");
    type.field("faction", &Identity::faction).ref("faction");
    type.field("role", &Identity::role)
        .tooltip("The project's own word for what they are: guard, inmate, medic...");
    type.field("data", &Identity::data)
        .tooltip("Anything else rules and scripts want to read, as an object ({\"home\": "
                 "\"cell_12\"}).");
    type.check([](const Entity &, const Identity &who, const CheckContext &context,
                  std::vector<std::string> &problems) {
        if (!who.id.empty() && !validPersistentId(who.id))
            problems.push_back("the persistent id '" + who.id +
                               "' may only hold letters, digits and _ - . :");
        if (context.prefab && !who.id.empty())
            problems.push_back("is part of a prefab and names the persistent id '" + who.id +
                               "': every instance would claim it (leave it empty)");
        if (!context.prefab && who.id.empty())
            problems.push_back("has no persistent id; one is made up when the game starts, and "
                               "changes if the entity is renamed or reordered");
        if (!who.faction.empty() && context.known && !context.known("faction", who.faction))
            problems.push_back("belongs to the faction '" + who.faction +
                               "', which is not defined");
        if (!who.data.isObject() && !who.data.isNull())
            problems.push_back("'data' must be an object");
    });
}

std::string Identity::name() const {
    return displayName.empty() ? entity().name() : displayName;
}

void Identity::onStart(GameContext &context) {
    auto &actors = context.services().get<ActorService>();
    id = actors.add(entity().id(), id, entity().name());
    registered_ = true;
}

void Identity::onDestroy(GameContext &context) {
    if (!registered_)
        return;
    context.services().get<ActorService>().remove(entity().id(), id);
    registered_ = false;
}

void Identity::setFaction(GameContext &context, const std::string &value) {
    if (faction == value)
        return;
    const std::string before = faction;
    faction = value;
    context.services().get<ActorService>().touch();
    Json change = Json::object();
    change.set("from", before);
    change.set("to", value);
    context.events().emit(
        GameEvent("identity.faction_changed", entity().id(), {}, std::move(change)));
}

void Identity::setRole(GameContext &context, const std::string &value) {
    if (role == value)
        return;
    const std::string before = role;
    role = value;
    context.services().get<ActorService>().touch();
    Json change = Json::object();
    change.set("from", before);
    change.set("to", value);
    context.events().emit(GameEvent("identity.role_changed", entity().id(), {}, std::move(change)));
}

Json Identity::saveState() const {
    Json state = Json::object();
    state.set("id", id);
    state.set("faction", faction);
    state.set("role", role);
    return state;
}

Status Identity::loadState(GameContext &context, const Json &state) {
    const std::string saved = state.get("id").asString();
    if (!saved.empty() && saved != id) {
        auto &actors = context.services().get<ActorService>();
        actors.remove(entity().id(), id);
        id = actors.add(entity().id(), saved, entity().name());
    }
    if (state.get("faction").isString())
        faction = state.get("faction").asString();
    if (state.get("role").isString())
        role = state.get("role").asString();
    context.services().get<ActorService>().touch();
    return success();
}

std::string perceivedFaction(const Entity &subject) {
    if (const auto *effects = subject.get<StatusEffects>()) {
        const auto worn = effects->flagsWithPrefix("disguise.");
        if (!worn.empty())
            return worn.front();
    }
    const auto *identity = subject.get<Identity>();
    return identity ? identity->faction : std::string();
}

// ---- The service
// -----------------------------------------------------------------------------------------
std::string ActorService::add(EntityId entity, const std::string &wanted,
                              const std::string &entityName) {
    const std::string base = wanted.empty() ? "actor." + slug(entityName) : wanted;
    std::string id = base;
    if (byId_.contains(id)) {
        if (!wanted.empty())
            log(LogLevel::Warning, "actors",
                "Two characters claim the persistent id '" + wanted + "'; '" + entityName +
                    "' gets another one.");
        do {
            id = base + "." + std::to_string(++serial_);
        } while (byId_.contains(id));
    }
    byId_.emplace(id, entity);
    order_.push_back(entity);
    ++revision_;
    return id;
}

void ActorService::remove(EntityId entity, const std::string &persistentId) {
    const auto found = byId_.find(persistentId);
    if (found != byId_.end() && found->second == entity)
        byId_.erase(found);
    order_.erase(std::remove(order_.begin(), order_.end(), entity), order_.end());
    ++revision_;
}

EntityId ActorService::idOf(std::string_view persistentId) const {
    const auto found = byId_.find(persistentId);
    return found == byId_.end() ? EntityId{} : found->second;
}

Entity *ActorService::find(GameContext &context, std::string_view id) const {
    const EntityId entity = idOf(id);
    return entity ? context.scene().find(entity) : nullptr;
}

std::vector<Entity *> ActorService::inFaction(GameContext &context,
                                              std::string_view faction) const {
    std::vector<Entity *> list;
    for (const EntityId id : order_)
        if (Entity *entity = context.scene().find(id))
            if (const auto *who = entity->get<Identity>(); who && who->faction == faction)
                list.push_back(entity);
    return list;
}

std::vector<Entity *> ActorService::withRole(GameContext &context, std::string_view role) const {
    std::vector<Entity *> list;
    for (const EntityId id : order_)
        if (Entity *entity = context.scene().find(id))
            if (const auto *who = entity->get<Identity>(); who && who->role == role)
                list.push_back(entity);
    return list;
}

Json ActorService::saveState() const {
    Json state = Json::object();
    state.set("serial", std::to_string(serial_));
    return state;
}

Status ActorService::loadState(GameContext &, const Json &state) {
    try {
        const std::string saved = state.get("serial").asString();
        serial_ =
            std::max(serial_, static_cast<std::uint64_t>(std::stoull(saved.empty() ? "0" : saved)));
    } catch (const std::exception &) {
        return Error{"actors: the saved serial is not a number"};
    }
    return success();
}

void ActorService::describe(std::vector<std::pair<std::string, std::string>> &rows) const {
    rows.push_back({"Characters with an identity", std::to_string(order_.size())});
}

// ---- Rules
// ---------------------------------------------------------------------------------------
namespace {
std::optional<Value> identityFact(RuleContext &, Entity *subject, std::string_view rest) {
    const auto *who = subject ? subject->get<Identity>() : nullptr;
    if (!who)
        return std::nullopt;
    if (rest == "id")
        return Value{who->id};
    if (rest == "name")
        return Value{who->name()};
    if (rest == "faction")
        return Value{who->faction};
    if (rest == "role")
        return Value{who->role};
    if (rest == "perceivedFaction")
        return Value{perceivedFaction(*subject)};
    if (rest.starts_with("data.")) {
        auto value = valueFromJson(who->data.get(std::string(rest.substr(5))));
        return value ? std::optional<Value>(value.value()) : std::nullopt;
    }
    return std::nullopt;
}

// relationship.opinion.<id>, .trust.<id>, .hostility.<id>, .relation.<id>
std::optional<Value> relationshipFact(RuleContext &context, Entity *subject,
                                      std::string_view rest) {
    const auto *feelings = subject ? subject->get<Relationships>() : nullptr;
    const std::size_t dot = rest.find('.');
    if (dot == std::string_view::npos)
        return std::nullopt;
    const std::string_view what = rest.substr(0, dot);
    const std::string other(rest.substr(dot + 1));
    if (what == "relation") {
        const Entity *target =
            context.game.services().get<ActorService>().find(context.game, other);
        if (!subject || !target)
            return std::nullopt;
        return Value{std::string(relationName(relationBetween(context.game, *subject, *target)))};
    }
    if (!feelings)
        return std::nullopt;
    const PersonalRelation now = feelings->toward(other);
    if (what == "opinion")
        return Value{now.opinion};
    if (what == "trust")
        return Value{now.trust};
    if (what == "hostility")
        return Value{now.hostility};
    return std::nullopt;
}
} // namespace

void registerIdentityRules(RuleCatalog &catalog) {
    using Kind = ParamSpec::Kind;
    const auto param = [](const char *name, Kind kind, bool required = false,
                          const char *description = "") {
        return ParamSpec::make(name, kind, required, description);
    };
    catalog.addFacts("identity", identityFact, true);
    catalog.addFacts("relationship", relationshipFact, true);
    catalog.addPredicate({"HasRole",
                          "Factions",
                          "True when the entity has the role.",
                          {param("role", Kind::String, true),
                           param("entity", Kind::Entity, false, "default the actor")},
                          [](const Json &args, RuleContext &context) {
                              const Entity *who = context.resolveOne(
                                  args.contains("entity") ? args.get("entity").asString()
                                                          : "actor");
                              const auto *identity = who ? who->get<Identity>() : nullptr;
                              return identity && identity->role == args.get("role").asString();
                          },
                          nullptr});
    catalog.addAction({"SetFaction",
                       "Factions",
                       "Makes the entity a member of another faction (empty: of none).",
                       {param("entity", Kind::Entity, false, "default the actor"),
                        ParamSpec::make("faction", Kind::Ref, true, "", "faction")},
                       [](const Json &args, RuleContext &context) {
                           bool done = false;
                           for (Entity *entity : context.entitiesFrom(args, "entity", "actor"))
                               if (auto *identity = entity->get<Identity>()) {
                                   identity->setFaction(context.game,
                                                        args.get("faction").asString());
                                   done = true;
                               }
                           return done ? ActionResult::Done : ActionResult::Failed;
                       },
                       nullptr});
    catalog.addAction({"SetRole",
                       "Factions",
                       "Gives the entity another role.",
                       {param("entity", Kind::Entity, false, "default the actor"),
                        param("role", Kind::String, true)},
                       [](const Json &args, RuleContext &context) {
                           bool done = false;
                           for (Entity *entity : context.entitiesFrom(args, "entity", "actor"))
                               if (auto *identity = entity->get<Identity>()) {
                                   identity->setRole(context.game, args.get("role").asString());
                                   done = true;
                               }
                           return done ? ActionResult::Done : ActionResult::Failed;
                       },
                       nullptr});
}

void registerIdentityComponents(ComponentRegistry &registry) {
    registerIdentityRules(registry.extend<RuleCatalog>());
    registerFactionRules(registry.extend<RuleCatalog>());
    registry.add<Identity>("Identity");
    registry.add<Relationships>("Relationships");
}
} // namespace yk
