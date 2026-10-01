#include "yk/rules/Rules.hpp"
#include "yk/core/Log.hpp"
#include "yk/rules/RuleService.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/runtime/Random.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace yk {
// ---- Context ------------------------------------------------------------------------------------
Entity *RuleContext::selfEntity() const {
    return self ? game.scene().find(self) : nullptr;
}
Entity *RuleContext::actorEntity() const {
    return actor ? game.scene().find(actor) : nullptr;
}
Entity *RuleContext::targetEntity() const {
    return target ? game.scene().find(target) : nullptr;
}
std::vector<Entity *> RuleContext::resolve(std::string_view spec) const {
    std::vector<Entity *> result;
    if (spec.empty())
        return result;
    const auto add = [&](Entity *entity) {
        if (entity)
            result.push_back(entity);
    };
    if (spec == "self") {
        add(selfEntity());
    } else if (spec == "actor") {
        add(actorEntity());
    } else if (spec == "target") {
        add(targetEntity());
    } else if (spec.starts_with("id:")) {
        if (const auto id = parseEntityId(spec.substr(3)))
            add(game.scene().find(*id));
    } else if (spec.starts_with("tag:")) {
        const std::string tag(spec.substr(4));
        game.scene().forEach([&](Entity &entity) {
            if (entity.hasTag(tag))
                result.push_back(&entity);
        });
    } else {
        const std::string_view name = spec.starts_with("name:") ? spec.substr(5) : spec;
        add(game.scene().findByName(name));
    }
    return result;
}
Entity *RuleContext::resolveOne(std::string_view spec) const {
    const auto found = resolve(spec);
    return found.empty() ? nullptr : found.front();
}
const RuleCatalog *RuleContext::catalog() const {
    return game.scene().registry().extension<RuleCatalog>();
}
std::optional<Value> RuleContext::fact(std::string_view path) const {
    const RuleCatalog *rules = catalog();
    if (!rules)
        return std::nullopt;
    RuleContext copy = *this;
    return rules->read(copy, path);
}
Value RuleContext::argument(const Json &json) const {
    if (json.isString() && json.asString().starts_with("$")) {
        const auto fact = this->fact(json.asString().substr(1));
        return fact ? *fact : Value{};
    }
    auto value = valueFromJson(json);
    return value ? value.value() : Value{};
}

// ---- Comparison ---------------------------------------------------------------------------------
const std::vector<std::string> &compareOpNames() {
    static const std::vector<std::string> names{"==", "!=", "<", "<=", ">", ">="};
    return names;
}
std::optional<CompareOp> parseCompareOp(std::string_view text) {
    const auto &names = compareOpNames();
    for (std::size_t i = 0; i < names.size(); ++i)
        if (names[i] == text)
            return static_cast<CompareOp>(i);
    if (text == "=")
        return CompareOp::Equal;
    return std::nullopt;
}
bool compare(const Value &left, CompareOp op, const Value &right) {
    // A fact that has no value yet reads as the zero of what it is compared with ("not set" is 0,
    // false or empty), so `var.power == 0` holds before anything set it.
    Value lhs = left;
    if (std::holds_alternative<std::monostate>(lhs)) {
        if (std::holds_alternative<bool>(right))
            lhs = false;
        else if (isNumeric(right))
            lhs = 0.0;
        else if (std::holds_alternative<std::string>(right))
            lhs = std::string();
    }
    if (op == CompareOp::Equal)
        return valuesEqual(lhs, right);
    if (op == CompareOp::NotEqual)
        return !valuesEqual(lhs, right);
    int order = 0;
    if (!compareValues(lhs, right, order))
        return false;
    switch (op) {
    case CompareOp::Less:
        return order < 0;
    case CompareOp::LessEqual:
        return order <= 0;
    case CompareOp::Greater:
        return order > 0;
    case CompareOp::GreaterEqual:
        return order >= 0;
    default:
        return false;
    }
}

