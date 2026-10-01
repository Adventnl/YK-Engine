#pragma once
#include "yk/core/Json.hpp"
#include "yk/core/Result.hpp"
#include "yk/data/Table.hpp"
#include "yk/rules/Rules.hpp"
#include "yk/scene/Registry.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

// Numbers that characters have and that change: health, stamina, strength, hunger, money, heat. The
// engine knows none of these names: a project's data file lists the stats it uses (with their
// range, start, regeneration and the levels that raise events), and everything else (items that
// add to strength, effects that drain stamina, doors that need intellect, a heads-up display)
// refers to them by id.
namespace yk {
class GameContext;

// ---- Definitions --------------------------------------------------------------------------------
// Which number a modifier changes: the value itself, or the limits and the pace around it.
enum class StatChannel { Current, Max, Min, Regen };
const std::vector<std::string> &statChannelNames(); // "value", "max", "min", "regen"

struct StatThreshold {
    double at{0};
    bool below{true};  // Raised when the value falls below `at` (else: rises above it).
    std::string event; // The game event raised; its data names the stat, the value and the level.
};

struct StatDefinition {
    std::string id;
    std::string file; // Where it was defined.
    std::string name;
    std::string category; // For grouping in lists ("vital", "attribute", "economy").
    std::string description;
    double min{0};
    double max{100};
    double start{0};       // Where it begins, when it does not begin full.
    bool startsFull{true}; // "start": "max" (the default) in the file.
    double regen{0};       // Per second; negative drains (hunger, a heat that fades).
    double regenDelay{0};  // Seconds after the stat last went down before regen resumes.
    bool integer{false};   // Whole numbers only (money): changes are rounded.
    std::vector<StatThreshold> thresholds;

    double startValue() const {
        return startsFull ? max : start;
    }
    static Result<StatDefinition> fromJson(const Json &json, std::vector<std::string> &warnings);
    Json toJson() const;
};

// What an item, an effect or a piece of equipment says it does to a stat.
struct StatModifierSpec {
    std::string stat;
    StatChannel channel{StatChannel::Current};
    double add{0};  // Summed with the other adds first,
    double mult{1}; // then the result is multiplied by every mult.
    static Result<StatModifierSpec> fromJson(const Json &json);
    Json toJson() const;
};

// A modifier at work on one character: who put it there (so it can be taken away again as a
// group: "equip:Weapon", "effect:poisoned") and when it ends.
struct StatModifier {
    StatModifierSpec spec;
    std::string source;
    double expires{-1.0}; // Game time it ends at; negative: until removed.
};

using StatTable = DefinitionTable<StatDefinition>;

// ---- Status effects -----------------------------------------------------------------------------
// A temporary or permanent condition on a character: stunned, poisoned, hidden, exhausted, wanted.
// It may change stats (modifiers, damage over time), carry flags that other systems ask about
// ("no_move", "no_attack", "invisible"), scale named factors ("move.speed": 0.5), grant
// permission tokens, play an animation, and run rule actions when it starts and ends. The engine
// reads none of the flags and factors itself except where it says so; movement, perception,
// combat and interaction code asks the character's StatusEffects.
struct EffectDefinition {
    enum class Stacking { Refresh, Stack, Extend, Ignore };
    std::string id;
    std::string file;
    std::string name;
    std::string description;
    std::string icon;
    double duration{0}; // Seconds; 0 lasts until removed.
    Stacking stacking{Stacking::Refresh};
    int maxStacks{1};
    std::vector<StatModifierSpec> modifiers; // Scaled by the number of stacks.
    struct Tick {
        std::string stat;
        double amount{0}; // Added to the stat each time (scaled by stacks).
        double interval{1.0};
        std::string source; // Damage type, for health ticks ("poison"); informational.
    };
    std::vector<Tick> ticks;
    std::vector<std::string> flags;
    std::map<std::string, double> factors;
    std::vector<std::string> grants;
    std::vector<std::string> tags; // For finding groups: "debuff", "harmful", "poison".
    std::string animation;         // An animation trigger raised on the character when it starts.
    std::vector<Action> onApply;
    std::vector<Action> onExpire;
    bool hidden{false}; // Not shown in the effects list of a heads-up display.

