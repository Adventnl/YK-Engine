#include "yk/core/Value.hpp"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace yk {
namespace {
bool parseNumber(const std::string &text, double &out) {
    if (text.empty())
        return false;
    char *end = nullptr;
    errno = 0;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(parsed))
        return false;
    out = parsed;
    return true;
}
std::string formatNumber(double value) {
    char buffer[48];
    if (value == std::trunc(value) && std::fabs(value) < 1e15)
        std::snprintf(buffer, sizeof buffer, "%.0f", value);
    else
        std::snprintf(buffer, sizeof buffer, "%.2f", value);
    return buffer;
}
} // namespace

ValueType typeOf(const Value &value) {
    return static_cast<ValueType>(value.index());
}
const char *typeName(ValueType type) {
    switch (type) {
    case ValueType::None:
        return "none";
    case ValueType::Bool:
        return "bool";
    case ValueType::Int:
        return "int";
    case ValueType::Number:
        return "number";
    case ValueType::String:
        return "string";
    case ValueType::Entity:
        return "entity";
    case ValueType::Vec2:
        return "vec2";
    }
    return "none";
}

bool isNumeric(const Value &value) {
    return std::holds_alternative<bool>(value) || std::holds_alternative<std::int64_t>(value) ||
           std::holds_alternative<double>(value);
}
bool toBool(const Value &value) {
    if (const auto *flag = std::get_if<bool>(&value))
        return *flag;
    if (const auto *integer = std::get_if<std::int64_t>(&value))
        return *integer != 0;
    if (const auto *number = std::get_if<double>(&value))
        return *number != 0.0;
    if (const auto *text = std::get_if<std::string>(&value)) {
        if (text->empty() || *text == "false" || *text == "0")
            return false;
        double parsed = 0;
        if (parseNumber(*text, parsed))
            return parsed != 0.0;
        return true;
    }
    if (const auto *entity = std::get_if<EntityId>(&value))
        return static_cast<bool>(*entity);
    if (const auto *vector = std::get_if<Vec2>(&value))
        return vector->x != 0.0F || vector->y != 0.0F;
    return false;
}
double toNumber(const Value &value, double fallback) {
    if (const auto *flag = std::get_if<bool>(&value))
        return *flag ? 1.0 : 0.0;
    if (const auto *integer = std::get_if<std::int64_t>(&value))
        return static_cast<double>(*integer);
    if (const auto *number = std::get_if<double>(&value))
        return *number;
    if (const auto *text = std::get_if<std::string>(&value)) {
        double parsed = 0;
        if (parseNumber(*text, parsed))
            return parsed;
        if (*text == "true")
            return 1.0;
        if (*text == "false")
            return 0.0;
    }
    return fallback;
}
std::int64_t toInt(const Value &value, std::int64_t fallback) {
    if (const auto *integer = std::get_if<std::int64_t>(&value))
        return *integer;
    if (!isNumeric(value) && !std::holds_alternative<std::string>(value))
        return fallback;
    const double number = toNumber(value, std::numeric_limits<double>::quiet_NaN());
    if (!std::isfinite(number))
        return fallback;
    constexpr double limit = 9.2e18;
    return static_cast<std::int64_t>(std::llround(std::clamp(number, -limit, limit)));
}
std::string toText(const Value &value) {
    if (const auto *flag = std::get_if<bool>(&value))
        return *flag ? "true" : "false";
    if (const auto *integer = std::get_if<std::int64_t>(&value))
        return std::to_string(*integer);
    if (const auto *number = std::get_if<double>(&value))
        return formatNumber(*number);
    if (const auto *text = std::get_if<std::string>(&value))
        return *text;
    if (const auto *entity = std::get_if<EntityId>(&value))
        return toString(*entity);
    if (const auto *vector = std::get_if<Vec2>(&value))
        return formatNumber(static_cast<double>(vector->x)) + "," +
               formatNumber(static_cast<double>(vector->y));
    return {};
}
EntityId toEntity(const Value &value) {
    if (const auto *entity = std::get_if<EntityId>(&value))
        return *entity;
    if (const auto *text = std::get_if<std::string>(&value))
        if (const auto parsed = parseEntityId(*text))
            return *parsed;
    return {};
}
Vec2 toVec2(const Value &value) {
    if (const auto *vector = std::get_if<Vec2>(&value))
        return *vector;
    return {};
}

