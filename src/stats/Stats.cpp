#include "yk/stats/Stats.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
const std::vector<std::string> &statChannelNames() {
    static const std::vector<std::string> names{"value", "max", "min", "regen"};
    return names;
}
const std::vector<std::string> &stackingNames() {
    static const std::vector<std::string> names{"refresh", "stack", "extend", "ignore"};
    return names;
}

// ---- Stats --------------------------------------------------------------------------------------
Result<StatDefinition> StatDefinition::fromJson(const Json &json,
                                                std::vector<std::string> &warnings) {
    if (!json.isObject())
        return Error{"a stat must be an object"};
    StatDefinition stat;
    auto id = data::requiredString(json, "id");
    if (!id)
        return Error{id.error()};
    stat.id = id.value();
    if (!data::validId(stat.id))
        return Error{"'" + stat.id + "' is not a usable id (letters, digits, '_' and '-')"};
    stat.name = data::optionalString(json, "name", stat.id);
    stat.category = data::optionalString(json, "category");
    stat.description = data::optionalString(json, "description");
    constexpr double huge = 1.0e12;
    auto low = data::number(json, "min", 0.0, -huge, huge);
    auto high = data::number(json, "max", 100.0, -huge, huge);
    if (!low)
        return Error{low.error()};
    if (!high)
        return Error{high.error()};
    stat.min = low.value();
    stat.max = high.value();
    if (stat.min > stat.max)
        return Error{"'min' is above 'max'"};
    const Json &start = json.get("start");
    if (start.isNull() || (start.isString() && start.asString() == "max")) {
        stat.startsFull = true;
    } else if (start.isString() && start.asString() == "min") {
        stat.startsFull = false;
        stat.start = stat.min;
    } else if (start.isNumber() && start.asNumber() >= stat.min && start.asNumber() <= stat.max) {
        stat.startsFull = false;
        stat.start = start.asNumber();
    } else {
        return Error{"'start' must be a number between 'min' and 'max', or \"max\" or \"min\""};
    }
    auto regen = data::number(json, "regen", 0.0, -huge, huge);
    auto delay = data::number(json, "regenDelay", 0.0, 0.0, 86400.0);
    if (!regen)
        return Error{regen.error()};
    if (!delay)
        return Error{delay.error()};
    stat.regen = regen.value();
    stat.regenDelay = delay.value();
    stat.integer = json.get("integer").asBool(false);
    const Json &thresholds = json.get("thresholds");
    if (json.contains("thresholds") && !thresholds.isArray())
        return Error{"'thresholds' must be a list"};
    for (std::size_t i = 0; i < thresholds.size(); ++i) {
        const Json &item = thresholds.at(i);
        StatThreshold threshold;
        if (!item.isObject() || !item.get("at").isNumber())
            return Error{"threshold " + std::to_string(i + 1) + " needs a number 'at'"};
        threshold.at = item.get("at").asNumber();
        const std::string direction =
            item.get("direction").isString() ? item.get("direction").asString() : "below";
        if (direction != "below" && direction != "above")
            return Error{"threshold " + std::to_string(i + 1) +
                         ": 'direction' is \"below\" or \"above\""};
        threshold.below = direction == "below";
        threshold.event = data::optionalString(item, "event");
        if (threshold.event.empty())
            return Error{"threshold " + std::to_string(i + 1) + " needs the 'event' it raises"};
        stat.thresholds.push_back(std::move(threshold));
    }
    data::warnUnknown(json,
                      {"id", "name", "category", "description", "min", "max", "start", "regen",
                       "regenDelay", "integer", "thresholds"},
                      warnings);
    return stat;
}

Json StatDefinition::toJson() const {
    Json json = Json::object();
    json.set("id", id);
    if (name != id)
        json.set("name", name);
    if (!category.empty())
        json.set("category", category);
    if (!description.empty())
        json.set("description", description);
    json.set("min", min);
    json.set("max", max);
    if (startsFull)
        json.set("start", "max");
    else
        json.set("start", start);
    if (regen != 0.0)
        json.set("regen", regen);
    if (regenDelay != 0.0)
        json.set("regenDelay", regenDelay);
    if (integer)
        json.set("integer", true);
    if (!thresholds.empty()) {
        Json list = Json::array();
        for (const StatThreshold &threshold : thresholds) {
            Json item = Json::object();
            item.set("at", threshold.at);
            item.set("direction", threshold.below ? "below" : "above");
            item.set("event", threshold.event);
            list.push(item);
        }
        json.set("thresholds", list);
    }
    return json;
}

