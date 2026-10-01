#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/stats/Stats.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
namespace {
Json effectData(const std::string &effect, int stacks) {
    Json data = Json::object();
    data.set("effect", effect);
    data.set("stacks", stacks);
    return data;
}
} // namespace

void StatusEffects::describe(TypeBuilder<StatusEffects> &type) {
    type.category("Characters")
        .description("The status effects on a character: stunned, poisoned, hidden, exhausted... "
                     "Effects are defined in the project's data files; they change stats, carry "
                     "flags and factors other systems ask about, and may run actions.")
        .dependsOn("StatSet")
        .updatePhase(UpdatePhase::PreUpdate);
    type.field("initial", &StatusEffects::initial).ref("effect").tooltip("Effects it starts with.");
    type.field("immunities", &StatusEffects::immunities)
        .tooltip("Effect ids or effect tags it can never receive (\"poison\", \"stun\").");
    type.check([](const Entity &, const StatusEffects &effects, const CheckContext &context,
                  std::vector<std::string> &problems) {
        if (!context.known)
            return;
        for (const std::string &id : effects.initial)
            if (!context.known("effect", id))
                problems.push_back("starts with the effect '" + id + "', which is not defined");
    });
}

const EffectDefinition *StatusEffects::definition(GameContext &context,
                                                  const std::string &id) const {
    const StatCatalog &catalog = gameData(context).stats;
    return catalog.effects.find(id);
}

void StatusEffects::onStart(GameContext &context) {
    catalog_ = &gameData(context).stats;
    for (const std::string &id : initial)
        apply(context, id);
}

void StatusEffects::onDestroy(GameContext &) {
    active_.clear();
}

bool StatusEffects::has(std::string_view id) const {
    return std::any_of(active_.begin(), active_.end(),
                       [&](const Active &effect) { return effect.id == id; });
}
int StatusEffects::stacks(std::string_view id) const {
    for (const Active &effect : active_)
        if (effect.id == id)
            return effect.stacks;
    return 0;
}
bool StatusEffects::hasTag(std::string_view tag) const {
    if (!catalog_)
        return false;
    for (const Active &effect : active_)
        if (const EffectDefinition *def = catalog_->effects.find(effect.id))
            if (data::has(def->tags, tag))
                return true;
    return false;
}
bool StatusEffects::hasFlag(std::string_view flag) const {
    if (!catalog_)
        return false;
    for (const Active &effect : active_)
        if (const EffectDefinition *def = catalog_->effects.find(effect.id))
            if (data::has(def->flags, flag))
                return true;
    return false;
}
bool StatusEffects::grants(std::string_view token) const {
    if (!catalog_)
        return false;
    for (const Active &effect : active_)
        if (const EffectDefinition *def = catalog_->effects.find(effect.id))
            if (data::has(def->grants, token))
                return true;
    return false;
}
double StatusEffects::factor(std::string_view key) const {
    double product = 1.0;
    if (!catalog_)
        return product;
    for (const Active &effect : active_)
        if (const EffectDefinition *def = catalog_->effects.find(effect.id)) {
            const auto found = def->factors.find(std::string(key));
            if (found != def->factors.end())
                product *= std::pow(found->second, effect.stacks);
        }
    return product;
}

void StatusEffects::setModifiers(GameContext &context, const Active &effect,
                                 const EffectDefinition &def) {
    auto *stats = entity().get<StatSet>();
    if (!stats)
        return;
    const std::string source = "effect:" + def.id;
    stats->removeModifiers(context, source);
    for (const StatModifierSpec &spec : def.modifiers) {
        StatModifierSpec scaled = spec;
        scaled.add *= effect.stacks;
        scaled.mult = std::pow(scaled.mult, effect.stacks);
        stats->addModifier(context, scaled, source);
    }
}

void StatusEffects::begin(GameContext &context, Active &effect, const EffectDefinition &def) {
    effect.tickClocks.assign(def.ticks.size(), 0.0);
    setModifiers(context, effect, def);
    if (!def.animation.empty())
        if (auto *animated = entity().get<AnimatedSprite>())
            animated->trigger(def.animation);
    if (!def.onApply.empty()) {
        RuleContext rules(context);
        rules.self = entity().id();
        rules.actor = effect.source ? effect.source : entity().id();
        rules.target = entity().id();
        rules.origin = "effect '" + def.id + "' on '" + entity().name() + "'";
        execute(def.onApply, rules);
    }
}

bool StatusEffects::apply(GameContext &context, const std::string &id, double durationSeconds,
                          EntityId source) {
    const EffectDefinition *def = definition(context, id);
    if (!def)
        return false;
    catalog_ = &gameData(context).stats;
    for (const std::string &immunity : immunities)
        if (immunity == id || data::has(def->tags, immunity))
            return false;
    const double duration = durationSeconds > 0.0 ? durationSeconds : def->duration;
    for (Active &existing : active_) {
        if (existing.id != id)
            continue;
        switch (def->stacking) {
        case EffectDefinition::Stacking::Ignore:
            return false;
        case EffectDefinition::Stacking::Refresh:
            existing.remaining = duration;
            existing.duration = duration;
            break;
        case EffectDefinition::Stacking::Extend:
            if (existing.duration > 0.0)
                existing.remaining += duration;
            break;
        case EffectDefinition::Stacking::Stack:
            existing.stacks = std::min(existing.stacks + 1, def->maxStacks);
            existing.remaining = duration;
            existing.duration = duration;
            setModifiers(context, existing, *def);
            break;
        }
        existing.source = source;
        ++revision_;
        context.events().emit(
            GameEvent("effect.applied", entity().id(), source, effectData(id, existing.stacks)));
        return true;
    }
    Active effect;
    effect.id = id;
    effect.duration = duration;
    effect.remaining = duration;
    effect.source = source;
    begin(context, effect, *def);
    active_.push_back(std::move(effect));
    ++revision_;
    context.events().emit(GameEvent("effect.applied", entity().id(), source, effectData(id, 1)));
    return true;
}