bool valuesEqual(const Value &a, const Value &b) {
    if (std::holds_alternative<std::monostate>(a) || std::holds_alternative<std::monostate>(b))
        return std::holds_alternative<std::monostate>(a) &&
               std::holds_alternative<std::monostate>(b);
    if (isNumeric(a) && isNumeric(b))
        return toNumber(a) == toNumber(b);
    if (const auto *left = std::get_if<std::string>(&a)) {
        if (const auto *right = std::get_if<std::string>(&b))
            return *left == *right;
        if (isNumeric(b)) { // "3" == 3, "true" == true
            double parsed = 0;
            if (parseNumber(*left, parsed))
                return parsed == toNumber(b);
            if (std::holds_alternative<bool>(b))
                return (*left == "true") == std::get<bool>(b) &&
                       (*left == "true" || *left == "false");
        }
        return false;
    }
    if (std::holds_alternative<std::string>(b) && isNumeric(a))
        return valuesEqual(b, a);
    if (const auto *left = std::get_if<EntityId>(&a))
        if (const auto *right = std::get_if<EntityId>(&b))
            return *left == *right;
    if (const auto *left = std::get_if<Vec2>(&a))
        if (const auto *right = std::get_if<Vec2>(&b))
            return *left == *right;
    return false;
}
bool compareValues(const Value &a, const Value &b, int &result) {
    const auto order = [&](auto left, auto right) {
        result = left < right ? -1 : (left > right ? 1 : 0);
        return true;
    };
    if (isNumeric(a) && isNumeric(b))
        return order(toNumber(a), toNumber(b));
    if (const auto *left = std::get_if<std::string>(&a)) {
        if (const auto *right = std::get_if<std::string>(&b))
            return order(left->compare(*right), 0);
        double parsed = 0;
        if (isNumeric(b) && parseNumber(*left, parsed))
            return order(parsed, toNumber(b));
        return false;
    }
    if (std::holds_alternative<std::string>(b) && isNumeric(a)) {
        int reverse = 0;
        if (!compareValues(b, a, reverse))
            return false;
        result = -reverse;
        return true;
    }
    return false;
}

Json valueToJson(const Value &value) {
    if (const auto *flag = std::get_if<bool>(&value))
        return Json(*flag);
    if (const auto *integer = std::get_if<std::int64_t>(&value))
        return Json(*integer);
    if (const auto *number = std::get_if<double>(&value))
        return Json(*number);
    if (const auto *text = std::get_if<std::string>(&value))
        return Json(*text);
    if (const auto *entity = std::get_if<EntityId>(&value)) {
        Json object = Json::object();
        object.set("entity", toString(*entity));
        return object;
    }
    if (const auto *vector = std::get_if<Vec2>(&value)) {
        Json array = Json::array();
        array.push(Json(vector->x));
        array.push(Json(vector->y));
        return array;
    }
    return Json();
}
Result<Value> valueFromJson(const Json &json) {
    switch (json.type()) {
    case Json::Type::Null:
        return Value{};
    case Json::Type::Bool:
        return Value{json.asBool()};
    case Json::Type::Number: {
        const double number = json.asNumber();
        if (number == std::trunc(number) && std::fabs(number) < 9.0e15)
            return Value{static_cast<std::int64_t>(number)};
        return Value{number};
    }
    case Json::Type::String:
        return Value{json.asString()};
    case Json::Type::Array:
        if (json.size() == 2 && json.at(0).isNumber() && json.at(1).isNumber())
            return Value{Vec2{static_cast<float>(json.at(0).asNumber()),
                              static_cast<float>(json.at(1).asNumber())}};
        return Error{"an array value must be a [x, y] pair"};
    case Json::Type::Object:
        if (json.contains("entity")) {
            const auto id = parseEntityId(json.get("entity").asString());
            if (!id)
                return Error{"'" + json.get("entity").asString() + "' is not an entity id"};
            return Value{*id};
        }
        return Error{"an object value must be {\"entity\": \"<id>\"}"};
    }
    return Value{};
}
} // namespace yk
