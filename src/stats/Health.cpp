#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/stats/Stats.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
namespace {
const std::vector<std::string> &atZeroNames() {
    static const std::vector<std::string> names{"KnockOut", "Die", "Nothing"};
    return names;
}
} // namespace

void Health::describe(TypeBuilder<Health> &type) {
    type.category("Characters")
        .description(
            "A health model on top of a stat: damage with resistances by type, healing, an "
            "invulnerability window, and knocked out / dead when it runs out. Raises "
            "damaged, healed, knocked_out, recovering, recovered and died. Use Killable "
            "instead where one touch should end it.")
        .dependsOn("StatSet")
        .updatePhase(UpdatePhase::PreUpdate)
        .check([](const Entity &, const Health &health, const CheckContext &context,
                  std::vector<std::string> &problems) {
            if (context.known && !context.known("stat", health.stat))
                problems.push_back("its stat '" + health.stat +
                                   "' is not defined in the project's stats");
            if (!health.resistances.isObject())
                problems.push_back("'resistances' must be an object like {\"fire\": 0.5}");
            if (health.atZero == AtZero::KnockOut && health.knockedOutSeconds <= 0.0F)
                problems.push_back("is knocked out when it runs out but for no time at all");
        });
    type.field("stat", &Health::stat).ref("stat").tooltip("The stat that is its health.");
    type.field("atZero", &Health::atZero)
        .options(atZeroNames())
        .tooltip("What happens when the health reaches its lowest level.");
    type.field("downAt", &Health::downAt)
        .range(-100000, 100000, 1)
        .tooltip("The health at or below which the character goes down.");
    type.field("invulnerableSeconds", &Health::invulnerableSeconds)
        .range(0, 60, 0.05)
        .tooltip("Seconds after a hit during which it takes no damage.");
    type.field("resistances", &Health::resistances)
        .tooltip("Fraction of each damage type removed: {\"fire\": 0.5}. Negative takes extra.");
    type.field("knockedOutSeconds", &Health::knockedOutSeconds).range(0, 3600, 0.5);
    type.field("recoveringSeconds", &Health::recoveringSeconds).range(0, 600, 0.1);
    type.field("recoverFraction", &Health::recoverFraction).range(0, 1, 0.05);
    type.field("koEffect", &Health::koEffect)
        .ref("effect")
        .tooltip("The status effect applied while knocked out (empty: none).");
    type.field("destroyOnDeath", &Health::destroyOnDeath);
}

StatSet *Health::stats() const {
    return entity().get<StatSet>();
}
double Health::current() const {
    const StatSet *set = stats();
    return set ? set->value(stat) : 0.0;
}
double Health::maximum() const {
    const StatSet *set = stats();
    return set ? set->max(stat) : 0.0;
}

void Health::onStart(GameContext &) {
    state_ = State::Active;
    timer_ = 0.0;
    invulnerableUntil_ = -1.0;
}

bool Health::invulnerable(GameContext &context) const {
    return context.time() < invulnerableUntil_;
}

double Health::resisted(const DamageInfo &info) const {
    double amount = info.amount;
    if (!info.type.empty() && resistances.get(info.type).isNumber())
        amount *= 1.0 - std::min(resistances.get(info.type).asNumber(), 1.0);
    if (const auto *effects = entity().get<StatusEffects>())
        amount *= effects->factor("damage.taken");
    return std::max(amount, 0.0);
}

double Health::damage(GameContext &context, const DamageInfo &info) {
    StatSet *set = stats();
    if (!set || state_ == State::Dead || !(info.amount > 0.0))
        return 0.0;
    if (!info.ignoresInvulnerability && invulnerable(context))
        return 0.0;
    const double amount = resisted(info);
    if (amount <= 0.0)
        return 0.0;
    const double applied =
        -set->add(context, stat, -amount,
                  "damage:" + (info.type.empty() ? std::string("untyped") : info.type));
    invulnerableUntil_ = invulnerableSeconds > 0.0F
                             ? context.time() + static_cast<double>(invulnerableSeconds)
                             : -1.0;
    Json data = Json::object();
    data.set("amount", applied);
    data.set("type", info.type);
    data.set("health", set->value(stat));
    context.events().emit(GameEvent("damaged", entity().id(), info.source, std::move(data)));
    if (state_ == State::Active && set->value(stat) <= static_cast<double>(downAt) + 1e-9)
        goDown(context, info);
    return applied;
}