// ---- Conditions ---------------------------------------------------------------------------------
Result<Condition> Condition::fromJson(const Json &json) {
    Condition condition;
    if (json.isNull()) {
        return condition; // No condition: always.
    }
    if (json.isBool()) {
        condition.kind = json.asBool() ? Kind::Always : Kind::Never;
        return condition;
    }
    if (json.isArray()) { // A bare list means "all of these".
        condition.kind = Kind::All;
        for (std::size_t i = 0; i < json.size(); ++i) {
            auto child = fromJson(json.at(i));
            if (!child)
                return Error{"[" + std::to_string(i) + "]: " + child.error()};
            condition.children.push_back(std::move(child.value()));
        }
        return condition;
    }
    if (!json.isObject())
        return Error{"a condition must be an object, a list or true/false"};
    const auto list = [&](const char *key, Kind kind) -> Result<Condition> {
        const Json &items = json.get(key);
        if (!items.isArray())
            return Error{std::string("'") + key + "' must be a list of conditions"};
        condition.kind = kind;
        for (std::size_t i = 0; i < items.size(); ++i) {
            auto child = fromJson(items.at(i));
            if (!child)
                return Error{std::string(key) + "[" + std::to_string(i) + "]: " + child.error()};
            condition.children.push_back(std::move(child.value()));
        }
        return condition;
    };
    if (json.contains("all"))
        return list("all", Kind::All);
    if (json.contains("any"))
        return list("any", Kind::Any);
    if (json.contains("not")) {
        auto child = fromJson(json.get("not"));
        if (!child)
            return Error{"not: " + child.error()};
        condition.kind = Kind::Not;
        condition.children.push_back(std::move(child.value()));
        return condition;
    }
    if (json.contains("type")) {
        if (!json.get("type").isString() || json.get("type").asString().empty())
            return Error{"a condition's 'type' must name a condition"};
        condition.kind = Kind::Predicate;
        condition.type = json.get("type").asString();
        condition.args = json;
        return condition;
    }
    if (json.contains("fact") || json.contains("var")) {
        const bool variable = json.contains("var");
        const std::string name = json.get(variable ? "var" : "fact").asString();
        if (name.empty())
            return Error{"a comparison needs the name of a fact (or a 'var')"};
        const std::string opText = json.contains("op") ? json.get("op").asString() : "==";
        const auto op = parseCompareOp(opText);
        if (!op)
            return Error{"unknown comparison '" + opText + "' (use ==, !=, <, <=, > or >=)"};
        condition.kind = Kind::Compare;
        condition.fact = variable ? "var." + name : name;
        condition.op = *op;
        condition.value = json.contains("value") ? json.get("value") : Json(true);
        return condition;
    }
    return Error{"a condition needs one of 'all', 'any', 'not', 'type', 'fact' or 'var'"};
}

Json Condition::toJson() const {
    Json json = Json::object();
    switch (kind) {
    case Kind::Always:
        return Json(true);
    case Kind::Never:
        return Json(false);
    case Kind::All:
    case Kind::Any: {
        Json items = Json::array();
        for (const Condition &child : children)
            items.push(child.toJson());
        json.set(kind == Kind::All ? "all" : "any", items);
        return json;
    }
    case Kind::Not:
        json.set("not", children.empty() ? Json(true) : children.front().toJson());
        return json;
    case Kind::Predicate:
        return args;
    case Kind::Compare:
        // The way it is usually written: "var" for a game variable, and nothing more for "is true".
        if (fact.starts_with("var."))
            json.set("var", fact.substr(4));
        else
            json.set("fact", fact);
        if (op != CompareOp::Equal || !(value == Json(true))) {
            json.set("op", compareOpNames()[static_cast<std::size_t>(op)]);
            json.set("value", value);
        }
        return json;
    }
    return json;
}

