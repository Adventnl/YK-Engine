#include "yk/world/Tileset.hpp"
#include <cmath>

namespace yk {
namespace {
const TileProperties defaults;

Json rectJson(const Rect &rect) {
    Json array = Json::array();
    array.push(rect.position.x);
    array.push(rect.position.y);
    array.push(rect.size.x);
    array.push(rect.size.y);
    return array;
}
} // namespace

const TileProperties &Tileset::properties(int index) const {
    const auto found = tiles.find(index);
    return found == tiles.end() ? defaults : found->second;
}
Rect Tileset::sourceRect(int index) const {
    const int count = tileCount();
    if (index < 0 || count <= 0)
        return {};
    const int clamped = index % count;
    const int column = clamped % columns, row = clamped / columns;
    return {{static_cast<float>(margin + column * (tileWidth + spacing)),
             static_cast<float>(margin + row * (tileHeight + spacing))},
            {static_cast<float>(tileWidth), static_cast<float>(tileHeight)}};
}
int Tileset::frameAt(int index, double seconds) const {
    const TileProperties &props = properties(index);
    if (props.animation.frames.empty() || !(props.animation.fps > 0.0F))
        return index;
    const auto step = static_cast<std::size_t>(std::floor(seconds * static_cast<double>(props.animation.fps)));
    return props.animation.frames[step % props.animation.frames.size()];
}

Status Tileset::validate() const {
    if (tileWidth < 1 || tileHeight < 1 || tileWidth > 4096 || tileHeight > 4096)
        return Error{"tile size must be between 1 and 4096 pixels"};
    if (columns < 1 || rows < 1 || columns > 4096 || rows > 4096)
        return Error{"the sheet needs at least one column and one row"};
    if (spacing < 0 || margin < 0)
        return Error{"spacing and margin cannot be negative"};
    for (const auto &[index, props] : tiles) {
        const std::string where = "tile " + std::to_string(index);
        if (index < 0 || index >= tileCount())
            return Error{where + " is outside the sheet (" + std::to_string(tileCount()) +
                         " tiles)"};
        if (!(props.cost > 0.0F) || !std::isfinite(props.cost))
            return Error{where + ": cost must be greater than zero"};
        if (!(props.noiseDamping >= 0.0F && props.noiseDamping <= 1.0F))
            return Error{where + ": noiseDamping must be between 0 and 1"};
        for (const int frame : props.animation.frames)
            if (frame < 0 || frame >= tileCount())
                return Error{where + ": animation frame " + std::to_string(frame) +
                             " is outside the sheet"};
        if (!props.animation.frames.empty() && !(props.animation.fps > 0.0F))
            return Error{where + ": animation fps must be greater than zero"};
        if (props.collider) {
            const Rect &box = *props.collider;
            if (!(box.size.x > 0.0F && box.size.y > 0.0F) || box.position.x < 0.0F ||
                box.position.y < 0.0F || box.position.x + box.size.x > 1.0001F ||
                box.position.y + box.size.y > 1.0001F)
                return Error{where + ": collider must be a box inside the tile, as fractions 0..1"};
        }
        if (props.modify.result >= tileCount())
            return Error{where + ": modify.result " + std::to_string(props.modify.result) +
                         " is outside the sheet"};
        if (props.modify.action.empty() && props.modify.resistance != 0.0F)
            return Error{where + ": modify.resistance needs a modify.action"};
    }
    return success();
}

Json Tileset::toJson() const {
    Json document = Json::object();
    document.set("format", tilesetFormatName);
    document.set("version", tilesetFormatVersion);
    document.set("name", name);
    document.set("texture", texture);
    document.set("tileWidth", tileWidth);
    document.set("tileHeight", tileHeight);
    document.set("columns", columns);
    document.set("rows", rows);
    if (spacing != 0)
        document.set("spacing", spacing);
    if (margin != 0)
        document.set("margin", margin);
    Json list = Json::array();
    for (const auto &[index, props] : tiles) {
        Json entry = Json::object();
        entry.set("id", index);
        if (props.solid)
            entry.set("solid", true);
        if (props.opaque)
            entry.set("opaque", true);
        if (!props.area.empty())
            entry.set("area", props.area);
        if (props.cost != 1.0F)
            entry.set("cost", props.cost);
        if (props.noiseDamping != 0.0F)
            entry.set("noiseDamping", props.noiseDamping);
        if (!props.tags.empty()) {
            Json tags = Json::array();
            for (const std::string &tag : props.tags)
                tags.push(tag);
            entry.set("tags", tags);
        }
        if (props.collider)
            entry.set("collider", rectJson(*props.collider));
        if (!props.animation.frames.empty()) {
            Json animation = Json::object();
            Json frames = Json::array();
            for (const int frame : props.animation.frames)
                frames.push(frame);
            animation.set("frames", frames);
            animation.set("fps", props.animation.fps);
            entry.set("animation", animation);
        }
        if (!props.modify.action.empty()) {
            Json modify = Json::object();
            modify.set("action", props.modify.action);
            modify.set("resistance", props.modify.resistance);
            modify.set("result", props.modify.result);
            if (!props.modify.material.empty())
                modify.set("material", props.modify.material);
            entry.set("modify", modify);
        }
        list.push(entry);
    }
    document.set("tiles", list);
    return document;
}

Result<Tileset> Tileset::fromJson(const Json &json) {
    if (!json.isObject())
        return Error{"a tileset must be a JSON object"};
    if (json.get("format").asString() != tilesetFormatName)
        return Error{std::string("not a ") + tilesetFormatName + " document"};
    if (!json.get("version").isNumber() || json.get("version").asInt() < 1)
        return Error{"missing or invalid tileset version"};
    if (json.get("version").asInt() > tilesetFormatVersion)
        return Error{"tileset version " + std::to_string(json.get("version").asInt()) +
                     " is newer than this build supports (" +
                     std::to_string(tilesetFormatVersion) + ")"};
    Tileset set;
    set.name = json.get("name").asString();
    set.texture = json.get("texture").asString();
    const auto integer = [&](const char *key, int fallback, int &out) -> Status {
        const Json *value = json.find(key);
        if (!value) {
            out = fallback;
            return success();
        }
        if (!value->isNumber() || !std::isfinite(value->asNumber()))
            return Error{std::string("'") + key + "' must be a number"};
        out = static_cast<int>(value->asInt());
        return success();
    };
    for (const auto &[key, fallback, out] :
         {std::tuple<const char *, int, int *>{"tileWidth", 16, &set.tileWidth},
          {"tileHeight", 16, &set.tileHeight},
          {"columns", 1, &set.columns},
          {"rows", 1, &set.rows},
          {"spacing", 0, &set.spacing},
          {"margin", 0, &set.margin}})
        if (auto status = integer(key, fallback, *out); !status)
            return Error{status.error()};
    const Json &list = json.get("tiles");
    if (json.contains("tiles") && !list.isArray())
        return Error{"'tiles' must be an array"};
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Json &entry = list.at(i);
        const std::string where = "tiles[" + std::to_string(i) + "]";
        if (!entry.isObject() || !entry.get("id").isNumber())
            return Error{where + " needs a numeric id"};
        const int index = static_cast<int>(entry.get("id").asInt());
        if (set.tiles.contains(index))
            return Error{where + ": tile " + std::to_string(index) + " is described twice"};
        TileProperties props;
        props.solid = entry.get("solid").asBool(false);
        props.opaque = entry.get("opaque").asBool(false);
        props.area = entry.get("area").asString();
        props.cost = static_cast<float>(entry.get("cost").asNumber(1.0));
        props.noiseDamping = static_cast<float>(entry.get("noiseDamping").asNumber(0.0));
        if (const Json *tags = entry.find("tags")) {
            if (!tags->isArray())
                return Error{where + ": tags must be an array of strings"};
            for (const Json &tag : tags->items()) {
                if (!tag.isString())
                    return Error{where + ": tags must be an array of strings"};
                props.tags.push_back(tag.asString());
            }
        }
        if (const Json *box = entry.find("collider")) {
            if (!box->isArray() || box->size() != 4)
                return Error{where + ": collider must be [x, y, width, height]"};
            props.collider = Rect{{static_cast<float>(box->at(0).asNumber()),
                                   static_cast<float>(box->at(1).asNumber())},
                                  {static_cast<float>(box->at(2).asNumber()),
                                   static_cast<float>(box->at(3).asNumber())}};
        }
        if (const Json *animation = entry.find("animation")) {
            if (!animation->isObject() || !animation->get("frames").isArray())
                return Error{where + ": animation needs a frames array"};
            for (const Json &frame : animation->get("frames").items())
                props.animation.frames.push_back(static_cast<int>(frame.asInt()));
            props.animation.fps = static_cast<float>(animation->get("fps").asNumber(4.0));
        }
        if (const Json *modify = entry.find("modify")) {
            if (!modify->isObject())
                return Error{where + ": modify must be an object"};
            props.modify.action = modify->get("action").asString();
            props.modify.resistance = static_cast<float>(modify->get("resistance").asNumber(0.0));
            props.modify.result = static_cast<int>(modify->get("result").asInt(-1));
            props.modify.material = modify->get("material").asString();
        }
        set.tiles.emplace(index, std::move(props));
    }
    if (auto status = set.validate(); !status)
        return Error{status.error()};
    return set;
}
} // namespace yk