void Health::goDown(GameContext &context, const DamageInfo &info) {
    switch (atZero) {
    case AtZero::Nothing:
        return;
    case AtZero::KnockOut: {
        state_ = State::KnockedOut;
        timer_ = static_cast<double>(knockedOutSeconds);
        if (!koEffect.empty())
            if (auto *effects = entity().get<StatusEffects>())
                effects->apply(context, koEffect, 0.0, info.source);
        context.events().emit(GameEvent("knocked_out", entity().id(), info.source));
        return;
    }
    case AtZero::Die:
        state_ = State::Dead;
        context.events().emit(GameEvent("died", entity().id(), info.source));
        if (destroyOnDeath)
            context.destroyLater(entity().id());
        return;
    }
}

double Health::heal(GameContext &context, double amount, EntityId source) {
    StatSet *set = stats();
    if (!set || state_ == State::Dead || !(amount > 0.0))
        return 0.0;
    const double applied = set->add(context, stat, amount, "heal");
    if (applied > 0.0) {
        Json data = Json::object();
        data.set("amount", applied);
        data.set("health", set->value(stat));
        context.events().emit(GameEvent("healed", entity().id(), source, std::move(data)));
    }
    return applied;
}

void Health::wake(GameContext &context) {
    state_ = State::Active;
    timer_ = 0.0;
    if (!koEffect.empty())
        if (auto *effects = entity().get<StatusEffects>())
            effects->remove(context, koEffect);
    context.events().emit(GameEvent("recovered", entity().id()));
}

void Health::revive(GameContext &context, double fraction) {
    StatSet *set = stats();
    if (!set || state_ == State::Active)
        return;
    const double target = std::max(set->min(stat) + (set->max(stat) - set->min(stat)) *
                                                        std::clamp(fraction, 0.0, 1.0),
                                   static_cast<double>(downAt) + 1.0);
    set->set(context, stat, std::min(target, set->max(stat)), "revive");
    wake(context);
}

void Health::onFixedUpdate(GameContext &context, float seconds) {
    if (state_ != State::KnockedOut && state_ != State::Recovering)
        return;
    timer_ -= static_cast<double>(seconds);
    if (timer_ > 0.0)
        return;
    if (state_ == State::KnockedOut) {
        state_ = State::Recovering;
        timer_ = static_cast<double>(recoveringSeconds);
        context.events().emit(GameEvent("recovering", entity().id()));
        if (timer_ > 0.0)
            return;
    }
    // Wakes with some health back, so the next touch does not put it straight down again.
    if (StatSet *set = stats()) {
        const double target = set->min(stat) + (set->max(stat) - set->min(stat)) *
                                                   static_cast<double>(recoverFraction);
        if (set->value(stat) < target)
            set->set(context, stat, std::max(target, static_cast<double>(downAt) + 1.0),
                     "recovery");
    }
    wake(context);
}

Json Health::saveState() const {
    Json state = Json::object();
    state.set("state", static_cast<int>(state_));
    state.set("timer", timer_);
    return state;
}
Status Health::loadState(GameContext &, const Json &state) {
    const auto value = state.get("state").asInt(0);
    if (value < 0 || value > static_cast<int>(State::Dead))
        return Error{"health: the saved state is not valid"};
    state_ = static_cast<State>(value);
    timer_ = state.get("timer").asNumber(0.0);
    invulnerableUntil_ = -1.0;
    return success();
}
} // namespace yk