bool evaluate(const Condition &condition, RuleContext &context) {
    switch (condition.kind) {
    case Condition::Kind::Always:
        return true;
    case Condition::Kind::Never:
        return false;
    case Condition::Kind::All:
        return std::all_of(condition.children.begin(), condition.children.end(),
                           [&](const Condition &child) { return evaluate(child, context); });
    case Condition::Kind::Any:
        return std::any_of(condition.children.begin(), condition.children.end(),
                           [&](const Condition &child) { return evaluate(child, context); });
    case Condition::Kind::Not:
        return condition.children.empty() || !evaluate(condition.children.front(), context);
    case Condition::Kind::Compare: {
        const auto left = context.fact(condition.fact);
        return compare(left ? *left : Value{}, condition.op, context.argument(condition.value));
    }
    case Condition::Kind::Predicate: {
        const RuleCatalog *catalog = context.catalog();
        const PredicateType *type = catalog ? catalog->predicate(condition.type) : nullptr;
        if (!type || !type->evaluate) {
            log(LogLevel::Warning, "rules",
                (context.origin.empty() ? "" : context.origin + ": ") + "unknown condition '" +
                    condition.type + "' counts as false");
            return false;
        }
        return type->evaluate(condition.args, context);
    }
    }
    return false;
}

// ---- Actions ------------------------------------------------------------------------------------
Result<Action> Action::fromJson(const Json &json) {
    if (!json.isObject())
        return Error{"an action must be an object with a 'type'"};
    if (!json.get("type").isString() || json.get("type").asString().empty())
        return Error{"an action needs a 'type' naming what to do"};
    Action action;
    action.type = json.get("type").asString();
    action.args = json;
    return action;
}
Result<std::vector<Action>> Action::listFromJson(const Json &json) {
    std::vector<Action> actions;
    if (json.isNull())
        return actions;
    if (json.isObject()) {
        auto one = fromJson(json);
        if (!one)
            return Error{one.error()};
        actions.push_back(std::move(one.value()));
        return actions;
    }
    if (!json.isArray())
        return Error{"actions must be an action or a list of actions"};
    for (std::size_t i = 0; i < json.size(); ++i) {
        auto one = fromJson(json.at(i));
        if (!one)
            return Error{"action " + std::to_string(i + 1) + ": " + one.error()};
        actions.push_back(std::move(one.value()));
    }
    return actions;
}
Json Action::toJson() const {
    return args;
}

ActionResult execute(const Action &action, RuleContext &context) {
    const RuleCatalog *catalog = context.catalog();
    const ActionType *type = catalog ? catalog->action(action.type) : nullptr;
    if (!type || !type->execute) {
        log(LogLevel::Warning, "rules",
            (context.origin.empty() ? "" : context.origin + ": ") + "unknown action '" +
                action.type + "' skipped");
        return ActionResult::Failed;
    }
    return type->execute(action.args, context);
}
ActionResult execute(const std::vector<Action> &actions, RuleContext &context, bool stopOnFailure) {
    ActionResult overall = ActionResult::Done;
    for (const Action &action : actions) {
        if (execute(action, context) == ActionResult::Failed) {
            overall = ActionResult::Failed;
            if (stopOnFailure)
                break;
        }
    }
    return overall;
}

RuleScheduler *schedulerFor(GameContext &game) {
    return &game.services().get<RuleService>();
}

// ---- The catalog --------------------------------------------------------------------------------
void RuleCatalog::addPredicate(PredicateType type) {
    if (type.name.empty() || predicates_.contains(type.name))
        throw std::logic_error("condition '" + type.name + "' is empty or already registered");
    const std::string name = type.name;
    predicates_.emplace(name, std::move(type));
}
void RuleCatalog::addAction(ActionType type) {
    if (type.name.empty() || actions_.contains(type.name))
        throw std::logic_error("action '" + type.name + "' is empty or already registered");
    const std::string name = type.name;
    actions_.emplace(name, std::move(type));
}
void RuleCatalog::addFacts(std::string ns, FactProvider provider, bool perEntity) {
    if (ns.empty() || facts_.contains(ns))
        throw std::logic_error("fact namespace '" + ns + "' is empty or already registered");
    facts_.emplace(std::move(ns), Facts{std::move(provider), perEntity});
}
const PredicateType *RuleCatalog::predicate(std::string_view name) const {
    const auto found = predicates_.find(name);
    return found == predicates_.end() ? nullptr : &found->second;
}
const ActionType *RuleCatalog::action(std::string_view name) const {
    const auto found = actions_.find(name);
    return found == actions_.end() ? nullptr : &found->second;
}
std::vector<std::string> RuleCatalog::factNamespaces() const {
    std::vector<std::string> names;
    for (const auto &[name, facts] : facts_) {
        (void)facts;
        names.push_back(name);
    }
    return names;
}

