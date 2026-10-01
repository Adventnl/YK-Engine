#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/stats/Stats.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
namespace {
constexpr double epsilon = 1.0e-9;

Json statData(const std::string &stat, double from, double to, const std::string &cause) {
    Json data = Json::object();
    data.set("stat", stat);
    data.set("from", from);
    data.set("to", to);
    data.set("delta", to - from);
    if (!cause.empty())
        data.set("cause", cause);
    return data;
}
} // namespace

void StatSet::describe(TypeBuilder<StatSet> &type) {
    type.category("Characters")
        .description("The numbers a character has (health, stamina, strength, money...), as the "
                     "project's stats file defines them. 'start' gives this character's own "
                     "starting values.")
        .updatePhase(UpdatePhase::PreUpdate);
    type.field("start", &StatSet::start)
        .tooltip("This character's starting values, like {\"strength\": 40, \"money\": 25}. "
                 "Stats not named here begin as their definition says.");
    type.check([](const Entity &, const StatSet &set, const CheckContext &context,
                  std::vector<std::string> &problems) {
        if (!set.start.isObject() && !set.start.isNull()) {
            problems.push_back("'start' must be an object of stat values like {\"strength\": 40}");
            return;
        }
        for (std::size_t i = 0; i < set.start.size(); ++i) {
            if (!set.start.valueAt(i).isNumber())
                problems.push_back("the starting value of '" + set.start.keyAt(i) +
                                   "' is not a number");
            else if (context.known && !context.known("stat", set.start.keyAt(i)))
                problems.push_back("'start' names the stat '" + set.start.keyAt(i) +
                                   "', which is not defined");
        }
    });
}

const StatCatalog *StatSet::catalog(GameContext &context) const {
    return &gameData(context).stats;
}

void StatSet::onStart(GameContext &context) {
    catalog_ = catalog(context);
    now_ = context.time();
    entries_.clear();
    for (const StatDefinition &definition : catalog_->stats.all()) {
        Entry entry;
        entry.base = definition.startValue();
        if (start.isObject() && start.get(definition.id).isNumber())
            entry.base =
                std::clamp(start.get(definition.id).asNumber(), definition.min, definition.max);
        if (definition.integer)
            entry.base = std::round(entry.base);
        entries_[definition.id] = entry;
    }
    for (auto &[id, entry] : entries_)
        entry.lastValue = value(id);
}

double StatSet::channel(std::string_view stat, StatChannel which, double baseline) const {
    double added = baseline, multiplied = 1.0;
    for (const StatModifier &modifier : modifiers_)
        if (modifier.spec.stat == stat && modifier.spec.channel == which) {
            added += modifier.spec.add;
            multiplied *= modifier.spec.mult;
        }
    return added * multiplied;
}

bool StatSet::has(std::string_view stat) const {
    return entries_.find(stat) != entries_.end();
}
double StatSet::max(std::string_view stat) const {
    const StatDefinition *definition = catalog_ ? catalog_->stats.find(stat) : nullptr;
    return definition ? channel(stat, StatChannel::Max, definition->max) : 0.0;
}
double StatSet::min(std::string_view stat) const {
    const StatDefinition *definition = catalog_ ? catalog_->stats.find(stat) : nullptr;
    return definition ? std::min(channel(stat, StatChannel::Min, definition->min), max(stat)) : 0.0;
}
double StatSet::regen(std::string_view stat) const {
    const StatDefinition *definition = catalog_ ? catalog_->stats.find(stat) : nullptr;
    return definition ? channel(stat, StatChannel::Regen, definition->regen) : 0.0;
}
double StatSet::base(std::string_view stat) const {
    const auto found = entries_.find(stat);
    return found == entries_.end() ? 0.0 : found->second.base;
}
double StatSet::value(std::string_view stat) const {
    const auto found = entries_.find(stat);
    if (found == entries_.end())
        return 0.0;
    const double raw = std::clamp(channel(stat, StatChannel::Current, found->second.base),
                                  min(stat), std::max(max(stat), min(stat)));
    const StatDefinition *definition = catalog_ ? catalog_->stats.find(stat) : nullptr;
    return definition && definition->integer ? std::floor(raw + epsilon) : raw;
}
double StatSet::fraction(std::string_view stat) const {
    const double low = min(stat), high = max(stat);
    return high > low ? std::clamp((value(stat) - low) / (high - low), 0.0, 1.0) : 0.0;
}
std::vector<std::string> StatSet::ids() const {
    std::vector<std::string> list;
    for (const auto &[id, entry] : entries_) {
        (void)entry;
        list.push_back(id);
    }
    return list;
}

