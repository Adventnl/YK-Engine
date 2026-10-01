#include "yk/scene/Property.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace yk {
namespace {
bool finiteNumber(double v) {
    return std::isfinite(v);
}
double clampRange(const PropertyInfo &p, double v) {
    return p.hasRange ? std::clamp(v, p.minValue, p.maxValue) : v;
}
} // namespace

bool PropertyInfo::assign(Component &component, PropertyValue value) const {
    if (readOnly || !set)
        return false;
    switch (type) {
    case PropertyType::Bool:
        if (!std::holds_alternative<bool>(value))
            return false;
        break;
    case PropertyType::Int: {
        if (const auto *integer = std::get_if<std::int64_t>(&value)) {
            value = static_cast<std::int64_t>(
                std::llround(clampRange(*this, static_cast<double>(*integer))));
        } else if (const auto *number = std::get_if<double>(&value)) {
            if (!finiteNumber(*number))
                return false;
            value = static_cast<std::int64_t>(std::llround(clampRange(*this, *number)));
        } else {
            return false;
        }
        break;
    }
    case PropertyType::Float: {
        double number;
        if (const auto *real = std::get_if<double>(&value))
            number = *real;
        else if (const auto *integer = std::get_if<std::int64_t>(&value))
            number = static_cast<double>(*integer);
        else
            return false;
        if (!finiteNumber(number))
            return false;
        value = clampRange(*this, number);
        break;
    }
    case PropertyType::Vec2: {
        const auto *vector = std::get_if<Vec2>(&value);
        if (!vector || !finite(*vector))
            return false;
        if (hasRange)
            value = Vec2{static_cast<float>(clampRange(*this, static_cast<double>(vector->x))),
                         static_cast<float>(clampRange(*this, static_cast<double>(vector->y)))};
        break;
    }
    case PropertyType::Enum: {
        const auto *index = std::get_if<std::int64_t>(&value);
        if (!index || *index < 0 ||
            (!options.empty() && *index >= static_cast<std::int64_t>(options.size())))
            return false;
        break;
    }
    case PropertyType::String:
        if (!std::holds_alternative<std::string>(value))
            return false;
        break;
    case PropertyType::Color:
        if (!std::holds_alternative<Color>(value))
            return false;
        break;
    case PropertyType::EntityReference:
        if (!std::holds_alternative<EntityId>(value))
            return false;
        break;
    case PropertyType::EntityReferenceList:
        if (!std::holds_alternative<std::vector<EntityId>>(value))
            return false;
        break;
    case PropertyType::StringList:
        if (!std::holds_alternative<std::vector<std::string>>(value))
            return false;
        break;
    case PropertyType::Asset:
        if (!std::holds_alternative<AssetRef>(value))
            return false;
        break;
    case PropertyType::Json:
        if (!std::holds_alternative<Json>(value))
            return false;
        break;
    }
    return set(component, value);
}

Json propertyToJson(const PropertyInfo &property, const PropertyValue &value) {
    switch (property.type) {
    case PropertyType::Bool:
        return Json(std::get<bool>(value));
    case PropertyType::Int:
        return Json(static_cast<double>(std::get<std::int64_t>(value)));
    case PropertyType::Float: {
        // Floats are stored as double in PropertyValue; keep the shortest text for float-sized
        // values.
        const double v = std::get<double>(value);
        const float narrowed = static_cast<float>(v);
        return static_cast<double>(narrowed) == v ? Json(narrowed) : Json(v);
    }
    case PropertyType::String:
        return Json(std::get<std::string>(value));
    case PropertyType::Vec2: {
        const Vec2 v = std::get<Vec2>(value);
        Json array = Json::array();
        array.push(v.x);
        array.push(v.y);
        return array;
    }
    case PropertyType::Color:
        return Json(formatColor(std::get<Color>(value)));
    case PropertyType::Enum: {
        const auto index = std::get<std::int64_t>(value);
        if (index >= 0 && index < static_cast<std::int64_t>(property.options.size()))
            return Json(property.options[static_cast<std::size_t>(index)]);
        return Json(static_cast<double>(index));
    }
    case PropertyType::EntityReference:
        return std::get<EntityId>(value) ? Json(toString(std::get<EntityId>(value))) : Json();
    case PropertyType::EntityReferenceList: {
        Json array = Json::array();
        for (const EntityId id : std::get<std::vector<EntityId>>(value))
            array.push(toString(id));
        return array;
    }
    case PropertyType::StringList: {
        Json array = Json::array();
        for (const std::string &text : std::get<std::vector<std::string>>(value))
            array.push(text);
        return array;
    }
    case PropertyType::Asset:
        return Json(std::get<AssetRef>(value).path);
    case PropertyType::Json:
        return std::get<Json>(value);
    }
    return Json();
}

