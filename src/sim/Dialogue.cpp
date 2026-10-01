#include "yk/sim/Dialogue.hpp"
#include "yk/components/Components.hpp"
#include "yk/data/Table.hpp"
#include "yk/runtime/GameContext.hpp"
#include <algorithm>
#include <set>

namespace yk {
const DialogueNode *DialogueGraph::node(std::string_view id) const {
    for (const DialogueNode &candidate : nodes)
        if (candidate.id == id)
            return &candidate;
    return nullptr;
}
const DialogueSpeaker *DialogueGraph::speaker(std::string_view id) const {
    const auto found = speakers.find(std::string(id));
    return found == speakers.end() ? nullptr : &found->second;
}

namespace {
Result<std::vector<Action>> actionsOf(const Json &json, const std::string &where) {
    auto actions = Action::listFromJson(json);
    if (!actions)
        return Error{where + actions.error()};
    return actions.value();
}
Result<Condition> conditionOf(const Json &json, const std::string &where) {
    auto condition = Condition::fromJson(json);
    if (!condition)
        return Error{where + condition.error()};
    return condition.value();
}
} // namespace

Result<DialogueGraph> DialogueGraph::fromJson(const Json &json,
                                              std::vector<std::string> &warnings) {
    if (!json.isObject())
        return Error{"a dialogue is an object with 'pages' or 'nodes'"};
    DialogueGraph graph;
    if (json.contains("version") && json.get("version").asInt(1) > 2)
        return Error{"this dialogue is version " + std::to_string(json.get("version").asInt()) +
                     " but this engine reads up to version 2"};
    if (json.contains("pages") && !json.contains("nodes")) {
        // The linear form: each page leads to the next.
        const Json &pages = json.get("pages");
        if (!pages.isArray() || pages.size() == 0)
            return Error{"dialogue needs a nonempty pages array"};
        for (std::size_t i = 0; i < pages.size(); ++i) {
            const Json &page = pages.at(i);
            DialogueNode node;
            node.id = "page" + std::to_string(i + 1);
            if (page.isString()) {
                node.text = page.asString();
            } else if (page.isObject()) {
                node.speaker = page.get("speaker").asString();
                node.text = page.get("text").asString();
                node.portrait = page.get("portrait").asString();
            }
            if (node.text.empty())
                return Error{"page " + std::to_string(i + 1) + " needs text"};
            node.next = i + 1 < pages.size() ? "page" + std::to_string(i + 2) : std::string();
            graph.nodes.push_back(std::move(node));
        }
        graph.start = "page1";
        return graph;
    }
    const Json &nodes = json.get("nodes");
    if (!nodes.isObject() || nodes.size() == 0)
        return Error{"a dialogue graph needs 'nodes': an object of nodes by id"};
    if (json.contains("speakers")) {
        const Json &speakers = json.get("speakers");
        if (!speakers.isObject())
            return Error{"'speakers' must be an object of speakers by id"};
        for (std::size_t i = 0; i < speakers.size(); ++i) {
            const Json &entry = speakers.valueAt(i);
            DialogueSpeaker speaker;
            speaker.name = data::optionalString(entry, "name", speakers.keyAt(i));
            speaker.side = data::optionalString(entry, "side", "left");
            if (speaker.side != "left" && speaker.side != "right")
                return Error{"speaker '" + speakers.keyAt(i) +
                             "': 'side' is \"left\" or \"right\""};
            const Json &portraits = entry.get("portraits");
            for (std::size_t k = 0; k < portraits.size(); ++k) {
                if (!portraits.valueAt(k).isString())
                    return Error{"speaker '" + speakers.keyAt(i) + "': the portrait '" +
                                 portraits.keyAt(k) + "' must be an image path"};
                speaker.portraits[portraits.keyAt(k)] = portraits.valueAt(k).asString();
            }
            graph.speakers[speakers.keyAt(i)] = std::move(speaker);
        }
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Json &entry = nodes.valueAt(i);
        DialogueNode node;
        node.id = nodes.keyAt(i);
        const std::string where = "node '" + node.id + "': ";
        if (!data::validId(node.id))
            return Error{where + "'" + node.id +
                         "' is not a usable id (letters, digits, '_' and '-')"};
        if (!entry.isObject())
            return Error{where + "must be an object"};
        node.speaker = data::optionalString(entry, "speaker");
        node.text = data::optionalString(entry, "text");
        node.portrait = data::optionalString(entry, "portrait");
        node.expression = data::optionalString(entry, "expression");
        node.side = data::optionalString(entry, "side");
        if (!node.side.empty() && node.side != "left" && node.side != "right")
            return Error{where + "'side' is \"left\" or \"right\""};
        node.next = entry.get("next").isString() ? entry.get("next").asString() : std::string();
        auto actions = actionsOf(entry.get("actions"), where + "actions: ");
        if (!actions)
            return Error{actions.error()};
        node.actions = std::move(actions.value());
        const Json &choices = entry.get("choices");
        if (entry.contains("choices") && !choices.isArray())
            return Error{where + "'choices' must be a list"};
        for (std::size_t k = 0; k < choices.size(); ++k) {
            const Json &choice = choices.at(k);
            const std::string place = where + "choice " + std::to_string(k + 1) + ": ";
            if (!choice.isObject() || !choice.get("text").isString() ||
                choice.get("text").asString().empty())
                return Error{place + "needs 'text'"};
            DialogueChoice item;
            item.text = choice.get("text").asString();
            auto condition = conditionOf(choice.get("if"), place + "if: ");
            if (!condition)
                return Error{condition.error()};
            item.condition = std::move(condition.value());
            auto choiceActions = actionsOf(choice.get("actions"), place + "actions: ");
            if (!choiceActions)
                return Error{choiceActions.error()};
            item.actions = std::move(choiceActions.value());
            item.next =
                choice.get("next").isString() ? choice.get("next").asString() : std::string();
            item.once = choice.get("once").asBool(false);
            node.choices.push_back(std::move(item));
        }
        const Json &branches = entry.get("branch");
        if (entry.contains("branch") && !branches.isArray())
            return Error{where + "'branch' must be a list"};
        for (std::size_t k = 0; k < branches.size(); ++k) {
            const Json &branch = branches.at(k);
            const std::string place = where + "branch " + std::to_string(k + 1) + ": ";
            if (!branch.isObject())
                return Error{place + "must be an object like {\"if\": ..., \"next\": \"node\"}"};
            DialogueBranch item;
            auto condition = conditionOf(branch.get("if"), place + "if: ");
            if (!condition)
                return Error{condition.error()};
            item.condition = std::move(condition.value());
            item.next =
                branch.get("next").isString() ? branch.get("next").asString() : std::string();
            node.branches.push_back(std::move(item));
        }
        graph.nodes.push_back(std::move(node));
    }
    // Every way on must lead to a node.
    for (const DialogueNode &node : graph.nodes) {
        const std::string where = "node '" + node.id + "': ";
        const auto exists = [&](const std::string &id) {
            return id.empty() || graph.node(id) != nullptr;
        };
        if (!exists(node.next))
            return Error{where + "'next' names '" + node.next + "', which is not a node"};
        for (std::size_t k = 0; k < node.choices.size(); ++k)
            if (!exists(node.choices[k].next))
                return Error{where + "choice " + std::to_string(k + 1) + " goes to '" +
                             node.choices[k].next + "', which is not a node"};
        for (std::size_t k = 0; k < node.branches.size(); ++k)
            if (!exists(node.branches[k].next))
                return Error{where + "branch " + std::to_string(k + 1) + " goes to '" +
                             node.branches[k].next + "', which is not a node"};
        if (node.text.empty() && node.choices.empty() && node.actions.empty() &&
            node.branches.empty() && node.next.empty())
            warnings.push_back(where + "has no text, no choices and nothing to do");
        if (!node.speaker.empty() && !graph.speakers.empty() &&
            !graph.speakers.contains(node.speaker))
            warnings.push_back(where + "the speaker '" + node.speaker +
                               "' is not in 'speakers', so it is shown as written");
        if (!node.expression.empty()) {
            const DialogueSpeaker *speaker = graph.speaker(node.speaker);
            if (speaker && !speaker->portraits.contains(node.expression))
                warnings.push_back(where + "the speaker '" + node.speaker +
                                   "' has no portrait for the expression '" + node.expression +
                                   "'");
        }
    }
    graph.start = data::optionalString(json, "start", graph.nodes.front().id);
    if (!graph.node(graph.start))
        return Error{"'start' names '" + graph.start + "', which is not a node"};
    data::warnUnknown(json, {"format", "version", "start", "speakers", "nodes"}, warnings);
    return graph;
}

Json DialogueGraph::toJson() const {
    Json json = Json::object();
    json.set("format", "yk.dialogue");
    json.set("version", 2);
    json.set("start", start);
    if (!speakers.empty()) {
        Json list = Json::object();
        for (const auto &[id, speaker] : speakers) {
            Json entry = Json::object();
            entry.set("name", speaker.name);
            if (speaker.side != "left")
                entry.set("side", speaker.side);
            Json portraits = Json::object();
            for (const auto &[expression, path] : speaker.portraits)
                portraits.set(expression, path);
            entry.set("portraits", portraits);
            list.set(id, entry);
        }
        json.set("speakers", list);
    }
    Json list = Json::object();
    const auto writeActions = [](Json &into, const char *key, const std::vector<Action> &actions) {
        if (actions.empty())
            return;
        Json array = Json::array();
        for (const Action &action : actions)
            array.push(action.toJson());
        into.set(key, array);
    };
    for (const DialogueNode &node : nodes) {
        Json entry = Json::object();
        if (!node.speaker.empty())
            entry.set("speaker", node.speaker);
        if (!node.expression.empty())
            entry.set("expression", node.expression);
        if (!node.portrait.empty())
            entry.set("portrait", node.portrait);
        if (!node.side.empty())
            entry.set("side", node.side);
        if (!node.text.empty())
            entry.set("text", node.text);
        writeActions(entry, "actions", node.actions);
        if (!node.choices.empty()) {
            Json choices = Json::array();
            for (const DialogueChoice &choice : node.choices) {
                Json item = Json::object();
                item.set("text", choice.text);
                if (!choice.condition.empty())
                    item.set("if", choice.condition.toJson());
                writeActions(item, "actions", choice.actions);
                if (!choice.next.empty())
                    item.set("next", choice.next);
                if (choice.once)
                    item.set("once", true);
                choices.push(item);
            }
            entry.set("choices", choices);
        }
        if (!node.branches.empty()) {
            Json branches = Json::array();
            for (const DialogueBranch &branch : node.branches) {
                Json item = Json::object();
                if (!branch.condition.empty())
                    item.set("if", branch.condition.toJson());
                item.set("next", branch.next);
                branches.push(item);
            }
            entry.set("branch", branches);
        }
        if (!node.next.empty())
            entry.set("next", node.next);
        list.set(node.id, entry);
    }
    json.set("nodes", list);
    return json;
}

void DialogueGraph::check(std::vector<std::string> &warnings) const {
    std::set<std::string> reached;
    std::vector<std::string> pending{start};
    while (!pending.empty()) {
        const std::string id = pending.back();
        pending.pop_back();
        if (!reached.insert(id).second)
            continue;
        const DialogueNode *current = node(id);
        if (!current)
            continue;
        if (!current->next.empty())
            pending.push_back(current->next);
        for (const DialogueChoice &choice : current->choices)
            if (!choice.next.empty())
                pending.push_back(choice.next);
        for (const DialogueBranch &branch : current->branches)
            if (!branch.next.empty())
                pending.push_back(branch.next);
    }
    for (const DialogueNode &current : nodes) {
        if (!reached.contains(current.id))
            warnings.push_back("node '" + current.id + "' can never be reached from '" + start +
                               "'");
        if (!current.choices.empty() && (!current.next.empty() || !current.branches.empty()))
            warnings.push_back("node '" + current.id +
                               "' has choices, so its 'next' and 'branch' are never used");
        if (!current.choices.empty() &&
            std::all_of(current.choices.begin(), current.choices.end(),
                        [](const DialogueChoice &c) { return !c.condition.empty(); }))
            warnings.push_back(
                "node '" + current.id +
                "': every choice has a condition; if none holds the conversation ends there");
    }
}

void DialogueGraph::visitRules(const std::string &file, const RuleSourceVisitor &visit) const {
    for (const DialogueNode &current : nodes) {
        const std::string where = "dialogue node '" + current.id + "'";
        if (!current.actions.empty())
            visit({file, where + " actions", nullptr, &current.actions});
        for (std::size_t k = 0; k < current.choices.size(); ++k) {
            const DialogueChoice &choice = current.choices[k];
            const std::string place = where + " choice " + std::to_string(k + 1);
            if (!choice.condition.empty())
                visit({file, place + " if", &choice.condition, nullptr});
            if (!choice.actions.empty())
                visit({file, place + " actions", nullptr, &choice.actions});
        }
        for (std::size_t k = 0; k < current.branches.size(); ++k)
            if (!current.branches[k].condition.empty())
                visit({file, where + " branch " + std::to_string(k + 1) + " if",
                       &current.branches[k].condition, nullptr});
    }
}

std::vector<std::string> DialogueGraph::images() const {
    std::vector<std::string> list;
    for (const DialogueNode &current : nodes)
        if (!current.portrait.empty())
            list.push_back(current.portrait);
    for (const auto &[id, speaker] : speakers) {
        (void)id;
        for (const auto &[expression, path] : speaker.portraits) {
            (void)expression;
            list.push_back(path);
        }
    }
    return list;
}

// ---- A walk through a graph
// -----------------------------------------------------------------------
DialogueSession::DialogueSession(std::shared_ptr<const DialogueGraph> graph, std::string graphId)
    : graph_(std::move(graph)), graphId_(std::move(graphId)) {}

const DialogueNode *DialogueSession::node() const {
    return node_;
}

bool DialogueSession::begin(RuleContext &context) {
    if (!graph_ || graph_->nodes.empty())
        return false;
    finished_ = false;
    enter(context, graph_->start);
    return true;
}

void DialogueSession::enter(RuleContext &context, const std::string &startId) {
    std::string id = startId;
    for (int guard = 0; guard < 64; ++guard) { // Nodes with nothing to show pass straight through.
        node_ = graph_->node(id);
        if (!node_) {
            finished_ = true;
            return;
        }
        execute(node_->actions, context);
        if (!node_->text.empty() || !node_->choices.empty()) {
            // Present it.
            const DialogueSpeaker *speaker = graph_->speaker(node_->speaker);
            line_ = DialogueLine{};
            line_.speaker = speaker ? speaker->name : node_->speaker;
            line_.text = context.game.blackboard().format(node_->text);
            line_.expression = node_->expression;
            line_.side = !node_->side.empty() ? node_->side : (speaker ? speaker->side : "left");
            if (!node_->portrait.empty())
                line_.portrait = node_->portrait;
            else if (speaker) {
                const auto expression = speaker->portraits.find(node_->expression);
                const auto usual = speaker->portraits.find("default");
                line_.portrait = expression != speaker->portraits.end() ? expression->second
                                 : usual != speaker->portraits.end()    ? usual->second
                                                                        : std::string();
            }
            offered_.clear();
            for (std::size_t k = 0; k < node_->choices.size(); ++k) {
                const DialogueChoice &choice = node_->choices[k];
                const std::string seen =
                    "dialogue.seen." + graphId_ + "." + node_->id + "." + std::to_string(k);
                if (choice.once && context.game.blackboard().flag(seen))
                    continue;
                if (!evaluate(choice.condition, context))
                    continue;
                offered_.push_back(static_cast<int>(k));
                line_.choices.push_back(context.game.blackboard().format(choice.text));
            }
            if (!node_->choices.empty() && offered_.empty()) {
                finished_ = true; // Nothing to say and nothing to choose: the conversation ends.
                return;
            }
            return;
        }
        // A node with no text: follow it on.
        std::string next = node_->next;
        for (const DialogueBranch &branch : node_->branches)
            if (evaluate(branch.condition, context)) {
                next = branch.next;
                break;
            }
        if (next.empty()) {
            finished_ = true;
            return;
        }
        id = next;
    }
    finished_ = true; // A loop of nodes with nothing to show.
}

bool DialogueSession::advance(RuleContext &context, int pick) {
    if (finished_ || !node_)
        return false;
    std::string next;
    if (!node_->choices.empty()) {
        if (pick < 0 || pick >= static_cast<int>(offered_.size())) {
            return true; // Not a choice on offer: stay.
        }
        const std::size_t index =
            static_cast<std::size_t>(offered_[static_cast<std::size_t>(pick)]);
        const DialogueChoice &choice = node_->choices[index];
        if (choice.once)
            context.game.blackboard().setBool(
                "dialogue.seen." + graphId_ + "." + node_->id + "." + std::to_string(index), true);
        execute(choice.actions, context);
        next = choice.next;
    } else {
        next = node_->next;
        for (const DialogueBranch &branch : node_->branches)
            if (evaluate(branch.condition, context)) {
                next = branch.next;
                break;
            }
    }
    if (next.empty()) {
        finished_ = true;
        return false;
    }
    enter(context, next);
    return !finished_;
}

// ---- Rules
// ------------------------------------------------------------------------------------------
void registerDialogueRules(RuleCatalog &catalog) {
    using Kind = ParamSpec::Kind;
    catalog.addAction(
        {"StartDialogue",
         "Dialogue",
         "Starts the conversation of an entity (its Dialogue component) with the actor.",
         {ParamSpec::make("entity", Kind::Entity, false, "default the target, else self")},
         [](const Json &args, RuleContext &context) {
             Entity *talker = context.resolveOne(
                 args.contains("entity") ? args.get("entity").asString() : "target");
             if (!talker)
                 talker = context.selfEntity();
             auto *dialogue = talker ? talker->get<Dialogue>() : nullptr;
             if (!dialogue || dialogue->active())
                 return ActionResult::Failed;
             dialogue->begin(context.game, context.actor);
             return dialogue->active() ? ActionResult::Done : ActionResult::Failed;
         },
         nullptr});
    catalog.addAction({"EndDialogue",
                       "Dialogue",
                       "Ends the conversation of an entity.",
                       {ParamSpec::make("entity", Kind::Entity, false, "default self")},
                       [](const Json &args, RuleContext &context) {
                           bool done = false;
                           for (Entity *entity : context.entitiesFrom(args, "entity", "self"))
                               if (auto *dialogue = entity->get<Dialogue>();
                                   dialogue && dialogue->active()) {
                                   dialogue->close(context.game);
                                   done = true;
                               }
                           return done ? ActionResult::Done : ActionResult::Failed;
                       },
                       nullptr});
    catalog.addPredicate({"DialogueActive",
                          "Dialogue",
                          "True while the entity's conversation is on screen.",
                          {ParamSpec::make("entity", Kind::Entity, false, "default self")},
                          [](const Json &args, RuleContext &context) {
                              for (Entity *entity : context.entitiesFrom(args, "entity", "self"))
                                  if (const auto *dialogue = entity->get<Dialogue>();
                                      dialogue && dialogue->active())
                                      return true;
                              return false;
                          },
                          nullptr});
}
} // namespace yk
