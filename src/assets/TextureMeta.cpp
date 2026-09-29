#include "yk/assets/TextureMeta.hpp"
#include <cmath>

namespace yk {
namespace {
const char *filterName(TextureFilter filter) {
    return filter == TextureFilter::Nearest ? "nearest" : "linear";
}
Result<TextureFilter> parseFilter(const Json &json) {
    if (json.isString() && json.asString() == "nearest")
        return TextureFilter::Nearest;
    if (json.isString() && json.asString() == "linear")
        return TextureFilter::Linear;
    return Error{"'filter' must be \"nearest\" or \"linear\""};
}
Result<float> parsePixelsPerUnit(const Json &json) {
    if (!json.isNumber() || !std::isfinite(json.asNumber()) || json.asNumber() < 1.0 ||
        json.asNumber() > 4096.0)
        return Error{"'pixelsPerUnit' must be a number from 1 to 4096"};
    return static_cast<float>(json.asNumber());
}
Result<int> parseGrid(const Json &json, const char *name) {
    if (!json.isNumber() || json.asNumber() < 1.0 || json.asNumber() > 256.0 ||
        json.asNumber() != std::floor(json.asNumber()))
        return Error{std::string("'") + name + "' must be a whole number from 1 to 256"};
    return static_cast<int>(json.asInt());
}
} // namespace

Json TextureMeta::toJson() const {
    Json json = Json::object();
    json.set("format", formatName);
    json.set("version", 1);
    if (pixelsPerUnit)
        json.set("pixelsPerUnit", *pixelsPerUnit);
    if (filter)
        json.set("filter", filterName(*filter));
    if (columns)
        json.set("columns", *columns);
    if (rows)
        json.set("rows", *rows);
    if (border) {
        Json array = Json::array();
        for (const int value : *border)
            array.push(value);
        json.set("border", array);
    }
    return json;
}

Result<TextureMeta> TextureMeta::fromJson(const Json &json) {
    if (!json.isObject() || json.get("format").asString() != formatName)
        return Error{"Not a yk.texture document"};
    TextureMeta meta;
    if (const Json *value = json.find("pixelsPerUnit")) {
        auto parsed = parsePixelsPerUnit(*value);
        if (!parsed)
            return Error{parsed.error()};
        meta.pixelsPerUnit = parsed.value();
    }
    if (const Json *value = json.find("filter")) {
        auto parsed = parseFilter(*value);
        if (!parsed)
            return Error{parsed.error()};
        meta.filter = parsed.value();
    }
    if (const Json *value = json.find("columns")) {
        auto parsed = parseGrid(*value, "columns");
        if (!parsed)
            return Error{parsed.error()};
        meta.columns = parsed.value();
    }
    if (const Json *value = json.find("rows")) {
        auto parsed = parseGrid(*value, "rows");
        if (!parsed)
            return Error{parsed.error()};
        meta.rows = parsed.value();
    }
    if (const Json *value = json.find("border")) {
        if (!value->isArray() || value->size() != 4)
            return Error{"'border' must be [left, top, right, bottom] in pixels"};
        std::array<int, 4> border{};
        for (std::size_t i = 0; i < 4; ++i) {
            const Json &edge = value->at(i);
            if (!edge.isNumber() || edge.asNumber() < 0.0 || edge.asNumber() > 8192.0 ||
                edge.asNumber() != std::floor(edge.asNumber()))
                return Error{"'border' entries must be whole numbers from 0 to 8192"};
            border[i] = static_cast<int>(edge.asInt());
        }
        meta.border = border;
    }
    return meta;
}

Json TextureDefaults::toJson() const {
    Json json = Json::object();
    json.set("pixelsPerUnit", pixelsPerUnit);
    json.set("filter", filterName(filter));
    return json;
}

Result<TextureDefaults> TextureDefaults::fromJson(const Json &json) {
    if (!json.isObject())
        return Error{"'textures' must be an object"};
    TextureDefaults defaults;
    if (const Json *value = json.find("pixelsPerUnit")) {
        auto parsed = parsePixelsPerUnit(*value);
        if (!parsed)
            return Error{"textures: " + parsed.error()};
        defaults.pixelsPerUnit = parsed.value();
    }
    if (const Json *value = json.find("filter")) {
        auto parsed = parseFilter(*value);
        if (!parsed)
            return Error{"textures: " + parsed.error()};
        defaults.filter = parsed.value();
    }
    return defaults;
}

ResolvedTexture resolve(const TextureDefaults &defaults, const TextureMeta &meta) {
    ResolvedTexture out;
    out.pixelsPerUnit = meta.pixelsPerUnit.value_or(defaults.pixelsPerUnit);
    out.filter = meta.filter.value_or(defaults.filter);
    out.columns = meta.columns.value_or(1);
    out.rows = meta.rows.value_or(1);
    if (meta.border)
        out.border = *meta.border;
    return out;
}
} // namespace yk
