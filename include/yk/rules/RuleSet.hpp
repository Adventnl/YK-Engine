#pragma once
#include "yk/rules/RuleService.hpp"
#include "yk/scene/Registry.hpp"

namespace yk {
// A list of rules on an entity: WHEN something happens IF these conditions hold THEN do these
// actions. The data is JSON (edited with the rule editor) so it is saved with the scene and
// prefabs, and needs no code. `self` in its rules is this entity.
//
//   [{"id": "repair_console",
//     "when": {"event": "interacted", "source": "self"},
//     "if": {"all": [{"type": "HasItem", "item": "circuit_board"},
//                    {"var": "power", "op": "==", "value": 0}]},
//     "then": [{"type": "RemoveItem", "item": "circuit_board"},
//              {"type": "SetVariable", "name": "autopilot_repaired", "value": true}]}]
class RuleSet final : public Component {
  public:
    static void describe(TypeBuilder<RuleSet> &type);
    const std::vector<Rule> &rules() const {
        return rules_;
    }
    const Json &rulesJson() const {
        return json_;
    }
    bool setRulesJson(const Json &json);
    const std::string &lastError() const {
        return error_;
    }
    // Switches one rule on or off while the game runs (a quest that starts a rule, a scripted
    // alarm that stops one). The authored data is unchanged; the service keeps and saves the
    // difference. False when the set is not running or has no such rule.
    bool setRuleEnabled(GameContext &context, const std::string &ruleId, bool on);
    void onStart(GameContext &context) override;
    void onDestroy(GameContext &context) override;

  private:
    Json json_{Json::array()};
    std::vector<Rule> rules_;
    std::string error_;
    RuleService::Handle handle_{0};
};

// A RuleReport for a component's check(): errors go to the validator's error channel (or into the
// plain list outside it) and ids are looked up through the check's `known`. Components that hold
// rules or conditions of their own (an item's use, a container's open permission) use it.
class ComponentRuleReport final : public RuleReport {
  public:
    ComponentRuleReport(std::vector<std::string> &problems, const CheckContext &context)
        : problems_(problems), context_(context) {}
    void error(const std::string &message) override {
        if (context_.error)
            context_.error(message);
        else
            problems_.push_back(message);
    }
    void warning(const std::string &message) override {
        problems_.push_back(message);
    }
    bool known(std::string_view kind, std::string_view id) const override {
        return !context_.known || context_.known(kind, id);
    }

  private:
    std::vector<std::string> &problems_;
    const CheckContext &context_;
};

void registerRuleComponents(ComponentRegistry &registry);
} // namespace yk
