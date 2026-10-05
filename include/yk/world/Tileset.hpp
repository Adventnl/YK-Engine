#pragma once
#include "yk/core/Json.hpp"
#include "yk/core/Math.hpp"
#include "yk/core/Result.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace yk {
inline constexpr const char *tilesetFormatName = "yk.tileset";
inline constexpr const char *tilesetExtension = ".yktileset";
inline constexpr int tilesetFormatVersion = 1;

// What a tile means to the simulation, beyond how it looks. A tile map is only art plus these
// properties: the same wall tile blocks the character, the guard's sight line and the path of an
// agent because the tileset says it is solid and opaque, not because a game wrote code for walls.
struct TileProperties {
    bool solid{false};  // Blocks movement: physics collision and navigation.
    bool opaque{false}; // Blocks line of sight.
    std::string area;   // Navigation area name ("grass", "restricted"); empty: the default area.
    float cost{1.0F};   // Navigation cost multiplier (1 normal, 3 slow ground).
    float noiseDamping{0.0F}; // 0..1: how much of a noise a tile absorbs (a thick wall near 1).
    std::vector<std::string> tags; // Free labels ("wall", "floor", "vent"), for scripts and rules.
    // A solid tile blocks its whole cell unless it names a smaller box (fractions of the tile,
    // 0..1: a fence post, a thin wall along the top of the cell).
    std::optional<Rect> collider;
    struct Animation {
        std::vector<int> frames; // Tile indices shown in turn (water, a flickering light).
        float fps{4.0F};
        friend bool operator==(const Animation &, const Animation &) = default;
    } animation;
    // How the world can change it (cut a fence, dig, break a wall): the tool action that does it,
    // how much work it takes, and what is left (-1: an empty cell). The game rules decide who may.
    struct Modify {
        std::string action;
        float resistance{0.0F};
        int result{-1};
        std::string material;
        friend bool operator==(const Modify &, const Modify &) = default;
    } modify;
    friend bool operator==(const TileProperties &, const TileProperties &) = default;
};

// A sheet of equally sized tiles and the properties of the ones that have any (`.yktileset`).
// Tiles are addressed by their cell in the sheet, counted from 0 along the rows; a tile map stores
// that index plus one, so that zero can mean an empty cell.
struct Tileset {
    std::string name;
    std::string texture;               // Project-relative sheet.
    int tileWidth{16}, tileHeight{16}; // Pixels.
    int columns{1}, rows{1};
    int spacing{0}, margin{0};           // Pixels between tiles and around the sheet.
    std::map<int, TileProperties> tiles; // Sparse: tiles not listed use the defaults.

    int tileCount() const {
        return columns * rows;
    }
    const TileProperties &properties(int index) const;
    // Pixels of tile `index` in the sheet.
    Rect sourceRect(int index) const;
    // The tile to draw now: animated tiles step through their frames with `seconds`.
    int frameAt(int index, double seconds) const;
    Status validate() const;
    Json toJson() const;
    static Result<Tileset> fromJson(const Json &json);
};

// The cell values stored in tile layers: 0 is empty; otherwise the low 24 bits hold tile index + 1
// and the high bits flags.
namespace tile {
inline constexpr std::int32_t indexMask = 0x00FFFFFF;
inline constexpr std::int32_t flipX = 1 << 28;
inline constexpr std::int32_t flipY = 1 << 29;
inline constexpr std::int32_t transpose = 1 << 30;
inline constexpr std::int32_t flagMask = flipX | flipY | transpose;
inline constexpr std::int32_t make(int index, std::int32_t flags = 0) {
    return static_cast<std::int32_t>(index + 1) | (flags & flagMask);
}
inline constexpr bool empty(std::int32_t cell) {
    return (cell & indexMask) == 0;
}
inline constexpr int index(std::int32_t cell) {
    return (cell & indexMask) - 1;
}
inline constexpr std::int32_t flags(std::int32_t cell) {
    return cell & flagMask;
}
} // namespace tile
} // namespace yk
