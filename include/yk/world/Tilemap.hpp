#pragma once
#include "yk/core/Json.hpp"
#include "yk/core/Math.hpp"
#include "yk/scene/Component.hpp"
#include "yk/scene/Property.hpp"
#include "yk/scene/Registry.hpp"
#include "yk/world/Tileset.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace yk {
// Tile layers store their cells in chunks of 16 x 16 and keep only the chunks that hold a tile, so
// a large, mostly empty map costs what it paints, drawing and collision work chunk by chunk, and an
// edit touches one chunk.
inline constexpr int tileChunkSize = 16;

struct ChunkKey {
    int x{}, y{};
    friend bool operator==(ChunkKey, ChunkKey) = default;
    friend auto operator<=>(ChunkKey a, ChunkKey b) {
        return a.y != b.y ? a.y <=> b.y : a.x <=> b.x; // Row by row: stable order in files.
    }
};
// Cell coordinates of a chunk's corner, and the chunk a cell belongs to (floor division).
ChunkKey chunkOf(int cellX, int cellY);
inline int chunkOrigin(int chunkCoordinate) {
    return chunkCoordinate * tileChunkSize;
}

class TileChunk {
  public:
    std::int32_t get(int localX, int localY) const {
        return cells_[static_cast<std::size_t>(localY * tileChunkSize + localX)];
    }
    // True when the value changed.
    bool set(int localX, int localY, std::int32_t value);
    int filled() const {
        return filled_;
    }
    const std::array<std::int32_t, tileChunkSize * tileChunkSize> &cells() const {
        return cells_;
    }
    friend bool operator==(const TileChunk &a, const TileChunk &b) {
        return a.cells_ == b.cells_;
    }

  private:
    std::array<std::int32_t, tileChunkSize * tileChunkSize> cells_{};
    int filled_{};
};

// What a layer is for. It only sets sensible defaults when a layer is made (a Wall layer is solid
// and sorts with characters, a Roof is drawn over them); the layer's own flags decide what it does.
enum class TileLayerKind { Floor, Wall, Terrain, Roof, Vent, Underground, Decoration, Collision };
const std::vector<std::string> &tileLayerKindNames();

class TileLayer {
  public:
    std::string name{"Layer"};
    TileLayerKind kind{TileLayerKind::Floor};
    std::string level;    // World level id this layer belongs to; empty: the first; "*": every level.
    bool visible{true};   // Drawn (and shown in the editor).
    bool locked{false};   // The editor will not paint on it.
    bool solid{false};    // Tiles the tileset calls solid block movement, sight and paths.
    bool ySort{false};    // Tiles sort with characters by their lower edge (walls, tall props).
    int sortLayer{0};     // Draw layer, added to the tile map's.
    float order{0.0F};    // Depth within the draw layer.
    float opacity{1.0F};

    std::int32_t cell(int x, int y) const;
    // Sets the cell; true when it changed. An all-empty chunk is dropped.
    bool setCell(int x, int y, std::int32_t value);
    const std::map<ChunkKey, TileChunk> &chunks() const {
        return chunks_;
    }
    bool isEmpty() const {
        return chunks_.empty();
    }
    // Inclusive cell bounds of the tiles painted; false when the layer is empty.
    bool bounds(int &minX, int &minY, int &maxX, int &maxY) const;
    std::size_t tileCount() const;

    Json toJson() const;
    static Result<TileLayer> fromJson(const Json &json);
    friend bool operator==(const TileLayer &a, const TileLayer &b);
    // Layer defaults for a kind.
    static TileLayer make(std::string name, TileLayerKind kind);

  private:
    std::map<ChunkKey, TileChunk> chunks_;
};

// One edit of a tile map, as the systems that mirror it (physics bodies, the navigation grid, the
// renderer's caches) are told. `before`/`after` are cell values.
struct TileChange {
    std::uint64_t sequence{};
    int layer{};
    int x{}, y{};
    std::int32_t before{}, after{};
};

// A grid of tiles drawn from a tileset: floors, walls, terrain, roofs, vents, tunnels and
// decoration, on as many layers as the map needs and on any world level. It sits beside ordinary
// entities and prefabs (a door is still an entity); the tileset says which tiles are solid, opaque,
// slow, or can be broken. The entity's position is the top-left corner of cell (0, 0) and its scale
// multiplies the cell size; rotation is not supported.
class Tilemap final : public Component {
  public:
    AssetRef tileset;
    Vec2 cellSize{1.0F, 1.0F}; // World units per tile.
    std::string collisionLayer{"Solid"}; // Project collision layer of the solid tiles.
    int drawLayer{-10};        // Renderer layer of sort layer 0.
    std::vector<TileLayer> layers;

    static void describe(TypeBuilder<Tilemap> &type);

    // ---- Geometry -----------------------------------------------------------------------------
    Vec2 cellSizeInWorld() const;
    Vec2 cellToWorld(int x, int y) const; // Top-left corner of the cell.
    Vec2 cellCenter(int x, int y) const;
    void worldToCell(Vec2 world, int &x, int &y) const;

    // ---- Editing ------------------------------------------------------------------------------
    // Sets a cell of a layer (false for an unknown layer or an unchanged cell) and records the
    // change for whoever mirrors the map.
    bool setTile(std::size_t layerIndex, int x, int y, std::int32_t value);
    std::int32_t tileAt(std::size_t layerIndex, int x, int y) const;
    std::size_t addLayer(TileLayer layer);
    // Changes with a sequence number above `after` (oldest first). `complete` is false when some of
    // them have been forgotten (the caller fell far behind): rebuild everything instead.
    std::vector<TileChange> changesSince(std::uint64_t after, bool &complete) const;
    std::uint64_t sequence() const {
        return sequence_;
    }
    // Incremented by every edit and every reload of the data (caches key on it).
    std::uint64_t revision() const {
        return revision_;
    }

    // ---- Data ---------------------------------------------------------------------------------
    Json layersToJson() const;
    // Replaces every layer; false (and no change) when the data is invalid.
    bool setLayersFromJson(const Json &json);
    // Reason the last setLayersFromJson failed.
    const std::string &lastError() const {
        return lastError_;
    }

  private:
    std::vector<TileChange> log_;
    std::uint64_t sequence_{};
    std::uint64_t revision_{};
    std::string lastError_;
};
} // namespace yk