// Raises the events a change implies: the deliberate change itself, thresholds crossed, and the
// limits reached.
void StatSet::settle(GameContext &context, const StatDefinition &definition, Entry &entry,
                     double before, const std::string &cause, bool deliberate) {
    const double after = value(definition.id);
    entry.lastValue = after;
    if (std::fabs(after - before) < epsilon)
        return;
    ++revision_;
    const EntityId self = entity().id();
    if (deliberate)
        context.events().emit(
            GameEvent("stat.changed", self, {}, statData(definition.id, before, after, cause)));
    for (const StatThreshold &threshold : definition.thresholds) {
        const bool crossed = threshold.below ? before >= threshold.at && after < threshold.at
                                             : before <= threshold.at && after > threshold.at;
        if (crossed) {
            Json data = statData(definition.id, before, after, cause);
            data.set("at", threshold.at);
            context.events().emit(GameEvent(threshold.event, self, {}, std::move(data)));
        }
    }
    const double low = min(definition.id), high = max(definition.id);
    if (high > low) {
        if (after <= low + epsilon && before > low + epsilon)
            context.events().emit(GameEvent("stat.depleted", self, {},
                                            statData(definition.id, before, after, cause)));
        if (after >= high - epsilon && before < high - epsilon)
            context.events().emit(
                GameEvent("stat.full", self, {}, statData(definition.id, before, after, cause)));
    }
}

double StatSet::add(GameContext &context, std::string_view stat, double delta,
                    const std::string &cause) {
    const auto found = entries_.find(stat);
    const StatDefinition *definition = catalog_ ? catalog_->stats.find(stat) : nullptr;
    if (found == entries_.end() || !definition || !std::isfinite(delta))
        return 0.0;
    if (definition->integer)
        delta = std::round(delta);
    Entry &entry = found->second;
    const double before = value(stat);
    entry.base = std::clamp(entry.base + delta, min(stat), std::max(max(stat), min(stat)));
    if (delta < 0.0)
        entry.lastReduced = context.time();
    settle(context, *definition, entry, before, cause, true);
    return value(stat) - before;
}

double StatSet::set(GameContext &context, std::string_view stat, double target,
                    const std::string &cause) {
    const auto found = entries_.find(stat);
    const StatDefinition *definition = catalog_ ? catalog_->stats.find(stat) : nullptr;
    if (found == entries_.end() || !definition || !std::isfinite(target))
        return 0.0;
    if (definition->integer)
        target = std::round(target);
    Entry &entry = found->second;
    const double before = value(stat);
    // Set the number the modifiers start from so that the value reads as asked (within limits).
    double adds = 0.0, mult = 1.0;
    for (const StatModifier &modifier : modifiers_)
        if (modifier.spec.stat == stat && modifier.spec.channel == StatChannel::Current) {
            adds += modifier.spec.add;
            mult *= modifier.spec.mult;
        }
    entry.base = std::abs(mult) > epsilon ? target / mult - adds : target;
    entry.base = std::clamp(entry.base, std::min(definition->min, min(stat)),
                            std::max(definition->max, max(stat)));
    if (value(stat) < before)
        entry.lastReduced = context.time();
    settle(context, *definition, entry, before, cause, true);
    return value(stat) - before;
}

bool StatSet::canSpend(std::string_view stat, double amount) const {
    return has(stat) && value(stat) + epsilon >= amount;
}
bool StatSet::spend(GameContext &context, std::string_view stat, double amount,
                    const std::string &cause) {
    if (amount < 0.0 || !canSpend(stat, amount))
        return false;
    add(context, stat, -amount, cause);
    return true;
}

void StatSet::addModifier(GameContext &context, StatModifierSpec spec, const std::string &source,
                          double durationSeconds) {
    const StatDefinition *definition = catalog_ ? catalog_->stats.find(spec.stat) : nullptr;
    if (!definition)
        return;
    const double before = value(spec.stat);
    StatModifier modifier;
    modifier.spec = std::move(spec);
    modifier.source = source;
    modifier.expires = durationSeconds > 0.0 ? context.time() + durationSeconds : -1.0;
    modifiers_.push_back(modifier);
    ++revision_;
    settle(context, *definition, entries_[definition->id], before, "modifier:" + source, false);
}

