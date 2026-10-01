#include "yk/rules/RuleService.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>
#include <functional>

namespace yk {
bool RuleService::eventMatches(const std::string &pattern, const std::string &name) {
    if (pattern.empty())
        return false;
    if (pattern == "*" || pattern == name)
        return true;
    return pattern.size() >= 2 && pattern.ends_with(".*") && name.size() > pattern.size() - 1 &&
           name.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
}

void RuleService::onStart(GameContext &context) {
    subscription_ = context.events().subscribe(
        EventBus::anyEvent, [this, &context](const GameEvent &event) { dispatch(context, event); });
}
void RuleService::onShutdown(GameContext &context) {
    if (subscription_ != 0)
        context.events().unsubscribe(subscription_);
    subscription_ = 0;
}

RuleService::Handle RuleService::add(EntityId owner, std::vector<Rule> rules, std::string label) {
    Owner entry;
    entry.id = owner;
    entry.label = std::move(label);
    // Highest priority first; equal priorities keep the order they were written in.
    std::stable_sort(rules.begin(), rules.end(),
                     [](const Rule &a, const Rule &b) { return a.priority > b.priority; });
    for (Rule &rule : rules) {
        Entry item;
        item.rule = std::move(rule);
        item.authoredEnabled = item.rule.enabled;
        if (item.rule.every > 0.0)
            item.nextTimer = clock_ + item.rule.every;
        else if (item.rule.after > 0.0)
            item.nextTimer = clock_ + item.rule.after;
        entry.entries.push_back(std::move(item));
    }
    const Handle handle = nextHandle_++;
    owners_.emplace(handle, std::move(entry));
    return handle;
}
void RuleService::remove(Handle handle) {
    owners_.erase(handle);
}
void RuleService::setEnabled(Handle handle, const std::string &ruleId, bool enabled) {
    if (const auto found = owners_.find(handle); found != owners_.end())
        for (Entry &entry : found->second.entries)
            if (entry.rule.id == ruleId)
                entry.rule.enabled = enabled;
}
bool RuleService::enabled(Handle handle, const std::string &ruleId) const {
    if (const auto found = owners_.find(handle); found != owners_.end())
        for (const Entry &entry : found->second.entries)
            if (entry.rule.id == ruleId)
                return entry.rule.enabled;
    return false;
}

void RuleService::fire(GameContext &context, Owner &owner, Entry &entry, const GameEvent *event) {
    RuleContext rules(context);
    rules.self = owner.id;
    rules.event = event;
    if (event) { // The one who did it and the thing it was done to.
        rules.actor = event->other ? event->other : event->source;
        rules.target = event->other ? event->source : EntityId{};
    } else {
        rules.actor = owner.id;
    }
    rules.origin = "rule '" + (entry.rule.id.empty() ? std::string("(unnamed)") : entry.rule.id) +
                   "' of " + owner.label;
    const bool met = evaluate(entry.rule.condition, rules);
    // The log shows every rule that was looked at (why did nothing happen?); only a rule that had
    // something to do starts its cooldown, is counted, or uses up a one-shot.
    recent_.push_back({clock_, entry.rule.id, owner.label, met});
    if (recent_.size() > 200)
        recent_.pop_front();
    const std::vector<Action> &actions = met ? entry.rule.then : entry.rule.otherwise;
    if (actions.empty())
        return;
    entry.lastFired = clock_;
    if (entry.rule.once && met)
        entry.fired = true; // The "else" of a one-shot rule keeps answering until it succeeds.
    ++counters_.fired;
    execute(actions, rules);
}

void RuleService::dispatch(GameContext &context, const GameEvent &event) {
    ++counters_.events;
    // Snapshot the handles: a rule may add or remove rules (spawning an entity with its own).
    std::vector<Handle> handles;
    handles.reserve(owners_.size());
    for (const auto &[handle, owner] : owners_) {
        (void)owner;
        handles.push_back(handle);
    }
    for (const Handle handle : handles) {
        const auto found = owners_.find(handle);
        if (found == owners_.end())
            continue;
        Owner &owner = found->second;
        for (std::size_t i = 0; i < owner.entries.size(); ++i) {
            Entry &entry = owner.entries[i];
            const Rule &rule = entry.rule;
            if (!rule.enabled || entry.fired || rule.event.empty() ||
                !eventMatches(rule.event, event.name))
                continue;
            if (rule.cooldown > 0.0 && clock_ - entry.lastFired < rule.cooldown)
                continue;
            // Who it must have come from.
            RuleContext probe(context);
            probe.self = owner.id;
            probe.actor = event.other ? event.other : event.source;
            probe.target = event.other ? event.source : EntityId{};
            const auto sameEntity = [&](const std::string &spec, EntityId id) {
                if (spec.empty())
                    return true;
                if (!id)
                    return false;
                for (const Entity *match : probe.resolve(spec))
                    if (match->id() == id)
                        return true;
                return false;
            };
            if (!sameEntity(rule.source, event.source) || !sameEntity(rule.other, event.other))
                continue;
            if (rule.dataFilter.isObject()) {
                bool matches = true;
                for (std::size_t k = 0; k < rule.dataFilter.size() && matches; ++k) {
                    const Json &have = event.data.get(rule.dataFilter.keyAt(k));
                    const auto wanted = valueFromJson(rule.dataFilter.valueAt(k));
                    const auto got = valueFromJson(have);
                    matches = wanted && got && valuesEqual(got.value(), wanted.value());
                }
                if (!matches)
                    continue;
            }
            fire(context, owner, entry, &event);
        }
    }
}

void RuleService::schedule(double seconds, std::vector<Action> actions,
                           const RuleContext &context) {
    ++counters_.delayed;
    later_.push_back({clock_ + std::max(seconds, 0.0), std::move(actions), context.self,
                      context.actor, context.target, context.origin, laterOrder_++});
}

void RuleService::onFixedUpdate(GameContext &context, float seconds) {
    clock_ += static_cast<double>(seconds);
    // Timer rules.
    std::vector<Handle> handles;
    for (const auto &[handle, owner] : owners_) {
        (void)owner;
        handles.push_back(handle);
    }
    for (const Handle handle : handles) {
        const auto found = owners_.find(handle);
        if (found == owners_.end())
            continue;
        Owner &owner = found->second;
        for (std::size_t i = 0; i < owner.entries.size(); ++i) {
            Entry &entry = owner.entries[i];
            const Rule &rule = entry.rule;
            if (!rule.enabled || entry.fired || entry.nextTimer < 0.0 || !rule.event.empty() ||
                clock_ < entry.nextTimer)
                continue;
            fire(context, owner, entry, nullptr);
            if (entry.rule.every > 0.0) {
                // A steady beat (no drift of up to a tick each round); after a stall it carries on
                // from now instead of firing once for every beat that was missed.
                entry.nextTimer += entry.rule.every;
                if (entry.nextTimer <= clock_)
                    entry.nextTimer = clock_ + entry.rule.every;
            } else {
                entry.fired = true; // "after" fires once.
            }
        }
    }
    // Continuations whose time has come, oldest first. They may schedule more.
    if (!later_.empty()) {
        std::vector<Later> due;
        std::vector<Later> keep;
        for (Later &item : later_)
            (item.due <= clock_ + 1e-9 ? due : keep).push_back(std::move(item));
        later_ = std::move(keep);
        std::sort(due.begin(), due.end(), [](const Later &a, const Later &b) {
            return a.due != b.due ? a.due < b.due : a.order < b.order;
        });
        for (Later &item : due) {
            RuleContext rules(context);
            rules.self = item.self;
            rules.actor = item.actor;
            rules.target = item.target;
            rules.origin = item.origin;
            execute(item.actions, rules);
        }
    }
}

void RuleService::describe(std::vector<std::pair<std::string, std::string>> &rows) const {
    std::size_t rules = 0;
    for (const auto &[handle, owner] : owners_) {
        (void)handle;
        rules += owner.entries.size();
    }
    rows.push_back(
        {"Rules", std::to_string(rules) + " in " + std::to_string(owners_.size()) + " sets"});
    rows.push_back({"Events / fired",
                    std::to_string(counters_.events) + " / " + std::to_string(counters_.fired)});
    rows.push_back({"Waiting", std::to_string(later_.size()) + " delayed action lists"});
}

// What differs from the authored rules: which one-shots are used up and which rules were switched
// on or off, so a loaded game does not run them again. Rules are named by owner entity and rule id.
Json RuleService::saveState() const {
    Json state = Json::object();
    state.set("clock", clock_);
    Json fired = Json::array();
    Json switched = Json::array();
    for (const auto &[handle, owner] : owners_) {
        (void)handle;
        for (const Entry &entry : owner.entries) {
            if (entry.rule.id.empty())
                continue;
            if (entry.fired) {
                Json item = Json::object();
                item.set("owner", toString(owner.id));
                item.set("rule", entry.rule.id);
                fired.push(item);
            }
            if (entry.rule.enabled != entry.authoredEnabled) {
                Json item = Json::object();
                item.set("owner", toString(owner.id));
                item.set("rule", entry.rule.id);
                item.set("enabled", entry.rule.enabled);
                switched.push(item);
            }
        }
    }
    state.set("fired", fired);
    state.set("switched", switched);
    return state;
}
Status RuleService::loadState(GameContext &, const Json &state) {
    if (!state.isObject())
        return Error{"rules: the saved state is not an object"};
    clock_ = state.get("clock").asNumber(0.0);
    const auto each = [&](const Json &list,
                          const std::function<void(Entry &, const Json &)> &apply) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto owner = parseEntityId(list.at(i).get("owner").asString());
            const std::string rule = list.at(i).get("rule").asString();
            if (!owner)
                continue;
            for (auto &[handle, entry] : owners_) {
                (void)handle;
                if (entry.id != *owner)
                    continue;
                for (Entry &item : entry.entries)
                    if (item.rule.id == rule)
                        apply(item, list.at(i));
            }
        }
    };
    each(state.get("fired"), [](Entry &entry, const Json &) { entry.fired = true; });
    each(state.get("switched"), [](Entry &entry, const Json &item) {
        entry.rule.enabled = item.get("enabled").asBool(entry.authoredEnabled);
    });
    return success();
}
} // namespace yk