    static Result<EffectDefinition> fromJson(const Json &json, std::vector<std::string> &warnings);
    Json toJson() const;
};
using EffectTable = DefinitionTable<EffectDefinition>;
const std::vector<std::string> &stackingNames(); // "refresh", "stack", "extend", "ignore"

// The stat and effect definitions of the project, with the sections of a data file that hold them.
struct StatCatalog {
    StatTable stats;
    EffectTable effects;
    // Reads the "stats" and "effects" sections of a data file, when it has them.
    void load(const Json &document, const std::string &file, std::vector<DataProblem> &problems);
    // Effects whose modifiers and ticks name stats that do not exist.
    void check(std::vector<DataProblem> &problems) const;
    // Hands the validator every list of actions the definitions hold (an effect's onApply...).
    void visitRules(const RuleSourceVisitor &visit) const;
};

// ---- Components ---------------------------------------------------------------------------------
// The stats of one character. Every stat the project defines exists on it; `start` gives this
// character's own starting values ({"strength": 40}). Changes raise events:
//   stat.changed  (source: the character; data: stat, from, to, delta, cause) for deliberate
//   changes, and the events of the stat's thresholds, "stat.depleted" and "stat.full" when it hits
//   a limit.
// Passive regeneration and decay raise only the threshold and limit events.
class StatSet final : public Component {
  public:
    Json start{Json::object()};
    static void describe(TypeBuilder<StatSet> &type);

    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;

    bool has(std::string_view stat) const;
    // The number gameplay reads: base plus modifiers, within the limits.
    double value(std::string_view stat) const;
    double base(std::string_view stat) const;
    double max(std::string_view stat) const;
    double min(std::string_view stat) const;
    double regen(std::string_view stat) const;
    double fraction(std::string_view stat) const; // 0..1 across the range.
    std::vector<std::string> ids() const;

    // Changes the stored number by `delta` (damage, spending, healing) and returns how much the
    // value really changed after the limits. `cause` names why ("damage:fire", "item:coffee").
    // `quiet` leaves out the stat.changed event of this change (a drain that happens every tick,
    // like a sprint, would raise sixty a second); thresholds and the limits still raise theirs.
    double add(GameContext &context, std::string_view stat, double delta,
               const std::string &cause = {}, bool quiet = false);
    double set(GameContext &context, std::string_view stat, double value,
               const std::string &cause = {});
    // Spends `amount` when there is that much (stamina for a sprint); false and unchanged
    // otherwise.
    bool spend(GameContext &context, std::string_view stat, double amount,
               const std::string &cause = {}, bool quiet = false);
    bool canSpend(std::string_view stat, double amount) const;

    // Modifiers: `source` groups them so a whole group can be removed at once. A modifier with a
    // duration ends by itself.
    void addModifier(GameContext &context, StatModifierSpec spec, const std::string &source,
                     double durationSeconds = 0.0);
    std::size_t removeModifiers(GameContext &context, const std::string &source);
    const std::vector<StatModifier> &modifiers() const {
        return modifiers_;
    }
    std::uint64_t revision() const {
        return revision_;
    }

  private:
    struct Entry {
        double base{0};
        double lastReduced{-1.0e9}; // Game time of the last drop (for the regeneration delay).
        double lastValue{0};        // For threshold and limit events.
    };
    double channel(std::string_view stat, StatChannel which, double baseline) const;
    void settle(GameContext &context, const StatDefinition &definition, Entry &entry, double before,
                const std::string &cause, bool deliberate);
    const StatCatalog *catalog(GameContext &context) const;

    std::map<std::string, Entry, std::less<>> entries_;
    std::vector<StatModifier> modifiers_;
    const StatCatalog *catalog_{nullptr};
    std::uint64_t revision_{0};
    double now_{0.0};
    friend class StatusEffects;
};

// The status effects on a character.
class StatusEffects final : public Component {
  public:
    std::vector<std::string> initial;    // Effects that are on it from the start.
    std::vector<std::string> immunities; // Effect ids or tags it cannot receive.
    static void describe(TypeBuilder<StatusEffects> &type);

    struct Active {
        std::string id;
        double remaining{0}; // Seconds left; 0 with an unlimited effect.
        double duration{0};
        int stacks{1};
        EntityId source{}; // Who applied it.
        double nextTick{0};
        std::vector<double> tickClocks;
    };

    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onDestroy(GameContext &context) override;
    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;

