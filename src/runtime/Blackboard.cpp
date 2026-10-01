#include "yk/runtime/Blackboard.hpp"
#include <algorithm>

namespace yk {
void Blackboard::write(const std::string &key, Value value) {
    const auto found = values_.find(key);
    if (found != values_.end() && typeOf(found->second) == typeOf(value) &&
        valuesEqual(found->second, value))
        return; // Not a change.
    Value before = found == values_.end() ? Value{} : found->second;
    values_[key] = value;
    ++revision_;
    // Listeners may subscribe or unsubscribe while being called: iterate a copy of the ids.
    std::vector<ListenerId> ids;
    ids.reserve(listeners_.size());
    for (const auto &entry : listeners_)
        ids.push_back(entry.first);
    for (const ListenerId id : ids) {
        const auto it = std::find_if(listeners_.begin(), listeners_.end(),
                                     [&](const auto &entry) { return entry.first == id; });
        if (it != listeners_.end()) {
            const Listener listener = it->second;
            listener(key, before, value);
        }
    }
}
void Blackboard::set(const std::string &key, double value) {
    write(key, Value{value});
}
void Blackboard::set(const std::string &key, std::string value) {
    write(key, Value{std::move(value)});
}
void Blackboard::setBool(const std::string &key, bool value) {
    write(key, Value{value});
}
void Blackboard::setInt(const std::string &key, std::int64_t value) {
    write(key, Value{value});
}
void Blackboard::setEntity(const std::string &key, EntityId value) {
    write(key, Value{value});
}
void Blackboard::setVec2(const std::string &key, Vec2 value) {
    write(key, Value{value});
}
void Blackboard::setValue(const std::string &key, Value value) {
    write(key, std::move(value));
}
void Blackboard::add(const std::string &key, double delta) {
    write(key, Value{number(key) + delta});
}
bool Blackboard::erase(const std::string &key) {
    const auto found = values_.find(key);
    if (found == values_.end())
        return false;
    Value before = found->second;
    values_.erase(found);
    kept_.erase(key);
    ++revision_;
    std::vector<ListenerId> ids;
    for (const auto &entry : listeners_)
        ids.push_back(entry.first);
    for (const ListenerId id : ids) {
        const auto it = std::find_if(listeners_.begin(), listeners_.end(),
                                     [&](const auto &entry) { return entry.first == id; });
        if (it != listeners_.end()) {
            const Listener listener = it->second;
            listener(key, before, Value{});
        }
    }
    return true;
}
double Blackboard::number(const std::string &key, double fallback) const {
    const auto found = values_.find(key);
    if (found == values_.end() || !isNumeric(found->second))
        return fallback;
    return toNumber(found->second);
}
std::int64_t Blackboard::integer(const std::string &key, std::int64_t fallback) const {
    const auto found = values_.find(key);
    if (found == values_.end() || !isNumeric(found->second))
        return fallback;
    return toInt(found->second);
}
bool Blackboard::flag(const std::string &key, bool fallback) const {
    const auto found = values_.find(key);
    return found == values_.end() ? fallback : toBool(found->second);
}
EntityId Blackboard::entity(const std::string &key) const {
    const auto found = values_.find(key);
    return found == values_.end() ? EntityId{} : toEntity(found->second);
}
Vec2 Blackboard::vec2(const std::string &key) const {
    const auto found = values_.find(key);
    return found == values_.end() ? Vec2{} : toVec2(found->second);
}
std::string Blackboard::text(const std::string &key, std::string fallback) const {
    const auto found = values_.find(key);
    if (found == values_.end())
        return fallback;
    return toText(found->second);
}
Blackboard::Value Blackboard::get(const std::string &key) const {
    const auto found = values_.find(key);
    return found == values_.end() ? Value{} : found->second;
}
Blackboard::ListenerId Blackboard::subscribe(Listener listener) {
    listeners_.emplace_back(nextListener_, std::move(listener));
    return nextListener_++;
}
void Blackboard::unsubscribe(ListenerId id) {
    std::erase_if(listeners_, [&](const auto &entry) { return entry.first == id; });
}
std::string Blackboard::format(std::string_view templateText) const {
    std::string out;
    for (std::size_t i = 0; i < templateText.size(); ++i) {
        const char c = templateText[i];
        if ((c == '{' || c == '}') && i + 1 < templateText.size() && templateText[i + 1] == c) {
            out.push_back(c);
            ++i;
        } else if (c == '{') {
            const auto close = templateText.find('}', i + 1);
            if (close == std::string_view::npos) {
                out.push_back(c); // Unterminated: keep the text as written.
                continue;
            }
            // "{name}" or "{name:fallback}": the fallback shows while the variable is unset.
            const std::string field(templateText.substr(i + 1, close - i - 1));
            const auto colon = field.find(':');
            const std::string name = field.substr(0, colon);
            out += colon == std::string::npos ? text(name) : text(name, field.substr(colon + 1));
            i = close;
        } else {
            out.push_back(c);
        }
    }
    return out;
}
Json Blackboard::toJson() const {
    Json object = Json::object();
    for (const auto &[key, value] : values_)
        object.set(key, valueToJson(value));
    return object;
}
Status Blackboard::loadJson(const Json &json) {
    if (!json.isObject())
        return Error{"variables must be an object"};
    std::map<std::string, Value> loaded;
    for (std::size_t i = 0; i < json.size(); ++i) {
        auto value = valueFromJson(json.valueAt(i));
        if (!value)
            return Error{"variable '" + json.keyAt(i) + "': " + value.error()};
        loaded.emplace(json.keyAt(i), std::move(value.value()));
    }
    for (auto &[key, value] : loaded)
        write(key, std::move(value));
    return success();
}
} // namespace yk