Result<PropertyValue> propertyFromJson(const PropertyInfo &property, const Json &json) {
    const auto mismatch = [&](const char *expected) {
        return Error{"property '" + property.name + "' expects " + expected};
    };
    switch (property.type) {
    case PropertyType::Bool:
        if (!json.isBool())
            return mismatch("a boolean");
        return PropertyValue{json.asBool()};
    case PropertyType::Int:
        if (!json.isNumber() || !finiteNumber(json.asNumber()))
            return mismatch("a number");
        return PropertyValue{json.asInt()};
    case PropertyType::Float:
        if (!json.isNumber() || !finiteNumber(json.asNumber()))
            return mismatch("a number");
        return PropertyValue{json.asNumber()};
    case PropertyType::String:
        if (!json.isString())
            return mismatch("a string");
        return PropertyValue{json.asString()};
    case PropertyType::Vec2:
        if (!json.isArray() || json.size() != 2 || !json.at(0).isNumber() || !json.at(1).isNumber())
            return mismatch("[x, y]");
        return PropertyValue{Vec2{static_cast<float>(json.at(0).asNumber()),
                                  static_cast<float>(json.at(1).asNumber())}};
    case PropertyType::Color: {
        const auto color = json.isString() ? parseColor(json.asString()) : std::nullopt;
        if (!color)
            return mismatch("a color like \"#rrggbb\" or \"#rrggbbaa\"");
        return PropertyValue{*color};
    }
    case PropertyType::Enum: {
        if (json.isString()) {
            const auto found =
                std::find(property.options.begin(), property.options.end(), json.asString());
            if (found == property.options.end())
                return Error{"property '" + property.name + "' has no option '" + json.asString() +
                             "'"};
            return PropertyValue{static_cast<std::int64_t>(found - property.options.begin())};
        }
        if (json.isNumber())
            return PropertyValue{json.asInt()};
        return mismatch("an option name");
    }
    case PropertyType::EntityReference: {
        if (json.isNull())
            return PropertyValue{EntityId{}};
        const auto id = json.isString() ? parseEntityId(json.asString()) : std::nullopt;
        if (!id)
            return mismatch("an entity id string or null");
        return PropertyValue{*id};
    }
    case PropertyType::EntityReferenceList: {
        if (!json.isArray())
            return mismatch("an array of entity ids");
        std::vector<EntityId> ids;
        for (const Json &item : json.items()) {
            const auto id = item.isString() ? parseEntityId(item.asString()) : std::nullopt;
            if (!id)
                return mismatch("an array of entity id strings");
            ids.push_back(*id);
        }
        return PropertyValue{std::move(ids)};
    }
    case PropertyType::StringList: {
        if (!json.isArray())
            return mismatch("an array of strings");
        std::vector<std::string> values;
        for (const Json &item : json.items()) {
            if (!item.isString())
                return mismatch("an array of strings");
            values.push_back(item.asString());
        }
        return PropertyValue{std::move(values)};
    }
    case PropertyType::Asset:
        if (!json.isString())
            return mismatch("an asset path string");
        return PropertyValue{AssetRef{json.asString()}};
    case PropertyType::Json:
        return PropertyValue{json};
    }
    return mismatch("a supported value");
}

std::string prettifyName(const std::string &identifier) {
    std::string out;
    for (std::size_t i = 0; i < identifier.size(); ++i) {
        const char c = identifier[i];
        if (c == '_') {
            out.push_back(' ');
            continue;
        }
        const bool boundary = i > 0 && std::isupper(static_cast<unsigned char>(c)) &&
                              (std::islower(static_cast<unsigned char>(identifier[i - 1])) ||
                               std::isdigit(static_cast<unsigned char>(identifier[i - 1])));
        if (boundary)
            out.push_back(' ');
        out.push_back(i == 0 || out.back() == ' '
                          ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                          : c);
    }
    return out;
}
} // namespace yk