    // Starts an effect; a duration of 0 uses the definition's. False when the effect does not
    // exist, the character is immune, or it is already on and the effect says to ignore a repeat.
    bool apply(GameContext &context, const std::string &id, double durationSeconds = 0.0,
               EntityId source = {});
    bool remove(GameContext &context, const std::string &id);
    std::size_t removeTagged(GameContext &context, const std::string &tag);
    bool has(std::string_view id) const;
    bool hasTag(std::string_view tag) const;
    int stacks(std::string_view id) const;
    // True while any active effect carries the flag ("no_move").
    bool hasFlag(std::string_view flag) const;
    // The flags of the active effects that start with `prefix`, without it ("disguise.guard" ->
    // "guard"), each once, in the order the effects came on.
    std::vector<std::string> flagsWithPrefix(std::string_view prefix) const;
    // The product of the named factor of every active effect (1 when none sets it).
    double factor(std::string_view key) const;
    // True while an effect grants the permission token.
    bool grants(std::string_view token) const;
    const std::vector<Active> &active() const {
        return active_;
    }
    std::uint64_t revision() const {
        return revision_;
    }

  private:
    const EffectDefinition *definition(GameContext &context, const std::string &id) const;
    void begin(GameContext &context, Active &effect, const EffectDefinition &def);
    void end(GameContext &context, const Active &effect, const char *how);
    void setModifiers(GameContext &context, const Active &effect, const EffectDefinition &def);

    std::vector<Active> active_;
    const StatCatalog *catalog_{nullptr};
    std::uint64_t revision_{0};
};

// What happened to a character that was hurt: who did it, what kind of harm, and how it was meant
// to be delivered.
struct DamageInfo {
    double amount{0};
    std::string type;  // "blunt", "fire", ... (the character's resistances are by type)
    EntityId source{}; // The attacker (null for a trap or a fall).
    Vec2 direction{};  // From the attacker toward the victim; for knockback.
    double knockback{0};
    bool blockable{true}; // A blocking character may reduce or stop it.
    bool ignoresInvulnerability{false};
};

// Health, damage and healing on top of a stat (default "health"): an invulnerability window after
// a hit, resistances and weaknesses by damage type, and what happens when it runs out: the
// character is knocked out (and wakes again), dies, or nothing (the game decides). The knockout
// flows Active -> KnockedOut -> Recovering -> Active. Raises "damaged", "healed", "knocked_out",
// "recovering", "recovered" and "died" (source: the character; other: the attacker). `Killable`
// stays for games where one touch ends it.
class Health final : public Component {
  public:
    enum class AtZero { KnockOut, Die, Nothing };
    enum class State { Active, KnockedOut, Recovering, Dead };
    std::string stat{"health"};
    AtZero atZero{AtZero::Die};
    float downAt{0.0F};              // The health at or below which the character goes down.
    float invulnerableSeconds{0.0F}; // After each hit.
    Json resistances{
        Json::object()}; // {"fire": 0.5, "blunt": -0.25}: fraction removed (negative: extra).
    float knockedOutSeconds{20.0F};
    float recoveringSeconds{1.5F};
    float recoverFraction{0.3F}; // Of the maximum health, when it wakes.
    std::string koEffect{
        "knocked_out"}; // The status effect applied while down (skipped if undefined).
    bool destroyOnDeath{false};
    static void describe(TypeBuilder<Health> &type);

    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;

    // Applies the damage after resistances; returns the amount that took effect (0 when it was
    // ignored: invulnerable, already dead).
    double damage(GameContext &context, const DamageInfo &info);
    double heal(GameContext &context, double amount, EntityId source = {});
    State state() const {
        return state_;
    }
    bool alive() const {
        return state_ != State::Dead;
    }
    // Able to act: not knocked out, recovering or dead.
    bool active() const {
        return state_ == State::Active;
    }
    bool knockedOut() const {
        return state_ == State::KnockedOut || state_ == State::Recovering;
    }
    bool invulnerable(GameContext &context) const;
    // Brings a character that is down (or dead) back, with `fraction` of its maximum health.
    void revive(GameContext &context, double fraction = 0.25);
    double current() const;
    double maximum() const;
    // What a hit of this kind would really take off after resistances and effects.
    double resisted(const DamageInfo &info) const;

  private:
    StatSet *stats() const;
    void goDown(GameContext &context, const DamageInfo &info);
    void wake(GameContext &context);
    State state_{State::Active};
    double timer_{0.0};
    double invulnerableUntil_{-1.0};
};

// Adds the stat predicates, actions and facts to the rule catalog.
void registerStatRules(RuleCatalog &catalog);
void registerStatComponents(ComponentRegistry &registry);
} // namespace yk
