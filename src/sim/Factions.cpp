#include "yk/sim/Factions.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/sim/Identity.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
const char *relationName(Relation relation) {
    switch (relation) {
    case Relation::Friendly:
        return "friendly";
    case Relation::Neutral:
        return "neutral";
    case Relation::Suspicious:
        return "suspicious";
    case Relation::Hostile:
        return "hostile";
    }
    return "neutral";
}
const std::vector<std::string> &relationNames() {
    static const std::vector<std::string> names = {"friendly", "neutral", "suspicious", "hostile"};
    return names;
}
std::optional<Relation> parseRelation(std::string_view name) {
    for (const Relation relation :
         {Relation::Friendly, Relation::Neutral, Relation::Suspicious, Relation::Hostile})
        if (name == relationName(relation))
            return relation;
    return std::nullopt;
}

// ---- Definitions
// --------------------------------------------------------------------------------------
Result<FactionDefinition> FactionDefinition::fromJson(const Json &json,
                                                      std::vector<std::string> &warnings) {
    if (!json.isObject())
        return Error{"a faction must be an object"};
    FactionDefinition faction;
    auto id = data::requiredString(json, "id");
    if (!id)
        return Error{id.error()};
    faction.id = id.value();
    if (!data::validId(faction.id))
        return Error{"'" + faction.id + "' is not a usable id (letters, digits, '_' and '-')"};
    faction.name = data::optionalString(json, "name", faction.id);
    faction.description = data::optionalString(json, "description");
    faction.tags = data::stringList(json, "tags");
    if (json.contains("default")) {
        const auto parsed = parseRelation(json.get("default").asString());
        if (!parsed)
            return Error{"'default' must be friendly, neutral, suspicious or hostile"};
        faction.defaultRelation = *parsed;
    }
    if (json.contains("relations")) {
        if (!json.get("relations").isObject())
            return Error{"'relations' must be an object of faction id -> relation"};
        const Json &relations = json.get("relations");
        for (std::size_t i = 0; i < relations.size(); ++i) {
            const auto parsed = parseRelation(relations.valueAt(i).asString());
            if (!parsed)
                return Error{"the relation to '" + relations.keyAt(i) +
                             "' must be friendly, neutral, suspicious or hostile"};
            faction.relations[relations.keyAt(i)] = *parsed;
        }
    }
    data::warnUnknown(json, {"id", "name", "description", "default", "relations", "tags"},
                      warnings);
    return faction;
}

Json FactionDefinition::toJson() const {
    Json json = Json::object();
    json.set("id", id);
    json.set("name", name);
    if (!description.empty())
        json.set("description", description);
    json.set("default", relationName(defaultRelation));
    Json list = Json::object();
    for (const auto &[other, relation] : relations)
        list.set(other, relationName(relation));
    json.set("relations", std::move(list));
    if (!tags.empty())
        json.set("tags", data::toJsonList(tags));
    return json;
}

void FactionCatalog::load(const Json &document, const std::string &file,
                          std::vector<DataProblem> &problems) {
    if (document.contains("factions"))
        factions.load(document.get("factions"), file, problems, "faction");
}

void FactionCatalog::check(std::vector<DataProblem> &problems) const {
    for (const FactionDefinition &faction : factions.all())
        for (const auto &[other, relation] : faction.relations) {
            (void)relation;
            if (!factions.contains(other))
                problems.push_back({faction.file,
                                    "faction '" + faction.id + "' has a relation to '" + other +
                                        "', which is not a faction",
                                    true});
        }
}

Relation FactionCatalog::relation(std::string_view from, std::string_view to) const {
    const FactionDefinition *faction = from.empty() ? nullptr : factions.find(from);
    if (!faction)
        return Relation::Neutral;
    const auto found = faction->relations.find(std::string(to));
    if (found != faction->relations.end())
        return found->second;
    if (from == to)
        return Relation::Friendly;
    return faction->defaultRelation;
}

// ---- Personal feelings
// ----------------------------------------------------------------------------
namespace {
double clampOpinion(double value) {
    return std::clamp(value, -100.0, 100.0);
}
double clampMeter(double value) {
    return std::clamp(value, 0.0, 100.0);
}
Json feelingsToJson(const PersonalRelation &feelings) {
    Json json = Json::object();
    json.set("opinion", feelings.opinion);
    json.set("trust", feelings.trust);
    json.set("hostility", feelings.hostility);
    return json;
}
PersonalRelation feelingsFromJson(const Json &json) {
    PersonalRelation feelings;
    feelings.opinion = clampOpinion(json.get("opinion").asNumber(0.0));
    feelings.trust = clampMeter(json.get("trust").asNumber(0.0));
    feelings.hostility = clampMeter(json.get("hostility").asNumber(0.0));
    return feelings;
}
} // namespace

