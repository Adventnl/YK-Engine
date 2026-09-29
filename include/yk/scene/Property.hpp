#pragma once
#include "yk/core/Color.hpp"
#include "yk/core/Json.hpp"
#include "yk/core/Math.hpp"
#include "yk/core/Result.hpp"
#include "yk/scene/EntityId.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace yk {
class Component;

// Project-relative asset path ("assets/sounds/door.wav"), or a built-in such as "builtin:circle" or
// "tone:440,0.1". Empty means none. Assets are resolved lazily by the runtime, never by the scene.
struct AssetRef {
    std::string path;
    friend bool operator==(const AssetRef &, const AssetRef &) = default;
};

enum class PropertyType {
    Bool,
    Int,
    Float,
    String,
    Vec2,
    Color,
    Enum,
    EntityReference,
    EntityReferenceList,
    StringList,
    Asset
};

// Enum values travel as their integer index into PropertyInfo::options.
using PropertyValue = std::variant<bool, std::int64_t, double, std::string, Vec2, Color, EntityId,
                                   std::vector<EntityId>, std::vector<std::string>, AssetRef>;

// Reflection record for one editable, serializable component field.
struct PropertyInfo {
    std::string name; // Serialized key; the editor prettifies it for display.
    PropertyType type{PropertyType::Bool};
    std::string tooltip;
    bool hasRange{};
    double minValue{}, maxValue{}, step{};
    std::vector<std::string> options; // Enum labels (index == value).
    std::string assetKind;            // "texture", "sound", "prefab", "animation", "scene".
    bool hidden{};                    // Serialized but not shown.
    bool readOnly{};                  // Shown but not serialized or edited (runtime state).
    bool isSize{};                    // Extents the resize gizmo scales proportionally.
    bool isOffset{};                  // Local offsets the resize gizmo scales with sizes.
    bool isLayer{};                   // Integer index into the project's collision layers.
    bool multiline{};
    PropertyValue defaultValue;
    std::function<PropertyValue(const Component &)> get;
    std::function<bool(Component &, const PropertyValue &)> set;

    // Validates the value against type, range and enum bounds, clamps numerics, then assigns.
    // Returns false and leaves the component untouched when the value is unacceptable.
    bool assign(Component &component, PropertyValue value) const;
    PropertyValue read(const Component &component) const {
        return get(component);
    }
};

// JSON encoding: bool/number/string as themselves, Vec2 as [x, y], Color as "#rrggbbaa", enum as
// its option name (falls back to the integer), references as hex id strings, asset as a path.
Json propertyToJson(const PropertyInfo &property, const PropertyValue &value);
Result<PropertyValue> propertyFromJson(const PropertyInfo &property, const Json &json);
// "moveSpeed" -> "Move Speed", for labels.
std::string prettifyName(const std::string &identifier);
} // namespace yk
