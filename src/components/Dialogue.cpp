#include "yk/sim/Dialogue.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/runtime/GameContext.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
void Dialogue::describe(TypeBuilder<Dialogue> &type) {
    type.category("UI").description(
        "Screen-space conversation with optional portrait: pages of text, or a graph with choices, "
        "conditions and actions.");
    type.field("sequence", &Dialogue::sequence)
        .asset("dialogue")
        .tooltip(
            "Optional .ykdialogue JSON asset: pages of {speaker, text, portrait} objects, or a "
            "graph of nodes with choices, branches and actions.");
    type.field("speaker", &Dialogue::speaker);
    type.field("pages", &Dialogue::pages).tooltip("Inline pages used when sequence is empty.");
    type.field("portrait", &Dialogue::portrait)
        .asset("texture")
        .tooltip("Default high-resolution portrait for every page.");
    type.field("advanceSet", &Dialogue::advanceSet).inputSet();
    type.field("advanceAction", &Dialogue::advanceAction).inputAction();
    type.field("upAction", &Dialogue::upAction).inputAction();
    type.field("downAction", &Dialogue::downAction).inputAction();
    type.field("charactersPerSecond", &Dialogue::charactersPerSecond).range(0, 300, 1);
    type.field("lockInput", &Dialogue::lockInput)
        .tooltip("Movement and actions stop while the conversation is open.");
}

bool Dialogue::choosing() const {
    return active_ && session_ && !choices_.empty() &&
           visibleCharacters_ >= static_cast<float>(graphPage_.text.size());
}

void Dialogue::showNode(GameContext &context) {
    const DialogueLine &line = session_->line();
    graphPage_.speaker = line.speaker.empty() ? speaker : line.speaker;
    graphPage_.text = line.text;
    graphPage_.portrait = line.portrait.empty() ? portrait : AssetRef{line.portrait};
    graphPage_.side = line.side;
    graphPage_.expression = line.expression;
    choices_ = line.choices;
    selected_ = 0;
    visibleCharacters_ =
        charactersPerSecond > 0 ? 0.0F : static_cast<float>(graphPage_.text.size());
    (void)context;
}

void Dialogue::begin(GameContext &context, EntityId actor) {
    if (active_)
        return;
    loaded_.clear();
    session_.reset();
    choices_.clear();
    actor_ = actor;
    source_ = sequence.path;
    if (!sequence.path.empty() && context.assets()) {
        auto data = context.assets()->readText(sequence.path);
        auto document = data ? Json::parse(data.value()) : Result<Json>(Error{data.error()});
        if (document && document.value().get("nodes").isObject()) {
            std::vector<std::string> warnings;
            auto graph = DialogueGraph::fromJson(document.value(), warnings);
            if (!graph) {
                log(LogLevel::Warning, "dialogue",
                    "Cannot load conversation " + sequence.path + ": " + graph.error());
            } else {
                session_ = std::make_shared<DialogueSession>(
                    std::make_shared<const DialogueGraph>(std::move(graph.value())), sequence.path);
                RuleContext rules(context);
                rules.self = entity().id();
                rules.actor = actor_;
                rules.target = entity().id();
                rules.origin = "dialogue '" + sequence.path + "' of '" + entity().name() + "'";
                if (!session_->begin(rules) || session_->finished()) {
                    session_.reset(); // Nothing to show.
                    return;
                }
                showNode(context);
            }
        } else if (document && document.value().get("pages").isArray()) {
            for (const Json &item : document.value().get("pages").items()) {
                DialoguePage page;
                if (item.isString()) {
                    page.text = item.asString();
                } else if (item.isObject()) {
                    page.speaker = item.get("speaker").asString();
                    page.text = item.get("text").asString();
                    page.portrait.path = item.get("portrait").asString();
                }
                if (!page.text.empty()) {
                    if (page.speaker.empty())
                        page.speaker = speaker;
                    if (page.portrait.path.empty())
                        page.portrait = portrait;
                    loaded_.push_back(std::move(page));
                }
            }
        } else {
            log(LogLevel::Warning, "dialogue",
                "Cannot load conversation " + sequence.path + ": " +
                    (document ? std::string("missing pages array or nodes") : document.error()));
        }
    }
    if (!session_ && loaded_.empty())
        for (const auto &text : pages)
            if (!text.empty())
                loaded_.push_back({speaker, text, portrait, "left", {}});
    if (!session_) {
        if (loaded_.empty())
            return;
        page_ = 0;
        visibleCharacters_ =
            charactersPerSecond > 0 ? 0.0F : static_cast<float>(loaded_[0].text.size());
    }
    startedTick_ = context.tick();
    active_ = true;
    if (lockInput)
        context.lockInput("dialogue:" + toString(entity().id()), true);
    context.emit("conversation_started", entity().id(), actor_);
}

std::string Dialogue::visibleText() const {
    if (!active_)
        return {};
    const std::string &text = session_ ? graphPage_.text : loaded_[page_].text;
    return text.substr(0, std::min(text.size(), static_cast<std::size_t>(visibleCharacters_)));
}

void Dialogue::end(GameContext &context) {
    if (!active_)
        return;
    active_ = false;
    session_.reset();
    choices_.clear();
    if (lockInput)
        context.lockInput("dialogue:" + toString(entity().id()), false);
    context.emit("conversation_finished", entity().id(), actor_);
}

void Dialogue::close(GameContext &context) {
    end(context);
}

void Dialogue::onFixedUpdate(GameContext &context, float seconds) {
    if (!active_)
        return;
    const std::string &text = session_ ? graphPage_.text : loaded_[page_].text;
    visibleCharacters_ =
        std::min(static_cast<float>(text.size()),
                 visibleCharacters_ + std::max(0.0F, charactersPerSecond) * seconds);
    if (context.tick() == startedTick_)
        return;
    const bool shown = visibleCharacters_ >= static_cast<float>(text.size());
    if (choosing() && !choices_.empty()) {
        const int count = static_cast<int>(choices_.size());
        if (context.input().state(advanceSet, upAction).pressed)
            selected_ = (selected_ + count - 1) % count;
        if (context.input().state(advanceSet, downAction).pressed)
            selected_ = (selected_ + 1) % count;
    }
    if (!context.input().state(advanceSet, advanceAction).pressed)
        return;
    if (!shown) {
        visibleCharacters_ = static_cast<float>(text.size());
        return;
    }
    if (session_) {
        RuleContext rules(context);
        rules.self = entity().id();
        rules.actor = actor_;
        rules.target = entity().id();
        rules.origin = "dialogue '" + source_ + "' of '" + entity().name() + "'";
        const DialogueNode *node = session_->node();
        if (node && !choices_.empty()) {
            Json data = Json::object();
            data.set("node", node->id);
            data.set("choice", selected_);
            data.set("text", choices_[static_cast<std::size_t>(selected_)]);
            context.events().emit(
                GameEvent("dialogue.choice", entity().id(), actor_, std::move(data)));
        }
        if (!session_->advance(rules, selected_)) {
            end(context);
        } else {
            showNode(context);
            context.emit("conversation_page", entity().id(), actor_);
        }
    } else if (++page_ < loaded_.size()) {
        visibleCharacters_ =
            charactersPerSecond > 0 ? 0.0F : static_cast<float>(loaded_[page_].text.size());
        context.emit("conversation_page", entity().id());
    } else {
        end(context);
    }
}

void Dialogue::onDestroy(GameContext &context) {
    end(context);
}
} // namespace yk
