#pragma once
#include "yk/data/Table.hpp"
#include "yk/rules/Rules.hpp"
#include "yk/scene/Registry.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

// How groups regard each other, and how individuals do. A project's data names its factions and
// says how each regards the others; nothing here knows what a "guard" is. A faction's regard is one
// of four steps (friendly, neutral, suspicious, hostile) and is not necessarily mutual: guards may
// be suspicious of inmates who are merely neutral toward them. Individuals can feel otherwise
// (a guard who likes one inmate, an inmate who was beaten up by one guard): the Relationships
// component keeps that personal state by persistent identity, so it survives a save and a change of
// scene, and turns the two into the one answer that AI, dialogue, trade and law enforcement ask
// for.
//
//   "factions": [
//     {"id": "guards", "name": "Guards", "default": "neutral",
//      "relations": {"inmates": "suspicious", "guards": "friendly"}},
//     {"id": "inmates", "name": "Inmates", "relations": {"medics": "friendly"}}]
namespace yk {
class GameContext;
class GameData;

enum class Relation { Friendly, Neutral, Suspicious, Hostile };
const char *relationName(Relation relation);
std::optional<Relation> parseRelation(std::string_view name);
const std::vector<std::string> &relationNames(); // friendly, neutral, suspicious, hostile

struct FactionDefinition {
    std::string id;
    std::string file;
    std::string name;
    std::string description;
    Relation defaultRelation{Relation::Neutral}; // Toward a faction it does not mention.
    std::map<std::string, Relation> relations;   // Faction id -> how this one regards it.
    std::vector<std::string> tags;

    static Result<FactionDefinition> fromJson(const Json &json, std::vector<std::string> &warnings);
    Json toJson() const;
};
using FactionTable = DefinitionTable<FactionDefinition>;

struct FactionCatalog {
    FactionTable factions;
    void load(const Json &document, const std::string &file, std::vector<DataProblem> &problems);
    // Relations that name factions nobody defined.
    void check(std::vector<DataProblem> &problems) const;
    // How `from` regards `to`: what its definition says, else its default; members of one faction
    // are friendly to each other unless the definition says otherwise; no faction (or an unknown
    // one) is neutral toward everyone.
    Relation relation(std::string_view from, std::string_view to) const;
};

// What one character feels about another, by the other's persistent id.
struct PersonalRelation {
    double opinion{0.0};   // -100 loathing .. +100 devoted
    double trust{0.0};     // 0 .. 100: would believe them, lend them things
    double hostility{0.0}; // 0 .. 100: wants to harm them
    bool empty() const {
        return opinion == 0.0 && trust == 0.0 && hostility == 0.0;
    }
};

// A character's personal feelings, and the verdict of faction and feelings together:
//   hostility at or above `hostileAt`            -> hostile, whatever their factions say
//   opinion at or below `suspiciousAt`           -> at least suspicious
//   opinion at or above `friendlyAt` (and not hostile) -> friendly
//   otherwise                                    -> what the factions say
// Raises relationship.changed (source: this character; data: subject, opinion, trust, hostility)
// whenever something changes.
class Relationships final : public Component {
  public:
    double friendlyAt{50.0};
    double suspiciousAt{-30.0};
    double hostileAt{60.0};
    // Feelings fade toward neutral: the fraction lost per second (0.5: half is gone after a
    // second, 0: they last).
    float forgetPerSecond{0.0F};
    Json start{Json::object()}; // {"npc.warden": {"opinion": 20, "trust": 5, "hostility": 0}}
    static void describe(TypeBuilder<Relationships> &type);

    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;

    PersonalRelation toward(const std::string &subject) const;
    // Adds to what is felt (each limited to its range); true when anything changed.
    bool change(GameContext &context, const std::string &subject, double opinion, double trust,
                double hostility);
    bool set(GameContext &context, const std::string &subject, const PersonalRelation &feelings);
    const std::map<std::string, PersonalRelation> &all() const {
        return feelings_;
    }
    // The verdict for `subject` given what their factions say.
    Relation verdict(Relation byFaction, const std::string &subject) const;

  private:
    void announce(GameContext &context, const std::string &subject);
    std::map<std::string, PersonalRelation> feelings_;
};

// How `observer` regards `subject`: their factions (the subject's as the observer perceives it when
// `perceived`, which a disguise changes) and the observer's personal feelings, if it keeps any.
Relation relationBetween(GameContext &context, const Entity &observer, const Entity &subject,
                         bool perceived = false);

void registerFactionRules(RuleCatalog &catalog);
} // namespace yk
