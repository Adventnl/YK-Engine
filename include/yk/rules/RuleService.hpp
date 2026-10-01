#pragma once
#include "yk/rules/Rules.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/runtime/Services.hpp"
#include <deque>
#include <map>
#include <unordered_map>

namespace yk {
// Runs the rules of every RuleSet in the scene: listens to the event bus, evaluates the matching
// rules' conditions and runs their actions, fires timer rules ("every 5 seconds", "2 seconds after
// the scene started"), and runs the continuation of Delay and Repeat actions when their time comes.
// Rules run in priority order (highest first) and then in the order they were written.
class RuleService final : public Service, public RuleScheduler {
  public:
    using Handle = std::uint64_t;
    const char *name() const override {
        return "rules";
    }
    UpdatePhase phase() const override {
        return UpdatePhase::PreUpdate;
    }
    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onShutdown(GameContext &context) override;
    void describe(std::vector<std::pair<std::string, std::string>> &rows) const override;
    std::string saveKey() const override {
        return "rules";
    }
    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;

    // Registers the rules of one owner (an entity, or none for scenario-wide rules); `label` names
    // them in messages. Returns a handle to remove them with.
    Handle add(EntityId owner, std::vector<Rule> rules, std::string label);
    void remove(Handle handle);
    // Switches one rule of a set on or off while the game runs; the authored rules are unchanged
    // and the difference is part of the saved state.
    void setEnabled(Handle handle, const std::string &ruleId, bool enabled);
    bool enabled(Handle handle, const std::string &ruleId) const;

    void schedule(double seconds, std::vector<Action> actions, const RuleContext &context) override;

    struct Counters {
        std::uint64_t events{}, fired{}, delayed{};
    };
    const Counters &counters() const {
        return counters_;
    }
    // Rules (all owners) that fired, newest last; for the Debug panel's rule log.
    struct Firing {
        double time{};
        std::string rule;
        std::string origin;
        bool conditionMet{};
    };
    const std::deque<Firing> &recent() const {
        return recent_;
    }

  private:
    struct Entry {
        Rule rule;
        bool authoredEnabled{true};
        bool fired{false};
        double lastFired{-1.0e9};
        double nextTimer{-1.0};
    };
    struct Owner {
        EntityId id;
        std::string label;
        std::vector<Entry> entries;
    };
    struct Later {
        double due;
        std::vector<Action> actions;
        EntityId self, actor, target;
        std::string origin;
        std::uint64_t order;
    };
    void dispatch(GameContext &context, const GameEvent &event);
    void fire(GameContext &context, Owner &owner, Entry &entry, const GameEvent *event);

    std::map<Handle, Owner> owners_;
    Handle nextHandle_{1};
    EventBus::Subscription subscription_{};
    std::vector<Later> later_;
    std::uint64_t laterOrder_{};
    double clock_{0.0};
    Counters counters_;
    std::deque<Firing> recent_;
};
} // namespace yk
