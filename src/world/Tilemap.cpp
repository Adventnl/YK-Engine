#include "yk/world/Tilemap.hpp"
#include "yk/scene/Entity.hpp"
#include "yk/scene/Scene.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
namespace {
constexpr std::size_t logLimit = 4096;

int floorDivide(int value, int divisor) {
    int quotient = value / divisor;
    if ((value % divisor != 0) && ((value < 0) != (divisor < 0)))
        --quotient;
    return quotient;
}
int floorModulo(int value, int divisor) {
    return value - floorDivide(value, divisor) * divisor;
}
} // namespace

ChunkKey chunkOf(int cellX, int cellY) {
    return {floorDivide(cellX, tileChunkSize), floorDivide(cellY, tileChunkSize)};
}

bool TileChunk::set(int localX, int localY, std::int32_t value) {
    std::int32_t &cell = cells_[static_cast<std::size_t>(localY * tileChunkSize + localX)];
    if (cell == value)
        return false;
    if (tile::empty(cell) && !tile::empty(value))
        ++filled_;
    else if (!tile::empty(cell) && tile::empty(value))
        --filled_;
    cell = value;
    return true;
}

const std::vector<std::string> &tileLayerKindNames() {
    static const std::vector<std::string> names{"Floor", "Wall",        "Terrain",   "Roof",
                                                "Vent",  "Underground", "Decoration", "Collision"};
    return names;
}

TileLayer TileLayer::make(std::string name, TileLayerKind kind) {
    TileLayer layer;
    layer.name = std::move(name);
    layer.kind = kind;
    switch (kind) {
    case TileLayerKind::Floor:
    case TileLayerKind::Terrain:
    case TileLayerKind::Underground:
        break;
    case TileLayerKind::Wall:
        layer.solid = true;
        layer.ySort = true;
        break;
    case TileLayerKind::Roof:
        layer.sortLayer = 20;
        break;
    case TileLayerKind::Vent:
        layer.solid = true;
        break;
    case TileLayerKind::Decoration:
        layer.sortLayer = 1;
        break;
    case TileLayerKind::Collision:
        layer.solid = true;
        layer.visible = false;
        break;
    }
    return layer;
}

std::int32_t TileLayer::cell(int x, int y) const {
    const auto found = chunks_.find(chunkOf(x, y));
    if (found == chunks_.end())
        return 0;
    return found->second.get(floorModulo(x, tileChunkSize), floorModulo(y, tileChunkSize));
}
bool TileLayer::setCell(int x, int y, std::int32_t value) {
    const ChunkKey key = chunkOf(x, y);
    auto found = chunks_.find(key);
    if (found == chunks_.end()) {
        if (tile::empty(value))
            return false;
        found = chunks_.emplace(key, TileChunk{}).first;
    }
    const bool changed =
        found->second.set(floorModulo(x, tileChunkSize), floorModulo(y, tileChunkSize), value);
    if (found->second.filled() == 0)
        chunks_.erase(found);
    return changed;
}
bool TileLayer::bounds(int &minX, int &minY, int &maxX, int &maxY) const {
    bool any = false;
    for (const auto &[key, chunk] : chunks_)
        for (int y = 0; y < tileChunkSize; ++y)
            for (int x = 0; x < tileChunkSize; ++x) {
                if (tile::empty(chunk.get(x, y)))
                    continue;
                const int cellX = chunkOrigin(key.x) + x, cellY = chunkOrigin(key.y) + y;
                if (!any) {
                    minX = maxX = cellX;
                    minY = maxY = cellY;
                    any = true;
                } else {
                    minX = std::min(minX, cellX);
                    maxX = std::max(maxX, cellX);
                    minY = std::min(minY, cellY);
                    maxY = std::max(maxY, cellY);
                }
            }
    return any;
}
std::size_t TileLayer::tileCount() const {
    std::size_t count = 0;
    for (const auto &[key, chunk] : chunks_) {
        (void)key;
        count += static_cast<std::size_t>(chunk.filled());
    }
    return count;
}
bool operator==(const TileLayer &a, const TileLayer &b) {
    return a.name == b.name && a.kind == b.kind && a.level == b.level && a.visible == b.visible &&
           a.locked == b.locked && a.solid == b.solid && a.ySort == b.ySort &&
           a.sortLayer == b.sortLayer && a.order == b.order && a.opacity == b.opacity &&
           a.chunks_ == b.chunks_;
}

