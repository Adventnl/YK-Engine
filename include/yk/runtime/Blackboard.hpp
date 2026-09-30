#pragma once
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <variant>

namespace yk {
// Named game variables ("gems", "level_message") shared by gameplay components and UI text. Data
// driven: a designer wires a Collectible's variable name to a UiText template in the editor with no
// code in between.
class Blackboard {
  public:
    using Value = std::variant<double, std::string>;

    void set(const std::string &key, double value);
    void set(const std::string &key, std::string value);
    void add(const std::string &key, double delta); // Treats a missing or textual value as zero.
    double number(const std::string &key, double fallback = 0.0) const;
    std::string text(const std::string &key,
                     std::string fallback = {}) const; // Numbers are formatted.
    bool has(const std::string &key) const {
        return values_.contains(key);
    }
    void clear() {
        values_.clear();
        kept_.clear();
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

    // Substitutes {name} with the variable's text (missing variables become empty) and
    // {name:fallback} with the fallback while unset. "{{" and "}}" produce literal braces.
    std::string format(std::string_view templateText) const;

  private:
    std::map<std::string, Value> values_;
    std::set<std::string> kept_;
};
} // namespace yk
