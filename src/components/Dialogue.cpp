#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/runtime/GameContext.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
void Dialogue::describe(TypeBuilder<Dialogue> &type) {
    type.category("UI").description(
        "Screen-space conversation with optional portrait and JSON pages.");
    type.field("sequence", &Dialogue::sequence)
        .asset("dialogue")
        .tooltip(
            "Optional .ykdialogue JSON asset with pages of {speaker, text, portrait} objects.");
    type.field("speaker", &Dialogue::speaker);
    type.field("pages", &Dialogue::pages).tooltip("Inline pages used when sequence is empty.");
    type.field("portrait", &Dialogue::portrait)
        .asset("texture")
        .tooltip("Default high-resolution portrait for every page.");
    type.field("advanceSet", &Dialogue::advanceSet).inputSet();
    type.field("advanceAction", &Dialogue::advanceAction).inputAction();
    type.field("charactersPerSecond", &Dialogue::charactersPerSecond).range(0, 300, 1);
}

void Dialogue::begin(GameContext &context) {
    if (active_)
        return;
    loaded_.clear();
    if (!sequence.path.empty() && context.assets()) {
        auto data = context.assets()->readText(sequence.path);
        auto document = data ? Json::parse(data.value()) : Result<Json>(Error{data.error()});
        if (document && document.value().get("pages").isArray()) {
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
                    (document ? std::string("missing pages array") : document.error()));
        }
    }
    if (loaded_.empty())
        for (const auto &text : pages)
            if (!text.empty())
                loaded_.push_back({speaker, text, portrait});
    if (loaded_.empty())
        return;
    page_ = 0;
    visibleCharacters_ =
        charactersPerSecond > 0 ? 0.0F : static_cast<float>(loaded_[0].text.size());
    startedTick_ = context.tick();
    active_ = true;
    context.lockInput("dialogue:" + toString(entity().id()), true);
    context.emit("conversation_started", entity().id());
}

std::string Dialogue::visibleText() const {
    if (!active_ || loaded_.empty())
        return {};
    const std::string &text = loaded_[page_].text;
    return text.substr(0, std::min(text.size(), static_cast<std::size_t>(visibleCharacters_)));
}

void Dialogue::end(GameContext &context) {
    if (!active_)
        return;
    active_ = false;
    context.lockInput("dialogue:" + toString(entity().id()), false);
    context.emit("conversation_finished", entity().id());
}

void Dialogue::onFixedUpdate(GameContext &context, float seconds) {
    if (!active_)
        return;
    const auto &text = loaded_[page_].text;
    visibleCharacters_ =
        std::min(static_cast<float>(text.size()),
                 visibleCharacters_ + std::max(0.0F, charactersPerSecond) * seconds);
    if (context.tick() == startedTick_ || !context.input().state(advanceSet, advanceAction).pressed)
        return;
    if (visibleCharacters_ < static_cast<float>(text.size())) {
        visibleCharacters_ = static_cast<float>(text.size());
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