Json TileLayer::toJson() const {
    Json layer = Json::object();
    layer.set("name", name);
    layer.set("kind", tileLayerKindNames()[static_cast<std::size_t>(kind)]);
    layer.set("level", level);
    layer.set("visible", visible);
    layer.set("locked", locked);
    layer.set("solid", solid);
    layer.set("ySort", ySort);
    layer.set("sortLayer", sortLayer);
    layer.set("order", order);
    layer.set("opacity", opacity);
    Json list = Json::array();
    for (const auto &[key, chunk] : chunks_) {
        Json entry = Json::object();
        entry.set("x", key.x);
        entry.set("y", key.y);
        // Runs of equal cells as value, count, value, count...: a floor chunk is two numbers.
        Json runs = Json::array();
        const auto &cells = chunk.cells();
        std::size_t at = 0;
        while (at < cells.size()) {
            std::size_t end = at + 1;
            while (end < cells.size() && cells[end] == cells[at])
                ++end;
            runs.push(Json(cells[at]));
            runs.push(Json(static_cast<std::int64_t>(end - at)));
            at = end;
        }
        entry.set("runs", runs);
        list.push(entry);
    }
    layer.set("chunks", list);
    return layer;
}

Result<TileLayer> TileLayer::fromJson(const Json &json) {
    if (!json.isObject())
        return Error{"a tile layer must be an object"};
    TileLayer layer;
    layer.name = json.get("name").asString();
    if (layer.name.empty())
        return Error{"a tile layer needs a name"};
    const std::string where = "layer '" + layer.name + "'";
    const std::string kind = json.contains("kind") ? json.get("kind").asString() : "Floor";
    const auto found = std::find(tileLayerKindNames().begin(), tileLayerKindNames().end(), kind);
    if (found == tileLayerKindNames().end())
        return Error{where + ": unknown kind '" + kind + "'"};
    layer.kind = static_cast<TileLayerKind>(found - tileLayerKindNames().begin());
    layer.level = json.get("level").asString();
    layer.visible = json.get("visible").asBool(true);
    layer.locked = json.get("locked").asBool(false);
    layer.solid = json.get("solid").asBool(false);
    layer.ySort = json.get("ySort").asBool(false);
    layer.sortLayer = static_cast<int>(json.get("sortLayer").asInt(0));
    layer.order = static_cast<float>(json.get("order").asNumber(0.0));
    layer.opacity = static_cast<float>(json.get("opacity").asNumber(1.0));
    if (!(layer.opacity >= 0.0F && layer.opacity <= 1.0F))
        return Error{where + ": opacity must be between 0 and 1"};
    const Json &chunks = json.get("chunks");
    if (json.contains("chunks") && !chunks.isArray())
        return Error{where + ": chunks must be an array"};
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const Json &entry = chunks.at(i);
        const std::string at = where + " chunk " + std::to_string(i);
        if (!entry.isObject() || !entry.get("x").isNumber() || !entry.get("y").isNumber())
            return Error{at + " needs x and y"};
        const ChunkKey key{static_cast<int>(entry.get("x").asInt()),
                           static_cast<int>(entry.get("y").asInt())};
        if (layer.chunks_.contains(key))
            return Error{at + ": chunk (" + std::to_string(key.x) + ", " + std::to_string(key.y) +
                         ") appears twice"};
        const Json &runs = entry.get("runs");
        if (!runs.isArray() || runs.size() % 2 != 0)
            return Error{at + ": runs must be value, count pairs"};
        TileChunk chunk;
        int position = 0;
        for (std::size_t r = 0; r < runs.size(); r += 2) {
            const Json &value = runs.at(r);
            const Json &count = runs.at(r + 1);
            if (!value.isNumber() || !count.isNumber() || count.asInt() < 1 ||
                value.asNumber() < 0 || value.asNumber() > 2147483647.0)
                return Error{at + ": a run needs a tile value and a positive count"};
            if (position + count.asInt() > tileChunkSize * tileChunkSize)
                return Error{at + ": runs cover more than " +
                             std::to_string(tileChunkSize * tileChunkSize) + " cells"};
            const auto cellValue = static_cast<std::int32_t>(value.asInt());
            for (std::int64_t n = 0; n < count.asInt(); ++n, ++position)
                chunk.set(position % tileChunkSize, position / tileChunkSize, cellValue);
        }
        if (position != tileChunkSize * tileChunkSize)
            return Error{at + ": runs cover " + std::to_string(position) + " of " +
                         std::to_string(tileChunkSize * tileChunkSize) + " cells"};
        if (chunk.filled() > 0)
            layer.chunks_.emplace(key, chunk);
    }
    return layer;
}