std::optional<Value> RuleCatalog::read(RuleContext &context, std::string_view path) const {
    std::string_view first = path.substr(0, path.find('.'));
    std::string_view rest =
        first.size() < path.size() ? path.substr(first.size() + 1) : std::string_view{};
    Entity *subject = nullptr;
    bool scoped = false;
    if (first == "actor" || first == "target" || first == "self") {
        scoped = true;
        subject = first == "actor"    ? context.actorEntity()
                  : first == "target" ? context.targetEntity()
                                      : context.selfEntity();
        const std::string_view tail = rest;
        first = tail.substr(0, tail.find('.'));
        rest = first.size() < tail.size() ? tail.substr(first.size() + 1) : std::string_view{};
    }
    auto found = facts_.find(first);
    if (found == facts_.end() && scoped) { // "actor.name": the entity's own facts.
        const auto entityFacts = facts_.find("entity");
        if (entityFacts == facts_.end() || !subject)
            return std::nullopt;
        const std::string whole =
            std::string(first) + (rest.empty() ? "" : "." + std::string(rest));
        return entityFacts->second.provider(context, subject, whole);
    }
    if (found == facts_.end())
        return std::nullopt;
    if (found->second.perEntity) {
        if (!scoped) {
            subject = context.actorEntity();
            if (!subject)
                subject = context.selfEntity();
        }
        if (!subject)
            return std::nullopt;
    }
    return found->second.provider(context, found->second.perEntity ? subject : nullptr, rest);
}

namespace {
const char *kindName(ParamSpec::Kind kind) {
    switch (kind) {
    case ParamSpec::Kind::String:
        return "text";
    case ParamSpec::Kind::Number:
        return "a number";
    case ParamSpec::Kind::Int:
        return "a whole number";
    case ParamSpec::Kind::Bool:
        return "true or false";
    case ParamSpec::Kind::Entity:
        return "an entity";
    case ParamSpec::Kind::Ref:
        return "a name";
    case ParamSpec::Kind::Enum:
        return "one of the options";
    case ParamSpec::Kind::Condition:
        return "a condition";
    case ParamSpec::Kind::Actions:
        return "actions";
    case ParamSpec::Kind::Any:
        return "a value";
    case ParamSpec::Kind::Vec2:
        return "[x, y]";
    }
    return "a value";
}
bool isFactReference(const Json &json) {
    return json.isString() && json.asString().starts_with("$");
}
} // namespace

