#pragma once
#include "yk/data/Table.hpp"
#include "yk/rules/Rules.hpp"
#include "yk/runtime/Services.hpp"
#include "yk/scene/Registry.hpp"
#include <string>
#include <vector>

// A facility's alert state as data. A project lists its security levels (0 normal ... 5 lockdown,
// or whatever it likes); entering one runs its actions, leaving runs the others, and a level may
// fall back by itself after a quiet spell. Lockdowns are named procedures with a countdown, actions
// to start, end and fail them. Nothing here knows doors, dogs or guards: the actions are the rule
// language (lock doors, spawn agents, change schedules), and AI brains and access conditions read
// the level and the lockdown as facts (security.level, security.lockdown).
//
//   "security": {
//     "levels": [
//       {"id": "normal", "name": "Normal"},
//       {"id": "alert", "name": "Increased patrol", "onEnter": [...], "onExit": [...], "decay":
//       120},
//       {"id": "lockdown", "name": "Lockdown"}],
//     "lockdowns": [
//       {"id": "riot", "countdown": 90, "level": "lockdown", "onStart": [...], "onEnd": [...],
//        "onFail": [...]}]}
namespace yk {
class GameContext;

struct SecurityLevel {
    std::string id;
    std::string name;
    std::vector<Action> onEnter, onExit;
    double decay{0.0}; // Seconds without a raise before it falls one level; 0: it stays.
};
struct LockdownDefinition {
    std::string id;
    std::string file;
    std::string name;
    double countdown{0.0}; // Seconds to end it before onFail runs; 0: no limit.
    std::string level;     // The level it puts the facility in ("" none).
    std::vector<Action> onStart, onEnd, onFail;
    static Result<LockdownDefinition> fromJson(const Json &json,
                                               std::vector<std::string> &warnings);
};
using LockdownTable = DefinitionTable<LockdownDefinition>;

struct SecurityCatalog {
    std::vector<SecurityLevel> levels;
    LockdownTable lockdowns;
    std::string file;
    void load(const Json &document, const std::string &file, std::vector<DataProblem> &problems);
    void check(std::vector<DataProblem> &problems) const;
    void visitRules(const RuleSourceVisitor &visit) const;
    int indexOf(std::string_view id) const;
};

// The running state. Events: security.changed (data: from, to, cause, level), lockdown.started,
// lockdown.ended (data: lockdown, failed), lockdown.countdown (each second; data: remaining).
class SecurityService final : public Service {
  public:
    const char *name() const override {
        return "security";
    }
    UpdatePhase phase() const override {
        return UpdatePhase::PostSimulation;
    }
    std::string saveKey() const override {
        return "security";
    }
    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    void describe(std::vector<std::pair<std::string, std::string>> &rows) const override;

    int level() const {
        return level_;
    }
    std::string levelId(GameContext &context) const;
    // Moves to a level by index or id (clamped); runs exit and enter actions. False when unchanged
    // or unknown.
    bool setLevel(GameContext &context, int index, const std::string &cause = {});
    bool setLevel(GameContext &context, const std::string &id, const std::string &cause = {});
    bool raise(GameContext &context, int by, const std::string &cause = {});
    const std::string &lockdown() const {
        return lockdown_;
    }
    double lockdownRemaining() const {
        return remaining_;
    }
    bool startLockdown(GameContext &context, const std::string &id);
    bool endLockdown(GameContext &context, bool failed = false);

  private:
    void run(GameContext &context, const std::vector<Action> &actions, const std::string &origin);
    int level_{0};
    double sinceRaise_{0.0};
    std::string lockdown_;
    double remaining_{0.0};
    double secondCarry_{0.0};
    bool inChange_{false};
};

// Who may use a door, a terminal, a vending machine, a gate: factions and roles that may (what they
// look like counts when countDisguise is set) and a condition that must hold (a keycard, a quest, a
// time of day, the security level). The thing that is used asks `allows`; rules ask AccessAllowed.
class AccessPolicy final : public Component {
  public:
    std::vector<std::string> allowedFactions; // Empty: no restriction by faction.
    std::vector<std::string> allowedRoles;    // Empty: no restriction by role.
    Json access;                              // Condition on the user; empty: none.
    bool countDisguise{true};
    bool lockedDuringLockdown{false}; // Nobody may use it while a lockdown runs.
    std::string deniedMessage;
    static void describe(TypeBuilder<AccessPolicy> &type);
    bool allows(GameContext &context, const Entity &actor, std::string *why = nullptr) const;

  private:
    mutable Json conditionSource_;
    mutable Condition condition_;
    mutable bool conditionOk_{false};
};

void registerSecurityRules(RuleCatalog &catalog);
void registerSecurityComponents(ComponentRegistry &registry);
} // namespace yk