void Relationships::describe(TypeBuilder<Relationships> &type) {
    type.category("Simulation")
        .description("What this character personally feels about others (opinion, trust, "
                     "hostility), by their persistent id. Together with the factions' regard it "
                     "decides who they treat as friend, stranger, suspect or enemy.")
        .updatePhase(UpdatePhase::PostSimulation);
    type.field("friendlyAt", &Relationships::friendlyAt)
        .range(-100, 100, 1)
        .tooltip("Opinion at which they treat the other as a friend.");
    type.field("suspiciousAt", &Relationships::suspiciousAt)
        .range(-100, 100, 1)
        .tooltip("Opinion at or below which they are at least suspicious of the other.");
    type.field("hostileAt", &Relationships::hostileAt)
        .range(0, 100, 1)
        .tooltip("Hostility at or above which they treat the other as an enemy, whatever the "
                 "factions say.");
    type.field("forgetPerSecond", &Relationships::forgetPerSecond)
        .range(0, 1, 0.001)
        .tooltip("Feelings fade toward neutral by this fraction per second; 0: they last.");
    type.field("start", &Relationships::start)
        .tooltip("Starting feelings: {\"npc.warden\": {\"opinion\": 20, \"trust\": 5, "
                 "\"hostility\": 0}}.");
    type.check([](const Entity &, const Relationships &rel, const CheckContext &,
                  std::vector<std::string> &problems) {
        if (!rel.start.isObject() && !rel.start.isNull()) {
            problems.push_back("'start' must be an object of persistent id -> feelings");
            return;
        }
        for (std::size_t i = 0; i < rel.start.size(); ++i)
            if (!rel.start.valueAt(i).isObject())
                problems.push_back("the feelings for '" + rel.start.keyAt(i) +
                                   "' must be an object with opinion, trust and hostility");
    });
}

void Relationships::onStart(GameContext &) {
    feelings_.clear();
    if (!start.isObject())
        return;
    for (std::size_t i = 0; i < start.size(); ++i)
        if (start.valueAt(i).isObject())
            feelings_[start.keyAt(i)] = feelingsFromJson(start.valueAt(i));
}

void Relationships::onFixedUpdate(GameContext &, float seconds) {
    if (!(forgetPerSecond > 0.0F) || feelings_.empty())
        return;
    const double keep = std::pow(std::max(0.0, 1.0 - static_cast<double>(forgetPerSecond)),
                                 static_cast<double>(seconds));
    for (auto &[subject, feelings] : feelings_) {
        (void)subject;
        feelings.opinion *= keep;
        feelings.trust *= keep;
        feelings.hostility *= keep;
        if (std::fabs(feelings.opinion) < 0.01)
            feelings.opinion = 0.0;
        if (feelings.trust < 0.01)
            feelings.trust = 0.0;
        if (feelings.hostility < 0.01)
            feelings.hostility = 0.0;
    }
}

PersonalRelation Relationships::toward(const std::string &subject) const {
    const auto found = feelings_.find(subject);
    return found == feelings_.end() ? PersonalRelation{} : found->second;
}

void Relationships::announce(GameContext &context, const std::string &subject) {
    const PersonalRelation feelings = toward(subject);
    Json data = feelingsToJson(feelings);
    data.set("subject", subject);
    context.events().emit(GameEvent("relationship.changed", entity().id(),
                                    context.services().get<ActorService>().idOf(subject),
                                    std::move(data)));
}

bool Relationships::set(GameContext &context, const std::string &subject,
                        const PersonalRelation &feelings) {
    if (subject.empty())
        return false;
    PersonalRelation clamped{clampOpinion(feelings.opinion), clampMeter(feelings.trust),
                             clampMeter(feelings.hostility)};
    const PersonalRelation before = toward(subject);
    if (before.opinion == clamped.opinion && before.trust == clamped.trust &&
        before.hostility == clamped.hostility)
        return false;
    if (clamped.empty())
        feelings_.erase(subject);
    else
        feelings_[subject] = clamped;
    announce(context, subject);
    return true;
}

bool Relationships::change(GameContext &context, const std::string &subject, double opinion,
                           double trust, double hostility) {
    PersonalRelation now = toward(subject);
    now.opinion += opinion;
    now.trust += trust;
    now.hostility += hostility;
    return set(context, subject, now);
}

Relation Relationships::verdict(Relation byFaction, const std::string &subject) const {
    const PersonalRelation feelings = toward(subject);
    if (feelings.hostility >= hostileAt)
        return Relation::Hostile;
    Relation result = byFaction;
    if (feelings.opinion <= suspiciousAt && result < Relation::Suspicious)
        result = Relation::Suspicious;
    else if (feelings.opinion >= friendlyAt && result != Relation::Hostile)
        result = Relation::Friendly;
    return result;
}

Json Relationships::saveState() const {
    Json state = Json::object();
    Json list = Json::object();
    for (const auto &[subject, feelings] : feelings_)
        list.set(subject, feelingsToJson(feelings));
    state.set("feelings", std::move(list));
    return state;
}