static void checkParams(const RuleCatalog &catalog, const std::vector<ParamSpec> &params,
                        const Json &args, const std::string &where, RuleReport &report) {
    for (const ParamSpec &param : params) {
        const Json *value = args.find(param.name);
        if (!value) {
            if (param.required)
                report.error(where + " needs '" + param.name + "'");
            continue;
        }
        if (isFactReference(*value))
            continue; // Read from a fact when it runs; nothing to check now.
        const auto wrong = [&] {
            report.error(where + ": '" + param.name + "' must be " + kindName(param.kind));
        };
        switch (param.kind) {
        case ParamSpec::Kind::String:
        case ParamSpec::Kind::Entity:
            if (!value->isString())
                wrong();
            break;
        case ParamSpec::Kind::Number:
        case ParamSpec::Kind::Int:
            if (!value->isNumber() || !std::isfinite(value->asNumber()))
                wrong();
            else if (param.kind == ParamSpec::Kind::Int &&
                     value->asNumber() != std::trunc(value->asNumber()))
                report.error(where + ": '" + param.name + "' must be a whole number");
            break;
        case ParamSpec::Kind::Bool:
            if (!value->isBool())
                wrong();
            break;
        case ParamSpec::Kind::Ref:
            if (!value->isString() || value->asString().empty()) {
                wrong();
            } else if (!param.refKind.empty() && !report.known(param.refKind, value->asString())) {
                report.error(where + ": there is no " + param.refKind + " '" + value->asString() +
                             "'");
            }
            break;
        case ParamSpec::Kind::Enum:
            if (!value->isString() || std::find(param.options.begin(), param.options.end(),
                                                value->asString()) == param.options.end())
                report.error(where + ": '" + param.name + "' must be one of " + [&] {
                    std::string list;
                    for (const std::string &option : param.options)
                        list += (list.empty() ? "" : ", ") + option;
                    return list;
                }());
            break;
        case ParamSpec::Kind::Condition: {
            auto condition = Condition::fromJson(*value);
            if (!condition)
                report.error(where + ": " + param.name + ": " + condition.error());
            else
                catalog.check(condition.value(), report);
            break;
        }
        case ParamSpec::Kind::Actions: {
            auto actions = Action::listFromJson(*value);
            if (!actions)
                report.error(where + ": " + param.name + ": " + actions.error());
            else
                catalog.check(actions.value(), report);
            break;
        }
        case ParamSpec::Kind::Any:
            if (value->isObject() || value->isArray())
                if (!valueFromJson(*value))
                    wrong();
            break;
        case ParamSpec::Kind::Vec2:
            if (!value->isArray() || value->size() != 2 || !value->at(0).isNumber() ||
                !value->at(1).isNumber())
                wrong();
            break;
        }
    }
}

void RuleCatalog::check(const Condition &condition, RuleReport &report) const {
    switch (condition.kind) {
    case Condition::Kind::Always:
    case Condition::Kind::Never:
        return;
    case Condition::Kind::All:
    case Condition::Kind::Any:
    case Condition::Kind::Not:
        for (const Condition &child : condition.children)
            check(child, report);
        return;
    case Condition::Kind::Compare: {
        const std::string head = condition.fact.substr(0, condition.fact.find('.'));
        std::string ns = head;
        if (head == "actor" || head == "target" || head == "self") {
            const auto rest =
                condition.fact.substr(std::min(condition.fact.size(), head.size() + 1));
            ns = rest.substr(0, rest.find('.'));
        }
        if (!facts_.contains(ns))
            report.error("condition: unknown fact '" + condition.fact + "' (no '" + ns +
                         "' facts are registered)");
        return;
    }
    case Condition::Kind::Predicate: {
        const PredicateType *type = predicate(condition.type);
        if (!type) {
            report.error("unknown condition '" + condition.type + "'");
            return;
        }
        checkParams(*this, type->params, condition.args, "condition '" + condition.type + "'",
                    report);
        if (type->check)
            type->check(condition.args, report);
        return;
    }
    }
}
void RuleCatalog::check(const std::vector<Action> &actions, RuleReport &report) const {
    for (const Action &action : actions) {
        const ActionType *type = this->action(action.type);
        if (!type) {
            report.error("unknown action '" + action.type + "'");
            continue;
        }
        checkParams(*this, type->params, action.args, "action '" + action.type + "'", report);
        if (type->check)
            type->check(action.args, report);
    }
}

