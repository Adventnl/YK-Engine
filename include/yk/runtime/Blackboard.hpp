#pragma once
#include <map>
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
    }
    const std::map<std::string, Value> &values() const {
        return values_;
    }

    // Substitutes {name} with the variable's text (missing variables become empty). "{{" and "}}"
    // produce literal braces.
    std::string format(std::string_view templateText) const;

  private:
    std::map<std::string, Value> values_;
};
} // namespace yk