Result<StatModifierSpec> StatModifierSpec::fromJson(const Json &json) {
    if (!json.isObject())
        return Error{"a stat modifier must be an object like {\"stat\": \"strength\", \"add\": 2}"};
    StatModifierSpec spec;
    auto stat = data::requiredString(json, "stat");
    if (!stat)
        return Error{stat.error()};
    spec.stat = stat.value();
    const std::string channel = data::optionalString(json, "channel", "value");
    const auto &names = statChannelNames();
    const auto found = std::find(names.begin(), names.end(), channel);
    if (found == names.end())
        return Error{"'channel' must be value, max, min or regen"};
    spec.channel = static_cast<StatChannel>(found - names.begin());
    auto add = data::number(json, "add", 0.0, -1.0e12, 1.0e12);
    auto mult = data::number(json, "mult", 1.0, -1000.0, 1000.0);
    if (!add)
        return Error{add.error()};
    if (!mult)
        return Error{mult.error()};
    spec.add = add.value();
    spec.mult = mult.value();
    if (!json.contains("add") && !json.contains("mult"))
        return Error{"a modifier for '" + spec.stat +
                     "' says neither 'add' nor 'mult', so it changes nothing"};
    return spec;
}

Json StatModifierSpec::toJson() const {
    Json json = Json::object();
    json.set("stat", stat);
    if (channel != StatChannel::Current)
        json.set("channel", statChannelNames()[static_cast<std::size_t>(channel)]);
    if (add != 0.0)
        json.set("add", add);
    if (mult != 1.0)
        json.set("mult", mult);
    return json;
}

// ---- Effects ------------------------------------------------------------------------------------
Result<EffectDefinition> EffectDefinition::fromJson(const Json &json,
                                                    std::vector<std::string> &warnings) {
    if (!json.isObject())
        return Error{"an effect must be an object"};
    EffectDefinition effect;
    auto id = data::requiredString(json, "id");
    if (!id)
        return Error{id.error()};
    effect.id = id.value();
    if (!data::validId(effect.id))
        return Error{"'" + effect.id + "' is not a usable id (letters, digits, '_' and '-')"};
    effect.name = data::optionalString(json, "name", effect.id);
    effect.description = data::optionalString(json, "description");
    effect.icon = data::optionalString(json, "icon");
    effect.animation = data::optionalString(json, "animation");
    effect.hidden = json.get("hidden").asBool(false);
    auto duration = data::number(json, "duration", 0.0, 0.0, 1.0e7);
    if (!duration)
        return Error{duration.error()};
    effect.duration = duration.value();
    const std::string stacking = data::optionalString(json, "stacking", "refresh");
    const auto &names = stackingNames();
    const auto found = std::find(names.begin(), names.end(), stacking);
    if (found == names.end())
        return Error{"'stacking' must be refresh, stack, extend or ignore"};
    effect.stacking = static_cast<Stacking>(found - names.begin());
    auto stacks = data::number(json, "maxStacks", 1.0, 1.0, 1000.0);
    if (!stacks)
        return Error{stacks.error()};
    effect.maxStacks = static_cast<int>(stacks.value());
    if (effect.stacking == Stacking::Stack && effect.maxStacks < 2)
        warnings.push_back("stacks, but 'maxStacks' is 1: it will never stack");
    if (json.contains("modifiers")) {
        if (!json.get("modifiers").isArray())
            return Error{"'modifiers' must be a list"};
        for (std::size_t i = 0; i < json.get("modifiers").size(); ++i) {
            auto modifier = StatModifierSpec::fromJson(json.get("modifiers").at(i));
            if (!modifier)
                return Error{"modifier " + std::to_string(i + 1) + ": " + modifier.error()};
            effect.modifiers.push_back(std::move(modifier.value()));
        }
    }
    if (json.contains("ticks")) {
        if (!json.get("ticks").isArray())
            return Error{"'ticks' must be a list"};
        for (std::size_t i = 0; i < json.get("ticks").size(); ++i) {
            const Json &item = json.get("ticks").at(i);
            Tick tick;
            tick.stat = data::optionalString(item, "stat");
            if (!item.isObject() || tick.stat.empty() || !item.get("amount").isNumber())
                return Error{"tick " + std::to_string(i + 1) +
                             " needs a 'stat' and a number 'amount'"};
            tick.amount = item.get("amount").asNumber();
            auto interval = data::number(item, "every", 1.0, 0.05, 86400.0);
            if (!interval)
                return Error{"tick " + std::to_string(i + 1) + ": " + interval.error()};
            tick.interval = interval.value();
            tick.source = data::optionalString(item, "type");
            effect.ticks.push_back(std::move(tick));
        }
    }
    effect.flags = data::stringList(json, "flags");
    effect.grants = data::stringList(json, "grants");
    effect.tags = data::stringList(json, "tags");
    if (json.contains("factors")) {
        const Json &factors = json.get("factors");
        if (!factors.isObject())
            return Error{"'factors' must be an object like {\"move.speed\": 0.5}"};
        for (std::size_t i = 0; i < factors.size(); ++i) {
            if (!factors.valueAt(i).isNumber() || factors.valueAt(i).asNumber() < 0.0)
                return Error{"the factor '" + factors.keyAt(i) + "' must be a number of 0 or more"};
            effect.factors[factors.keyAt(i)] = factors.valueAt(i).asNumber();
        }
    }
    for (const char *key : {"onApply", "onExpire"}) {
        auto actions = Action::listFromJson(json.get(key));
        if (!actions)
            return Error{std::string(key) + ": " + actions.error()};
        (std::string_view(key) == "onApply" ? effect.onApply : effect.onExpire) =
            std::move(actions.value());
    }
    data::warnUnknown(json,
                      {"id", "name", "description", "icon", "duration", "stacking", "maxStacks",
                       "modifiers", "ticks", "flags", "factors", "grants", "tags", "animation",
                       "onApply", "onExpire", "hidden"},
                      warnings);
    return effect;
}