// ---- Rules --------------------------------------------------------------------------------------
Result<Rule> Rule::fromJson(const Json &json) {
    if (!json.isObject())
        return Error{"a rule must be an object"};
    Rule rule;
    rule.id = json.get("id").asString();
    rule.enabled = json.get("enabled").asBool(true);
    const Json &when = json.get("when");
    if (when.isString()) {
        rule.event = when.asString();
    } else if (when.isObject()) {
        rule.event = when.get("event").asString();
        rule.every = when.get("every").asNumber(0.0);
        rule.after = when.get("after").asNumber(0.0);
        rule.source = when.get("source").asString();
        rule.other = when.get("other").asString();
        if (when.contains("data")) {
            if (!when.get("data").isObject())
                return Error{"'when.data' must be an object of values the event data must match"};
            rule.dataFilter = when.get("data");
        }
    } else {
        return Error{"a rule needs 'when': an event name or {\"event\": ..., \"every\": ..., "
                     "\"after\": ...}"};
    }
    if (rule.every < 0.0 || rule.after < 0.0 || !std::isfinite(rule.every) ||
        !std::isfinite(rule.after))
        return Error{"'every' and 'after' must be positive times in seconds"};
    if (rule.event.empty() && !(rule.every > 0.0) && !(rule.after > 0.0))
        return Error{"'when' needs an event, or 'every' / 'after' seconds"};
    auto condition = Condition::fromJson(json.get("if"));
    if (!condition)
        return Error{"if: " + condition.error()};
    rule.condition = std::move(condition.value());
    auto then = Action::listFromJson(json.get("then"));
    if (!then)
        return Error{"then: " + then.error()};
    rule.then = std::move(then.value());
    auto otherwise = Action::listFromJson(json.get("else"));
    if (!otherwise)
        return Error{"else: " + otherwise.error()};
    rule.otherwise = std::move(otherwise.value());
    rule.once = json.get("once").asBool(false);
    rule.cooldown = json.get("cooldown").asNumber(0.0);
    rule.priority = static_cast<int>(json.get("priority").asInt(0));
    if (rule.then.empty() && rule.otherwise.empty())
        return Error{"a rule needs 'then' (or 'else') actions"};
    return rule;
}
Json Rule::toJson() const {
    Json json = Json::object();
    if (!id.empty())
        json.set("id", id);
    if (!enabled)
        json.set("enabled", false);
    Json when = Json::object();
    if (!event.empty())
        when.set("event", event);
    if (every > 0.0)
        when.set("every", every);
    if (after > 0.0)
        when.set("after", after);
    if (!source.empty())
        when.set("source", source);
    if (!other.empty())
        when.set("other", other);
    if (dataFilter.isObject())
        when.set("data", dataFilter);
    json.set("when", when);
    if (!condition.empty())
        json.set("if", condition.toJson());
    Json thenList = Json::array();
    for (const Action &action : then)
        thenList.push(action.toJson());
    json.set("then", thenList);
    if (!otherwise.empty()) {
        Json elseList = Json::array();
        for (const Action &action : otherwise)
            elseList.push(action.toJson());
        json.set("else", elseList);
    }
    if (once)
        json.set("once", true);
    if (cooldown > 0.0)
        json.set("cooldown", cooldown);
    if (priority != 0)
        json.set("priority", priority);
    return json;
}
Result<std::vector<Rule>> rulesFromJson(const Json &json) {
    std::vector<Rule> rules;
    if (json.isNull())
        return rules;
    if (!json.isArray())
        return Error{"rules must be a list"};
    std::set<std::string> ids;
    for (std::size_t i = 0; i < json.size(); ++i) {
        auto rule = Rule::fromJson(json.at(i));
        if (!rule)
            return Error{"rule " + std::to_string(i + 1) +
                         (json.at(i).get("id").isString()
                              ? " ('" + json.at(i).get("id").asString() + "')"
                              : "") +
                         ": " + rule.error()};
        if (!rule.value().id.empty() && !ids.insert(rule.value().id).second)
            return Error{"two rules are called '" + rule.value().id + "'"};
        rules.push_back(std::move(rule.value()));
    }
    return rules;
}
Json rulesToJson(const std::vector<Rule> &rules) {
    Json list = Json::array();
    for (const Rule &rule : rules)
        list.push(rule.toJson());
    return list;
}
void check(const RuleCatalog &catalog, const Rule &rule, RuleReport &report) {
    catalog.check(rule.condition, report);
    catalog.check(rule.then, report);
    catalog.check(rule.otherwise, report);
}
} // namespace yk
