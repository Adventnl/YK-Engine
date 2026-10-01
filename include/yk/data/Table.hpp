#pragma once
#include "yk/core/Json.hpp"
#include "yk/core/Result.hpp"
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace yk {
// One thing wrong in a definition file, found while loading it. Loading never stops at the first:
// what is fine still loads (a game with one broken recipe still starts), and the validator turns
// every problem into an error the designer can fix.
struct DataProblem {
    std::string file;
    std::string message;
    bool error{true};
};

// The definitions of one kind (items, quests, effects...) by id, in the order they were loaded.
// `Def` has a string `id` (and optionally a string `file`, set to where it was read from) and
// `static Result<Def> fromJson(const Json &, std::vector<std::string> &warnings)`.
template <class Def> class DefinitionTable {
  public:
    // False, with the reason in `error`, when the id is empty or taken.
    bool add(Def def, std::string &error) {
        if (def.id.empty()) {
            error = "a definition has no 'id'";
            return false;
        }
        if (index_.contains(def.id)) {
            error = "there are two definitions called '" + def.id + "'";
            return false;
        }
        index_.emplace(def.id, items_.size());
        items_.push_back(std::move(def));
        return true;
    }
    const Def *find(std::string_view id) const {
        const auto found = index_.find(id);
        return found == index_.end() ? nullptr : &items_[found->second];
    }
    bool contains(std::string_view id) const {
        return find(id) != nullptr;
    }
    const std::vector<Def> &all() const {
        return items_;
    }
    std::size_t size() const {
        return items_.size();
    }
    bool empty() const {
        return items_.empty();
    }
    // Reads a JSON list; each entry that is wrong is reported (with its place and id) and skipped.
    // Doubtful things (a field nobody reads, probably a typo) are reported as warnings.
    void load(const Json &list, const std::string &file, std::vector<DataProblem> &problems,
              const std::string &what) {
        if (!list.isArray()) {
            problems.push_back({file, "'" + what + "' must be a list", true});
            return;
        }
        for (std::size_t i = 0; i < list.size(); ++i) {
            const std::string place =
                what + " " + std::to_string(i + 1) +
                (list.at(i).get("id").isString() ? " ('" + list.at(i).get("id").asString() + "')"
                                                 : "");
            std::vector<std::string> warnings;
            auto parsed = Def::fromJson(list.at(i), warnings);
            for (const std::string &warning : warnings)
                problems.push_back({file, place + ": " + warning, false});
            if (!parsed) {
                problems.push_back({file, place + ": " + parsed.error(), true});
                continue;
            }
            if constexpr (requires { parsed.value().file; })
                parsed.value().file = file;
            std::string error;
            if (!add(std::move(parsed.value()), error))
                problems.push_back({file, place + ": " + error, true});
        }
    }
    void clear() {
        items_.clear();
        index_.clear();
    }

  private:
    std::vector<Def> items_;
    std::map<std::string, std::size_t, std::less<>> index_;
};

// Small readers shared by the definition parsers: they take what is there and say what is wrong
// in the author's words.
namespace data {
// A string that must be there.
inline Result<std::string> requiredString(const Json &json, const char *key) {
    if (!json.get(key).isString() || json.get(key).asString().empty())
        return Error{std::string("'") + key + "' is needed (text)"};
    return json.get(key).asString();
}
inline std::string optionalString(const Json &json, const char *key, std::string fallback = {}) {
    return json.get(key).isString() ? json.get(key).asString() : fallback;
}
inline std::vector<std::string> stringList(const Json &json, const char *key) {
    std::vector<std::string> list;
    const Json &items = json.get(key);
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items.at(i).isString())
            list.push_back(items.at(i).asString());
    return list;
}
inline Json toJsonList(const std::vector<std::string> &list) {
    Json json = Json::array();
    for (const std::string &item : list)
        json.push(item);
    return json;
}
// True when `list` holds `item`.
inline bool has(const std::vector<std::string> &list, std::string_view item) {
    for (const std::string &entry : list)
        if (entry == item)
            return true;
    return false;
}
// Adds a warning for every key of an object that is not in `known` ("durabilty": a typo that
// would otherwise silently do nothing).
inline void warnUnknown(const Json &json, std::initializer_list<const char *> known,
                        std::vector<std::string> &warnings) {
    if (!json.isObject())
        return;
    for (std::size_t i = 0; i < json.size(); ++i) {
        bool found = false;
        for (const char *key : known)
            found = found || json.keyAt(i) == key;
        if (!found)
            warnings.push_back("unknown field '" + json.keyAt(i) + "' (it is ignored)");
    }
}
// A number that must be there, within a range.
inline Result<double> number(const Json &json, const char *key, double fallback, double low,
                             double high) {
    if (!json.contains(key))
        return fallback;
    if (!json.get(key).isNumber() || !(json.get(key).asNumber() >= low) ||
        !(json.get(key).asNumber() <= high))
        return Error{std::string("'") + key + "' must be a number from " + std::to_string(low) +
                     " to " + std::to_string(high)};
    return json.get(key).asNumber();
}
// Ids are what other definitions and the rules name things by: letters, digits, '_' and '-'.
inline bool validId(std::string_view id) {
    if (id.empty())
        return false;
    for (const char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return false;
    return true;
}
} // namespace data
} // namespace yk