Status Relationships::loadState(GameContext &, const Json &state) {
    feelings_.clear();
    const Json &list = state.get("feelings");
    if (!list.isObject())
        return success();
    for (std::size_t i = 0; i < list.size(); ++i)
        feelings_[list.keyAt(i)] = feelingsFromJson(list.valueAt(i));
    return success();
}

Relation relationBetween(GameContext &context, const Entity &observer, const Entity &subject,
                         bool perceived) {
    const auto *from = observer.get<Identity>();
    const auto *to = subject.get<Identity>();
    const std::string fromFaction = from ? from->faction : std::string();
    const std::string toFaction =
        perceived ? perceivedFaction(subject) : (to ? to->faction : std::string());
    const Relation base = gameData(context).factions.relation(fromFaction, toFaction);
    if (const auto *feelings = observer.get<Relationships>(); feelings && to)
        return feelings->verdict(base, to->id);
    return base;
}

// ---- Rules
// ---------------------------------------------------------------------------------------
void registerFactionRules(RuleCatalog &catalog) {
    using Kind = ParamSpec::Kind;
    const auto param = [](const char *name, Kind kind, bool required = false,
                          const char *description = "") {
        return ParamSpec::make(name, kind, required, description);
    };
    const auto relationParam = [&](const char *name, const char *description) {
        ParamSpec spec = param(name, Kind::Enum, false, description);
        spec.options = relationNames();
        return spec;
    };
    catalog.addPredicate(
        {"Relation",
         "Factions",
         "How the entity regards another, from their factions and personal feelings: it is one of "
         "friendly, neutral, suspicious, hostile (in that order of ill will).",
         {param("entity", Kind::Entity, false, "who regards: default self"),
          param("toward", Kind::Entity, false, "who is regarded: default the actor"),
          relationParam("is", "exactly this"), relationParam("atLeast", "this bad or worse"),
          relationParam("atMost", "this good or better"),
          param("perceived", Kind::Bool, false, "see the other as their clothing says")},
         [](const Json &args, RuleContext &context) {
             const Entity *observer = context.resolveOne(
                 args.contains("entity") ? args.get("entity").asString() : "self");
             const Entity *subject = context.resolveOne(
                 args.contains("toward") ? args.get("toward").asString() : "actor");
             if (!observer || !subject)
                 return false;
             const Relation relation = relationBetween(context.game, *observer, *subject,
                                                       args.get("perceived").asBool(false));
             if (const auto is = parseRelation(args.get("is").asString()); is && relation != *is)
                 return false;
             if (const auto least = parseRelation(args.get("atLeast").asString());
                 least && relation < *least)
                 return false;
             if (const auto most = parseRelation(args.get("atMost").asString());
                 most && relation > *most)
                 return false;
             return true;
         },
         [](const Json &args, RuleReport &report) {
             if (!args.contains("is") && !args.contains("atLeast") && !args.contains("atMost"))
                 report.warning("condition 'Relation' says nothing about which relation to test "
                                "('is', 'atLeast' or 'atMost'), so it is always true");
         }});
    catalog.addPredicate({"InFaction",
                          "Factions",
                          "True when the entity belongs to the faction (or looks like it does).",
                          {ParamSpec::make("faction", Kind::Ref, true, "", "faction"),
                           param("entity", Kind::Entity, false, "default the actor"),
                           param("perceived", Kind::Bool, false, "by their clothing")},
                          [](const Json &args, RuleContext &context) {
                              const Entity *who = context.resolveOne(
                                  args.contains("entity") ? args.get("entity").asString()
                                                          : "actor");
                              if (!who)
                                  return false;
                              const auto *identity = who->get<Identity>();
                              const std::string faction = args.get("perceived").asBool(false)
                                                              ? perceivedFaction(*who)
                                                              : (identity ? identity->faction : "");
                              return faction == args.get("faction").asString();
                          },
                          nullptr});
    catalog.addAction(
        {"ChangeRelationship",
         "Factions",
         "Changes what one character feels about another (added to what they feel now).",
         {param("entity", Kind::Entity, false, "who feels: default self"),
          param("toward", Kind::Entity, false, "about whom: default the actor"),
          param("opinion", Kind::Number), param("trust", Kind::Number),
          param("hostility", Kind::Number)},
         [](const Json &args, RuleContext &context) {
             const Entity *subject = context.resolveOne(
                 args.contains("toward") ? args.get("toward").asString() : "actor");
             const auto *identity = subject ? subject->get<Identity>() : nullptr;
             if (!identity)
                 return ActionResult::Failed;
             bool done = false;
             for (Entity *who : context.entitiesFrom(args, "entity", "self"))
                 if (auto *feelings = who->get<Relationships>())
                     done =
                         feelings->change(context.game, identity->id,
                                          toNumber(context.argument(args.get("opinion")), 0.0),
                                          toNumber(context.argument(args.get("trust")), 0.0),
                                          toNumber(context.argument(args.get("hostility")), 0.0)) ||
                         done;
             return done ? ActionResult::Done : ActionResult::Failed;
         },
         nullptr});
}
} // namespace yk
