#include "RuntimeImpl.hpp"
#include "yk/core/Log.hpp"
#include <array>
#include <set>

namespace yk {
namespace {
struct CellRect {
    int x{}, y{}, w{}, h{};
};
// Merges the full-cell solids of a chunk into as few rectangles as possible (rows of equal cells,
// then rows of equal rows), so a wall of a hundred tiles is a handful of shapes.
std::vector<CellRect> mergeSolids(const std::array<bool, tileChunkSize * tileChunkSize> &solid) {
    std::array<bool, tileChunkSize * tileChunkSize> used{};
    const auto at = [](int x, int y) { return static_cast<std::size_t>(y * tileChunkSize + x); };
    std::vector<CellRect> result;
    for (int y = 0; y < tileChunkSize; ++y)
        for (int x = 0; x < tileChunkSize; ++x) {
            if (!solid[at(x, y)] || used[at(x, y)])
                continue;
            int width = 1;
            while (x + width < tileChunkSize && solid[at(x + width, y)] && !used[at(x + width, y)])
                ++width;
            int height = 1;
            for (; y + height < tileChunkSize; ++height) {
                bool whole = true;
                for (int i = 0; i < width && whole; ++i)
                    whole = solid[at(x + i, y + height)] && !used[at(x + i, y + height)];
                if (!whole)
                    break;
            }
            for (int dy = 0; dy < height; ++dy)
                for (int dx = 0; dx < width; ++dx)
                    used[at(x + dx, y + dy)] = true;
            result.push_back({x, y, width, height});
        }
    return result;
}
} // namespace

void GameRuntime::Impl::bindTilemap(Entity &entity, Tilemap &map) {
    if (map.tileset.path.empty())
        return;
    auto set = self.tileset(map.tileset.path);
    if (!set)
        return;
    TilemapBinding binding;
    binding.tileset = set;
    binding.sequence = map.sequence();
    auto &stored = tilemaps[entity.id()] = std::move(binding);
    for (std::size_t li = 0; li < map.layers.size(); ++li)
        if (map.layers[li].solid)
            for (const auto &[key, chunk] : map.layers[li].chunks()) {
                (void)chunk;
                rebuildTileChunk(entity, map, stored, static_cast<int>(li), key);
            }
}

void GameRuntime::Impl::rebuildTileChunk(Entity &entity, const Tilemap &map,
                                         TilemapBinding &binding, int layerIndex, ChunkKey key) {
    const auto slot = std::make_pair(layerIndex, key);
    if (const auto old = binding.bodies.find(slot); old != binding.bodies.end()) {
        if (world->valid(old->second))
            world->destroy(old->second);
        binding.bodies.erase(old);
    }
    const TileLayer &layer = map.layers[static_cast<std::size_t>(layerIndex)];
    const auto chunk = layer.chunks().find(key);
    if (!layer.solid || chunk == layer.chunks().end())
        return;
    std::array<bool, tileChunkSize * tileChunkSize> solid{};
    std::vector<std::pair<Vec2, Vec2>> partial; // Cells with a box smaller than the cell.
    bool any = false;
    for (int y = 0; y < tileChunkSize; ++y)
        for (int x = 0; x < tileChunkSize; ++x) {
            const std::int32_t cell = chunk->second.get(x, y);
            if (tile::empty(cell))
                continue;
            const TileProperties &props = binding.tileset->properties(tile::index(cell));
            if (!props.solid)
                continue;
            any = true;
            if (props.collider) {
                const Vec2 corner = map.cellToWorld(chunkOrigin(key.x) + x, chunkOrigin(key.y) + y);
                const Vec2 cellSize = map.cellSizeInWorld();
                partial.push_back({corner + hadamard(props.collider->position, cellSize),
                                   hadamard(props.collider->size, cellSize)});
            } else {
                solid[static_cast<std::size_t>(y * tileChunkSize + x)] = true;
            }
        }
    if (!any)
        return;
    physics::BodyDef definition;
    definition.type = physics::BodyType::Static;
    auto body = world->createBody(definition);
    if (!body) {
        log(LogLevel::Warning, "physics", "'" + entity.name() + "' tile map: " + body.error());
        return;
    }
    physics::ShapeDef shape;
    shape.friction = 0.0F;
    shape.density = 0.0F;
    shape.filter.categoryBits = options.layers.categoryBits(map.collisionLayer);
    shape.filter.maskBits = options.layers.maskBits(map.collisionLayer);
    if (!scene->settings.levels.empty()) {
        const int level = scene->settings.levels.indexOf(layer.level);
        shape.filter.level = level == unknownLevel ? 0 : level;
    }
    const auto addBox = [&](Vec2 corner, Vec2 size) {
        const physics::Box box{size * 0.5F, corner + size * 0.5F, 0.0F, 0.0F};
        if (auto created = world->createShape(body.value(), box, shape); !created)
            log(LogLevel::Warning, "physics",
                "'" + entity.name() + "' tile map: " + created.error());
    };
    const Vec2 cellSize = map.cellSizeInWorld();
    for (const CellRect &rect : mergeSolids(solid))
        addBox(map.cellToWorld(chunkOrigin(key.x) + rect.x, chunkOrigin(key.y) + rect.y),
               {static_cast<float>(rect.w) * cellSize.x, static_cast<float>(rect.h) * cellSize.y});
    for (const auto &[corner, size] : partial)
        addBox(corner, size);
    binding.bodies.emplace(slot, body.value());
}

void GameRuntime::Impl::unbindTilemap(EntityId id) {
    const auto found = tilemaps.find(id);
    if (found == tilemaps.end())
        return;
    for (auto &[slot, body] : found->second.bodies) {
        (void)slot;
        if (world->valid(body))
            world->destroy(body);
    }
    tilemaps.erase(found);
}

void GameRuntime::Impl::syncTilemaps() {
    for (auto &[id, binding] : tilemaps) {
        Entity *entity = scene->find(id);
        auto *map = entity ? entity->get<Tilemap>() : nullptr;
        if (!map || map->sequence() == binding.sequence)
            continue;
        bool complete = false;
        const auto changes = map->changesSince(binding.sequence, complete);
        if (!complete) { // The map was reloaded or the log overflowed: rebuild every chunk.
            for (auto &[slot, body] : binding.bodies) {
                (void)slot;
                if (world->valid(body))
                    world->destroy(body);
            }
            binding.bodies.clear();
            for (std::size_t li = 0; li < map->layers.size(); ++li)
                if (map->layers[li].solid)
                    for (const auto &[key, chunk] : map->layers[li].chunks()) {
                        (void)chunk;
                        rebuildTileChunk(*entity, *map, binding, static_cast<int>(li), key);
                    }
        } else {
            std::set<std::pair<int, ChunkKey>> dirty;
            for (const TileChange &change : changes)
                dirty.emplace(change.layer, chunkOf(change.x, change.y));
            for (const auto &[layerIndex, key] : dirty)
                if (layerIndex >= 0 && static_cast<std::size_t>(layerIndex) < map->layers.size())
                    rebuildTileChunk(*entity, *map, binding, layerIndex, key);
        }
        binding.sequence = map->sequence();
    }
}
} // namespace yk
