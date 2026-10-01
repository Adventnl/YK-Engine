#include "yk/rules/RuleSet.hpp"
#include "yk/core/Log.hpp"
#include "yk/scene/Scene.hpp"
#include <algorithm>

namespace yk {
namespace {
// Collects what validation says about a rule list as plain sentences.
class ProblemReport final : public RuleReport {
  public:
    ProblemReport(std::vector<std::string> &problems, const CheckContext &context)
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

  private:
    std::vector<std::string> &problems_;
    const CheckContext &context_;
};
} // namespace

bool RuleSet::setRulesJson(const Json &json) {
    auto parsed = rulesFromJson(json);
    if (!parsed) {
        error_ = parsed.error();
        return false;
    }
    error_.clear();
    json_ = json.isNull() ? Json::array() : json;
    rules_ = std::move(parsed.value());
    return true;
}

void RuleSet::describe(TypeBuilder<RuleSet> &type) {
    type.category("Logic")
        .description("WHEN an event happens, IF conditions hold, THEN actions run: game logic as "
                     "data. Edited with the rule editor; `self` in its rules is this entity.")
        .updatePhase(UpdatePhase::PreUpdate);
    type.computed<Json>(
        "rules", [](const RuleSet &set) { return set.rulesJson(); },
        [](RuleSet &set, const Json &json) { return set.setRulesJson(json); });
    type.check([](const Entity &entity, const RuleSet &set, const CheckContext &context,
                  std::vector<std::string> &problems) {
        ProblemReport report(problems, context);
        if (!set.lastError().empty())
            report.error("its rules are not valid: " + set.lastError());
        const RuleCatalog *catalog = entity.scene().registry().extension<RuleCatalog>();
        if (!catalog)
            return;
        for (const Rule &rule : set.rules())
            check(*catalog, rule, report);
    });
}

bool RuleSet::setRuleEnabled(GameContext &context, const std::string &ruleId, bool on) {
    auto *service = context.services().find<RuleService>();
    const bool known = std::any_of(rules_.begin(), rules_.end(),
                                   [&](const Rule &rule) { return rule.id == ruleId; });
    if (handle_ == 0 || !service || !known)
        return false;
    service->setEnabled(handle_, ruleId, on);
    return true;
}

void RuleSet::onStart(GameContext &context) {
    if (rules_.empty())
        return;
    handle_ = context.services().get<RuleService>().add(entity().id(), rules_,
                                                        "'" + entity().name() + "'");
}
void RuleSet::onDestroy(GameContext &context) {
    if (handle_ != 0)
        if (auto *service = context.services().find<RuleService>())
            service->remove(handle_);
    handle_ = 0;
}

void registerRuleComponents(ComponentRegistry &registry) {
    registerCoreRules(registry.extend<RuleCatalog>());
    registry.add<RuleSet>("RuleSet");
}
} // namespace yk
