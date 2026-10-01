#pragma once
#include "yk/rules/Rules.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

// Conversations as graphs. A `.ykdialogue` file is either the old linear list of pages
// ({"pages": [...]}, which keeps working) or a graph of nodes. A node has a speaker, text, a
// portrait and expression, actions that run when it is shown, and a way on: choices the player
// picks from (each with an optional condition, actions and a destination), conditional branches
// (the first whose condition holds), or a plain `next`. Conditions and actions are the rule
// language, so a choice can need an item or a relationship and a reply can start a quest.
//
//   {"start": "greet",
//    "speakers": {"mara": {"name": "Keeper Mara", "side": "left",
//                          "portraits": {"default": "assets/mara.png", "smile":
//                          "assets/mara_smile.png"}}},
//    "nodes": {
//      "greet": {"speaker": "mara", "text": "Hello, {player_name}.",
//                "choices": [{"text": "I need a screwdriver.", "if": {"type": "HasItem", "item":
//                "favor_token"},
//                             "actions": [{"type": "GiveItem", "item": "screwdriver"}], "next":
//                             "thanks"},
//                            {"text": "Goodbye."}]},
//      "thanks": {"speaker": "mara", "expression": "smile", "text": "Do not let anyone see it."}}}
namespace yk {
struct DialogueChoice {
    std::string text;
    Condition condition; // "if" in the file
    std::vector<Action> actions;
    std::string next; // Empty: the conversation ends.
    bool once{false}; // Gone once picked (remembered in the game's variables).
};
struct DialogueBranch {
    Condition condition; // Empty: otherwise.
    std::string next;
};
struct DialogueSpeaker {
    std::string name;
    std::string side{"left"};
    std::map<std::string, std::string>
        portraits; // Expression -> image ("default" is the usual one).
};
struct DialogueNode {
    std::string id;
    std::string speaker; // A key of `speakers`, or a name.
    std::string text;
    std::string portrait;   // An image that overrides the speaker's.
    std::string expression; // Which of the speaker's portraits.
    std::string side;       // "left" or "right"; empty: the speaker's.
    std::vector<Action> actions;
    std::vector<DialogueChoice> choices;
    std::vector<DialogueBranch> branches;
    std::string next;
};

struct DialogueGraph {
    std::string start;
    std::map<std::string, DialogueSpeaker> speakers;
    std::vector<DialogueNode> nodes; // In file order.

    const DialogueNode *node(std::string_view id) const;
    const DialogueSpeaker *speaker(std::string_view id) const;
    // Reads either form; a linear file becomes a chain of nodes. Refuses a graph whose references
    // do not lead anywhere.
    static Result<DialogueGraph> fromJson(const Json &json, std::vector<std::string> &warnings);
    Json toJson() const;
    // What is wrong that parsing could not tell: unreachable nodes, a node that is both a choice
    // and a continuation.
    void check(std::vector<std::string> &warnings) const;
    // Hands the validator every condition and action list: label, condition, actions.
    void visitRules(const std::string &file, const RuleSourceVisitor &visit) const;
    // The image files the graph names.
    std::vector<std::string> images() const;
};

// What the player sees at one node.
struct DialogueLine {
    std::string speaker; // The name to show.
    std::string text;    // With {variables} filled in.
    std::string portrait;
    std::string side{"left"};
    std::string expression;
    std::vector<std::string> choices; // The choices on offer, in order.
};

// One walk through a graph. It needs a RuleContext for conditions and actions (self: the owner of
// the conversation, actor: who is talking to it).
class DialogueSession {
  public:
    DialogueSession(std::shared_ptr<const DialogueGraph> graph, std::string graphId);
    // Enters the start node (its actions run). False when the graph has no nodes.
    bool begin(RuleContext &context);
    bool finished() const {
        return finished_;
    }
    const DialogueNode *node() const;
    const DialogueLine &line() const {
        return line_;
    }
    // The choices on offer at this node (indices into the node's choices).
    const std::vector<int> &offered() const {
        return offered_;
    }
    // Leaves the node: with choices, by the one picked (an index into offered()); otherwise by
    // the first branch that holds or `next`. False when it ended the conversation.
    bool advance(RuleContext &context, int pick = 0);

  private:
    void enter(RuleContext &context, const std::string &id);
    std::shared_ptr<const DialogueGraph> graph_;
    std::string graphId_;
    const DialogueNode *node_{nullptr};
    DialogueLine line_;
    std::vector<int> offered_;
    bool finished_{false};
};

void registerDialogueRules(RuleCatalog &catalog);
} // namespace yk
