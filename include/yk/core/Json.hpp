#pragma once
#include "yk/core/Result.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace yk {
// Small, dependency-free JSON document: RFC 8259 parsing and deterministic serialization.
// Objects keep insertion order so saved data diffs cleanly. Numbers are IEEE doubles; use strings
// for 64-bit identifiers. Values constructed from float remember that they are single precision,
// so 0.1F serializes as 0.1 rather than 0.10000000149011612.
class Json {
  public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool value) : value_(value) {}
    template <class T,
              class = std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>>>
    Json(T value) : value_(static_cast<double>(value)), single_(std::is_same_v<T, float>) {}
    Json(const char *value) : value_(std::string(value)) {}
    Json(std::string value) : value_(std::move(value)) {}
    Json(std::string_view value) : value_(std::string(value)) {}

    static Json array() {
        Json json;
        json.value_ = std::vector<Json>{};
        return json;
    }
    static Json object() {
        Json json;
        json.value_ = ObjectData{};
        return json;
    }

    Type type() const;
    bool isNull() const {
        return type() == Type::Null;
    }
    bool isBool() const {
        return type() == Type::Bool;
    }
    bool isNumber() const {
        return type() == Type::Number;
    }
    bool isString() const {
        return type() == Type::String;
    }
    bool isArray() const {
        return type() == Type::Array;
    }
    bool isObject() const {
        return type() == Type::Object;
    }

    // Accessors return the fallback when the value has a different type.
    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    std::int64_t asInt(std::int64_t fallback = 0) const; // Rounds to nearest, saturates.
    const std::string &asString() const;                 // Empty string when not a string.

    // Arrays.
    std::size_t size() const; // Element/member count; zero for scalars.
    const Json &at(std::size_t index) const;
    Json &at(std::size_t index);
    Json &push(Json value); // Converts null to an array. Returns the stored element.
    const std::vector<Json> &items() const;

    // Objects (insertion ordered; linear lookup is fine at scene-file scale).
    bool contains(std::string_view key) const {
        return find(key) != nullptr;
    }
    const Json *find(std::string_view key) const;
    Json *find(std::string_view key);
    const Json &get(std::string_view key) const; // Null value when absent.
    Json &set(std::string key, Json value);      // Converts null to an object; replaces in place.
    bool erase(std::string_view key);
    const std::string &keyAt(std::size_t index) const;
    const Json &valueAt(std::size_t index) const;

    // indent < 0: compact. indent >= 0: pretty, short scalar arrays stay on one line.
    std::string dump(int indent = -1) const;
    static Result<Json> parse(std::string_view text);

    friend bool operator==(const Json &a, const Json &b);

  private:
    struct ObjectData {
        std::vector<std::string> keys;
        std::vector<Json> values;
    };
    void write(std::string &out, int indent, int depth) const;
    std::variant<std::monostate, bool, double, std::string, std::vector<Json>, ObjectData> value_;
    bool single_{};
};
} // namespace yk
