#pragma once
#include "yk/core/Value.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace yk {
// Named game variables ("gems", "level_message", "quest.escape.state") shared by gameplay
// components, rules, scripts and UI text. Data driven: a designer wires a Collectible's variable
// name to a UiText template in the editor with no code in between.
//
// Values are typed (see yk/core/Value.hpp): numbers, booleans, integers, text, entity references
// and points. The original number/text accessors keep their meaning; numeric accessors read
// booleans and integers too, and text() formats any value.
class Blackboard {
  public:
    using Value = yk::Value;
    // Called after a write that changed a variable (same value again is not a change).
    using Listener =
        std::function<void(const std::string &key, const Value &before, const Value &after)>;
    using ListenerId = std::uint64_t;

    void set(const std::string &key, double value);
    void set(const std::string &key, std::string value);
    void setBool(const std::string &key, bool value);
    void setInt(const std::string &key, std::int64_t value);
    void setEntity(const std::string &key, EntityId value);
    void setVec2(const std::string &key, Vec2 value);
    void setValue(const std::string &key, Value value);
    void add(const std::string &key, double delta); // Treats a missing or textual value as zero.
    bool erase(const std::string &key);

    // Numeric view of numbers, integers and booleans; fallback for anything else (and for text).
    double number(const std::string &key, double fallback = 0.0) const;
    std::int64_t integer(const std::string &key, std::int64_t fallback = 0) const;
    bool flag(const std::string &key, bool fallback = false) const; // Any value coerces (toBool).
    EntityId entity(const std::string &key) const;
    Vec2 vec2(const std::string &key) const;
    std::string text(const std::string &key,
                     std::string fallback = {}) const; // Any value is formatted.
    // The stored value, or none when the variable is not set.
    Value get(const std::string &key) const;
    bool has(const std::string &key) const {
        return values_.contains(key);
    }
    void clear() {
        values_.clear();
        kept_.clear();
        ++revision_;
    }
    // Marks a variable to be carried into the next scene (a score, the lives left, what was
    // collected): the host hands kept() to the runtime it starts for the next scene.
    void keep(const std::string &key) {
        kept_.insert(key);
    }
    // The kept variables and their values now (variables that were never set are not included).
    std::map<std::string, Value> kept() const {
        std::map<std::string, Value> result;
        for (const std::string &key : kept_)
            if (const auto found = values_.find(key); found != values_.end())
                result.emplace(key, found->second);
        return result;
    }
    const std::map<std::string, Value> &values() const {
        return values_;
    }
    // Incremented by every change; UI that mirrors variables redraws when it moves.
    std::uint64_t revision() const {
        return revision_;
    }

    ListenerId subscribe(Listener listener);
    void unsubscribe(ListenerId id);

    // Substitutes {name} with the variable's text (missing variables become empty) and
    // {name:fallback} with the fallback while unset. "{{" and "}}" produce literal braces.
    std::string format(std::string_view templateText) const;

    // JSON form of every variable (typed), and loading it back; used by save games.
    Json toJson() const;
    Status loadJson(const Json &json);

  private:
    void write(const std::string &key, Value value);
    std::map<std::string, Value> values_;
    std::set<std::string> kept_;
    std::uint64_t revision_{};
    std::vector<std::pair<ListenerId, Listener>> listeners_;
    ListenerId nextListener_{1};
};
} // namespace yk
