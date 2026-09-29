#include "KeyScript.hpp"
#include <charconv>

namespace yk {
Result<KeyScript> KeyScript::parse(std::string_view text) {
    KeyScript script;
    while (!text.empty()) {
        const auto comma = text.find(',');
        std::string_view item = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
        const auto at = item.find('@');
        const auto dash = item.find('-', at == std::string_view::npos ? 0 : at);
        if (at == std::string_view::npos || dash == std::string_view::npos)
            return Error{"Key script item '" + std::string(item) + "' must look like KEY@first-last"};
        const Key key = keyFromName(item.substr(0, at));
        if (key == Key::Count)
            return Error{"Unknown key '" + std::string(item.substr(0, at)) + "' in key script"};
        unsigned first = 0, last = 0;
        const auto firstText = item.substr(at + 1, dash - at - 1), lastText = item.substr(dash + 1);
        if (std::from_chars(firstText.data(), firstText.data() + firstText.size(), first).ec != std::errc{} ||
            std::from_chars(lastText.data(), lastText.data() + lastText.size(), last).ec != std::errc{} || last <= first)
            return Error{"Invalid frame range in key script item '" + std::string(item) + "'"};
        script.spans_.push_back({key, first, last});
    }
    return script;
}

Keyboard KeyScript::at(unsigned frame) const {
    Keyboard keyboard;
    for (const Span &span : spans_) {
        ButtonState state = keyboard.state(span.key);
        const bool held = frame >= span.first && frame < span.last;
        state.held = state.held || held;
        state.pressed = state.pressed || frame == span.first;
        state.released = state.released || (span.last > 0 && frame == span.last);
        keyboard.assign(span.key, state);
    }
    return keyboard;
}
} // namespace yk