void StatusEffects::end(GameContext &context, const Active &effect, const char *how) {
    const EffectDefinition *def = definition(context, effect.id);
    if (auto *stats = entity().get<StatSet>())
        stats->removeModifiers(context, "effect:" + effect.id);
    context.events().emit(GameEvent(std::string("effect.") + how, entity().id(), {},
                                    effectData(effect.id, effect.stacks)));
    if (def && std::string_view(how) == "expired" && !def->onExpire.empty()) {
        RuleContext rules(context);
        rules.self = entity().id();
        rules.actor = entity().id();
        rules.target = entity().id();
        rules.origin = "effect '" + def->id + "' on '" + entity().name() + "'";
        execute(def->onExpire, rules);
    }
}

bool StatusEffects::remove(GameContext &context, const std::string &id) {
    const auto found = std::find_if(active_.begin(), active_.end(),
                                    [&](const Active &effect) { return effect.id == id; });
    if (found == active_.end())
        return false;
    const Active gone = *found;
    active_.erase(found);
    ++revision_;
    end(context, gone, "removed");
    return true;
}

std::size_t StatusEffects::removeTagged(GameContext &context, const std::string &tag) {
    std::vector<std::string> doomed;
    if (catalog_)
        for (const Active &effect : active_)
            if (const EffectDefinition *def = catalog_->effects.find(effect.id))
                if (data::has(def->tags, tag))
                    doomed.push_back(effect.id);
    for (const std::string &id : doomed)
        remove(context, id);
    return doomed.size();
}

void StatusEffects::onFixedUpdate(GameContext &context, float seconds) {
    if (active_.empty())
        return;
    const double dt = static_cast<double>(seconds);
    auto *stats = entity().get<StatSet>();
    std::vector<Active> expired;
    for (Active &effect : active_) {
        const EffectDefinition *def = definition(context, effect.id);
        if (!def)
            continue;
        for (std::size_t i = 0; i < def->ticks.size(); ++i) {
            effect.tickClocks[i] += dt;
            while (effect.tickClocks[i] + 1e-9 >= def->ticks[i].interval) {
                effect.tickClocks[i] -= def->ticks[i].interval;
                if (!stats)
                    continue;
                const EffectDefinition::Tick &tick = def->ticks[i];
                const double amount = tick.amount * effect.stacks;
                if (auto *health = entity().get<Health>();
                    health && tick.stat == health->stat && amount < 0.0) {
                    DamageInfo info;
                    info.amount = -amount;
                    info.type = tick.source;
                    info.source = effect.source;
                    info.blockable = false;
                    info.ignoresInvulnerability = true;
                    health->damage(context, info);
                } else {
                    stats->add(context, tick.stat, amount, "effect:" + effect.id);
                }
            }
        }
        if (effect.duration > 0.0) {
            effect.remaining -= dt;
            if (effect.remaining <= 1e-9)
                expired.push_back(effect);
        }
    }
    for (const Active &effect : expired) {
        active_.erase(
            std::remove_if(active_.begin(), active_.end(),
                           [&](const Active &candidate) { return candidate.id == effect.id; }),
            active_.end());
        ++revision_;
        end(context, effect, "expired");
    }
}

Json StatusEffects::saveState() const {
    Json list = Json::array();
    for (const Active &effect : active_) {
        Json item = Json::object();
        item.set("effect", effect.id);
        item.set("remaining", effect.remaining);
        item.set("duration", effect.duration);
        item.set("stacks", effect.stacks);
        if (effect.source)
            item.set("source", toString(effect.source));
        Json clocks = Json::array();
        for (const double clock : effect.tickClocks)
            clocks.push(clock);
        item.set("clocks", clocks);
        list.push(item);
    }
    Json state = Json::object();
    state.set("active", list);
    return state;
}

Status StatusEffects::loadState(GameContext &context, const Json &state) {
    catalog_ = &gameData(context).stats;
    auto *stats = entity().get<StatSet>();
    if (stats)
        for (const Active &effect : active_)
            stats->removeModifiers(context, "effect:" + effect.id);
    active_.clear();
    const Json &list = state.get("active");
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Json &item = list.at(i);
        const EffectDefinition *def = definition(context, item.get("effect").asString());
        if (!def) {
            log(LogLevel::Warning, "effects",
                "a saved effect '" + item.get("effect").asString() +
                    "' no longer exists and was dropped");
            continue;
        }
        Active effect;
        effect.id = def->id;
        effect.remaining = item.get("remaining").asNumber(0.0);
        effect.duration = item.get("duration").asNumber(0.0);
        effect.stacks =
            std::clamp(static_cast<int>(item.get("stacks").asInt(1)), 1, def->maxStacks);
        if (const auto source = parseEntityId(item.get("source").asString()))
            effect.source = *source;
        effect.tickClocks.assign(def->ticks.size(), 0.0);
        for (std::size_t k = 0; k < effect.tickClocks.size() && k < item.get("clocks").size(); ++k)
            effect.tickClocks[k] = item.get("clocks").at(k).asNumber();
        setModifiers(context, effect, *def);
        active_.push_back(std::move(effect));
    }
    ++revision_;
    return success();
}
} // namespace yk