// ---- Tilemap ------------------------------------------------------------------------------------
void Tilemap::describe(TypeBuilder<Tilemap> &type) {
    type.category("World").description(
        "A grid of tiles from a tileset on any number of layers and world levels: floors, walls, "
        "terrain, roofs, vents, tunnels. The tileset says which tiles are solid, opaque, slow or "
        "breakable. Paint it with the tile tool; entities and prefabs sit alongside it.");
    type.field("tileset", &Tilemap::tileset).asset("tileset");
    type.field("cellSize", &Tilemap::cellSize)
        .range(0.05, 100, 0.05)
        .tooltip("World units per tile. The entity's position is the top-left corner of cell (0, 0).");
    type.field("collisionLayer", &Tilemap::collisionLayer)
        .layer()
        .tooltip("Project collision layer of the solid tiles.");
    type.field("drawLayer", &Tilemap::drawLayer)
        .range(-1000, 1000, 1)
        .tooltip("Draw layer of the map's layers whose own sort layer is 0.");
    type.computed<Json>(
            "layers", [](const Tilemap &map) { return map.layersToJson(); },
            [](Tilemap &map, const Json &json) { return map.setLayersFromJson(json); })
        .tooltip("The tile layers: edited with the tile tool.");
    type.check([](const Entity &entity, const Tilemap &map, const CheckContext &,
                  std::vector<std::string> &problems) {
        if (map.tileset.path.empty())
            problems.push_back("has no tileset, so its tiles cannot be drawn or collide");
        const WorldLevelSet &levels = entity.scene().settings.levels;
        for (const TileLayer &layer : map.layers)
            if (levels.indexOf(layer.level) == unknownLevel)
                problems.push_back("layer '" + layer.name + "' is on level '" + layer.level +
                                   "', which this scene does not define");
        for (std::size_t a = 0; a < map.layers.size(); ++a)
            for (std::size_t b = a + 1; b < map.layers.size(); ++b)
                if (map.layers[a].name == map.layers[b].name)
                    problems.push_back("two layers are both called '" + map.layers[a].name + "'");
    });
}

Vec2 Tilemap::cellSizeInWorld() const {
    const Transform2D world = entity().worldTransform();
    return {cellSize.x * std::fabs(world.scale.x), cellSize.y * std::fabs(world.scale.y)};
}
Vec2 Tilemap::cellToWorld(int x, int y) const {
    const Vec2 size = cellSizeInWorld();
    const Vec2 origin = entity().worldPosition();
    return {origin.x + static_cast<float>(x) * size.x, origin.y + static_cast<float>(y) * size.y};
}
Vec2 Tilemap::cellCenter(int x, int y) const {
    return cellToWorld(x, y) + cellSizeInWorld() * 0.5F;
}
void Tilemap::worldToCell(Vec2 world, int &x, int &y) const {
    const Vec2 size = cellSizeInWorld();
    const Vec2 origin = entity().worldPosition();
    x = static_cast<int>(std::floor((world.x - origin.x) / size.x));
    y = static_cast<int>(std::floor((world.y - origin.y) / size.y));
}

bool Tilemap::setTile(std::size_t layerIndex, int x, int y, std::int32_t value) {
    if (layerIndex >= layers.size())
        return false;
    TileLayer &layer = layers[layerIndex];
    const std::int32_t before = layer.cell(x, y);
    if (!layer.setCell(x, y, value))
        return false;
    ++sequence_;
    ++revision_;
    log_.push_back({sequence_, static_cast<int>(layerIndex), x, y, before, value});
    if (log_.size() > 2 * logLimit)
        log_.erase(log_.begin(), log_.end() - static_cast<std::ptrdiff_t>(logLimit));
    return true;
}
std::int32_t Tilemap::tileAt(std::size_t layerIndex, int x, int y) const {
    return layerIndex < layers.size() ? layers[layerIndex].cell(x, y) : 0;
}
std::size_t Tilemap::addLayer(TileLayer layer) {
    layers.push_back(std::move(layer));
    ++sequence_;
    ++revision_;
    log_.clear(); // Mirrors rebuild: a layer appearing is not a cell edit.
    return layers.size() - 1;
}
std::vector<TileChange> Tilemap::changesSince(std::uint64_t after, bool &complete) const {
    std::vector<TileChange> result;
    complete = log_.empty() ? after == sequence_ : log_.front().sequence <= after + 1;
    for (const TileChange &change : log_)
        if (change.sequence > after)
            result.push_back(change);
    return result;
}

Json Tilemap::layersToJson() const {
    Json array = Json::array();
    for (const TileLayer &layer : layers)
        array.push(layer.toJson());
    return array;
}
bool Tilemap::setLayersFromJson(const Json &json) {
    std::vector<TileLayer> parsed;
    if (json.isNull()) {
        // No layers.
    } else if (!json.isArray()) {
        lastError_ = "tile layers must be an array";
        return false;
    } else {
        for (std::size_t i = 0; i < json.size(); ++i) {
            auto layer = TileLayer::fromJson(json.at(i));
            if (!layer) {
                lastError_ = layer.error();
                return false;
            }
            parsed.push_back(std::move(layer.value()));
        }
    }
    lastError_.clear();
    layers = std::move(parsed);
    ++sequence_;
    ++revision_;
    log_.clear();
    return true;
}
} // namespace yk