Json EffectDefinition::toJson() const {
    Json json = Json::object();
    json.set("id", id);
    if (name != id)
        json.set("name", name);
    if (!description.empty())
        json.set("description", description);
    if (!icon.empty())
        json.set("icon", icon);
    if (duration > 0.0)
        json.set("duration", duration);
    if (stacking != Stacking::Refresh)
        json.set("stacking", stackingNames()[static_cast<std::size_t>(stacking)]);
    if (maxStacks != 1)
        json.set("maxStacks", maxStacks);
    if (!modifiers.empty()) {
        Json list = Json::array();
        for (const StatModifierSpec &modifier : modifiers)
            list.push(modifier.toJson());
        json.set("modifiers", list);
    }
    if (!ticks.empty()) {
        Json list = Json::array();
        for (const Tick &tick : ticks) {
            Json item = Json::object();
            item.set("stat", tick.stat);
            item.set("amount", tick.amount);
            if (tick.interval != 1.0)
                item.set("every", tick.interval);
            if (!tick.source.empty())
                item.set("type", tick.source);
            list.push(item);
        }
        json.set("ticks", list);
    }
    if (!flags.empty())
        json.set("flags", data::toJsonList(flags));
    if (!factors.empty()) {
        Json object = Json::object();
        for (const auto &[key, value] : factors)
            object.set(key, value);
        json.set("factors", object);
    }
    if (!grants.empty())
        json.set("grants", data::toJsonList(grants));
    if (!tags.empty())
        json.set("tags", data::toJsonList(tags));
    if (!animation.empty())
        json.set("animation", animation);
    if (hidden)
        json.set("hidden", true);
    const auto write = [&](const char *key, const std::vector<Action> &list) {
        if (list.empty())
            return;
        Json actions = Json::array();
        for (const Action &action : list)
            actions.push(action.toJson());
        json.set(key, actions);
    };
    write("onApply", onApply);
    write("onExpire", onExpire);
    return json;
}

// ---- Catalog ------------------------------------------------------------------------------------
void StatCatalog::load(const Json &document, const std::string &file,
                       std::vector<DataProblem> &problems) {
    if (document.contains("stats"))
        stats.load(document.get("stats"), file, problems, "stat");
    if (document.contains("effects"))
        effects.load(document.get("effects"), file, problems, "effect");
}

void StatCatalog::check(std::vector<DataProblem> &problems) const {
    for (const EffectDefinition &effect : effects.all()) {
        if (effect.id.starts_with("equip:"))
            continue; // An item's own effect: the item's check reports what is wrong with it.
        const std::string where = "effect '" + effect.id + "': ";
        for (const StatModifierSpec &modifier : effect.modifiers)
            if (!stats.contains(modifier.stat))
                problems.push_back({effect.file,
                                    where + "a modifier names the stat '" + modifier.stat +
                                        "', which is not defined",
                                    true});
        for (const EffectDefinition::Tick &tick : effect.ticks)
            if (!stats.contains(tick.stat))
                problems.push_back(
                    {effect.file,
                     where + "a tick names the stat '" + tick.stat + "', which is not defined",
                     true});
        if (effect.duration <= 0.0 && effect.stacking == EffectDefinition::Stacking::Extend)
            problems.push_back(
                {effect.file, where + "lasts until removed, so 'extend' does nothing", false});
    }
}

void StatCatalog::visitRules(const RuleSourceVisitor &visit) const {
    for (const EffectDefinition &effect : effects.all()) {
        if (!effect.onApply.empty())
            visit({effect.file, "effect '" + effect.id + "' onApply", nullptr, &effect.onApply});
        if (!effect.onExpire.empty())
            visit({effect.file, "effect '" + effect.id + "' onExpire", nullptr, &effect.onExpire});
    }
}
} // namespace yk