std::size_t StatSet::removeModifiers(GameContext &context, const std::string &source) {
    std::map<std::string, double> before;
    for (const StatModifier &modifier : modifiers_)
        if (modifier.source == source)
            before[modifier.spec.stat] = value(modifier.spec.stat);
    const auto removed =
        std::remove_if(modifiers_.begin(), modifiers_.end(),
                       [&](const StatModifier &modifier) { return modifier.source == source; });
    const std::size_t count = static_cast<std::size_t>(modifiers_.end() - removed);
    modifiers_.erase(removed, modifiers_.end());
    if (count > 0)
        ++revision_;
    for (const auto &[stat, was] : before) {
        const StatDefinition *definition = catalog_ ? catalog_->stats.find(stat) : nullptr;
        if (!definition)
            continue;
        Entry &entry = entries_[stat];
        entry.base = std::clamp(entry.base, min(stat), std::max(max(stat), min(stat)));
        settle(context, *definition, entry, was, "modifier:" + source, false);
    }
    return count;
}

void StatSet::onFixedUpdate(GameContext &context, float seconds) {
    now_ = context.time();
    const double dt = static_cast<double>(seconds);
    // Modifiers that ran out.
    std::vector<std::string> ended;
    for (const StatModifier &modifier : modifiers_)
        if (modifier.expires >= 0.0 && now_ >= modifier.expires)
            ended.push_back(modifier.source);
    std::sort(ended.begin(), ended.end());
    ended.erase(std::unique(ended.begin(), ended.end()), ended.end());
    for (const std::string &source : ended) {
        // Only the expired ones of that source: other modifiers of the same source stay.
        std::map<std::string, double> before;
        for (const StatModifier &modifier : modifiers_)
            if (modifier.source == source && modifier.expires >= 0.0 && now_ >= modifier.expires)
                before[modifier.spec.stat] = value(modifier.spec.stat);
        modifiers_.erase(std::remove_if(modifiers_.begin(), modifiers_.end(),
                                        [&](const StatModifier &modifier) {
                                            return modifier.source == source &&
                                                   modifier.expires >= 0.0 &&
                                                   now_ >= modifier.expires;
                                        }),
                         modifiers_.end());
        ++revision_;
        for (const auto &[stat, was] : before)
            if (const StatDefinition *definition =
                    catalog_ ? catalog_->stats.find(stat) : nullptr) {
                Entry &entry = entries_[stat];
                entry.base = std::clamp(entry.base, min(stat), std::max(max(stat), min(stat)));
                settle(context, *definition, entry, was, "modifier:" + source, false);
            }
    }
    // Regeneration and decay.
    if (!catalog_)
        return;
    for (const StatDefinition &definition : catalog_->stats.all()) {
        const double pace = regen(definition.id);
        if (pace == 0.0)
            continue;
        Entry &entry = entries_[definition.id];
        const double low = min(definition.id), high = std::max(max(definition.id), low);
        if (pace > 0.0 && (entry.base >= high || now_ - entry.lastReduced < definition.regenDelay))
            continue;
        if (pace < 0.0 && entry.base <= low)
            continue;
        const double before = value(definition.id);
        entry.base = std::clamp(entry.base + pace * dt, low, high);
        settle(context, definition, entry, before, "regen", false);
    }
}

Json StatSet::saveState() const {
    Json state = Json::object();
    Json bases = Json::object();
    for (const auto &[id, entry] : entries_)
        bases.set(id, entry.base);
    state.set("base", bases);
    Json mods = Json::array();
    for (const StatModifier &modifier : modifiers_) {
        Json item = modifier.spec.toJson();
        item.set("source", modifier.source);
        if (modifier.expires >= 0.0)
            item.set("remaining", std::max(0.0, modifier.expires - now_));
        mods.push(item);
    }
    state.set("modifiers", mods);
    return state;
}

Status StatSet::loadState(GameContext &context, const Json &state) {
    catalog_ = catalog(context);
    now_ = context.time();
    const Json &bases = state.get("base");
    for (std::size_t i = 0; i < bases.size(); ++i) {
        const auto found = entries_.find(bases.keyAt(i));
        if (found != entries_.end() && bases.valueAt(i).isNumber())
            found->second.base = bases.valueAt(i).asNumber();
    }
    modifiers_.clear();
    const Json &mods = state.get("modifiers");
    for (std::size_t i = 0; i < mods.size(); ++i) {
        auto spec = StatModifierSpec::fromJson(mods.at(i));
        if (!spec)
            return Error{"stat modifier " + std::to_string(i + 1) + ": " + spec.error()};
        StatModifier modifier;
        modifier.spec = std::move(spec.value());
        modifier.source = mods.at(i).get("source").asString();
        modifier.expires =
            mods.at(i).contains("remaining") ? now_ + mods.at(i).get("remaining").asNumber() : -1.0;
        modifiers_.push_back(std::move(modifier));
    }
    for (auto &[id, entry] : entries_) {
        entry.base = std::clamp(entry.base, min(id), std::max(max(id), min(id)));
        entry.lastValue = value(id);
    }
    ++revision_;
    return success();
}
} // namespace yk
