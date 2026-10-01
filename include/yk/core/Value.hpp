#pragma once
#include "yk/core/Json.hpp"
#include "yk/core/Math.hpp"
#include "yk/scene/EntityId.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace yk {
// The one dynamically typed value of the engine: game variables (Blackboard), facts that
// conditions and UI bindings read, event payload fields, rule and action arguments, and what
// scripts exchange with the engine. It is a plain std::variant so code can use std::get_if on it;
// the helpers below give the conversions every user needs, with fixed rules:
//
//   none      false, 0, ""
//   bool      0 / 1, "true" / "false"
//   int       whole numbers (64 bit); an int and a number compare by value
//   number    double
//   string    "true"/"false"/"1"/"0" read as booleans, numeric text reads as a number
//   entity    a scene entity (EntityId); never an index or pointer
//   vec2      a point or direction
using Value = std::variant<std::monostate, bool, std::int64_t, double, std::string, EntityId, Vec2>;

enum class ValueType { None, Bool, Int, Number, String, Entity, Vec2 };
ValueType typeOf(const Value &value);
const char *typeName(ValueType type);

bool isNumeric(const Value &value); // bool, int or number
bool toBool(const Value &value);
double toNumber(const Value &value, double fallback = 0.0);
std::int64_t toInt(const Value &value, std::int64_t fallback = 0);
// Whole numbers print without a fraction ("3"), others with two decimals ("0.25"); entities as
// their hex id; vectors as "x,y".
std::string toText(const Value &value);
EntityId toEntity(const Value &value);
Vec2 toVec2(const Value &value);

// Equality with numeric coercion (1 == 1.0 == true), string equality, entity and vector equality.
// Different kinds that cannot be coerced are unequal.
bool valuesEqual(const Value &a, const Value &b);
// Orders numerics numerically and strings lexicographically. False when the two cannot be ordered
// (different kinds, entities, vectors); `result` is -1, 0 or 1 when true.
bool compareValues(const Value &a, const Value &b, int &result);

// JSON round trip: none = null, bool/number/string as themselves, integers stay integers, an
// entity is {"entity": "<hex id>"} and a vector is [x, y].
Json valueToJson(const Value &value);
// Null reads as none; objects and arrays other than the two forms above are an error.
Result<Value> valueFromJson(const Json &json);
} // namespace yk
