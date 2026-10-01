#pragma once
#include "yk/core/Math.hpp"
#include <cstdint>
#include <vector>

namespace yk {
// The simulation's own picture of the world: a uniform grid per world level holding what movement,
// sight and sound need to know about every cell, however the cell got that way (a solid tile, a
// closed door, a crate the player pushed, a patch of mud). Physics shapes answer "what touches
// this body"; the grid answers "can anything walk here, can anyone see across this, how far does a
// shout carry" for hundreds of agents without asking Box2D.
//
// Static facts (flags, area, cost, noise damping) come from tile maps and zones and are replaced
// when those change; dynamic facts (blockers, sight blockers) are counted, so two things can block
// the same cell and the cell opens only when both are gone.
struct GridSpec {
    Vec2 origin{};        // World position of the corner of cell (0, 0).
    float cellSize{0.5F}; // Meters.
    int width{0}, height{0};
    int levels{1};
    friend bool operator==(const GridSpec &, const GridSpec &) = default;
};

namespace cellflag {
inline constexpr std::uint8_t solid = 1;  // Static: nothing walks here.
inline constexpr std::uint8_t opaque = 2; // Static: nothing sees through.
} // namespace cellflag

struct GridCell {
    std::uint8_t flags{};     // cellflag::*
    std::uint8_t area{0};     // Navigation area (0 is the ordinary one).
    std::uint8_t blockers{0}; // Dynamic things standing in the way.
    std::uint8_t sightBlockers{0};
    std::uint16_t cost{16};       // Movement cost multiplier in 1/16 (16 is 1.0).
    std::uint16_t door{0};        // Door covering this cell (0 none).
    std::uint8_t noiseDamping{0}; // Fraction of a sound a cell absorbs, in 1/255.
    std::uint8_t reserved{0};
    std::uint16_t zone{0}; // Zone owning this cell (0 none).
    std::uint8_t zonePriority{0};
    std::uint8_t reserved2{0};
};

struct CellRect {
    int level{}, minX{}, minY{}, maxX{}, maxY{}; // Inclusive.
};

class WorldGrid {
  public:
    void configure(const GridSpec &spec); // Clears every cell.
    const GridSpec &spec() const {
        return spec_;
    }
    bool configured() const {
        return spec_.width > 0 && spec_.height > 0;
    }
    bool inside(int level, int x, int y) const {
        return level >= 0 && level < spec_.levels && x >= 0 && y >= 0 && x < spec_.width &&
               y < spec_.height;
    }
    GridCell &at(int level, int x, int y) {
        return cells_[index(level, x, y)];
    }
    const GridCell &at(int level, int x, int y) const {
        return cells_[index(level, x, y)];
    }
    std::size_t index(int level, int x, int y) const {
        return (static_cast<std::size_t>(level) * static_cast<std::size_t>(spec_.height) +
                static_cast<std::size_t>(y)) *
                   static_cast<std::size_t>(spec_.width) +
               static_cast<std::size_t>(x);
    }
    std::size_t cellCount() const {
        return cells_.size();
    }

    // ---- Conversions ---------------------------------------------------------------------------
    void worldToCell(Vec2 world, int &x, int &y) const;
    Vec2 cellCenter(int x, int y) const;
    Rect cellRect(int x, int y) const;

    // ---- Questions -----------------------------------------------------------------------------
    // Nothing can stand here: a static solid or a dynamic blocker. Outside the grid counts as
    // blocked.
    bool blocked(int level, int x, int y) const;
    // Sight cannot pass: a static opaque cell or a dynamic sight blocker.
    bool opaque(int level, int x, int y) const;

    struct SightLine {
        bool clear{true};
        int blockX{}, blockY{}; // The first opaque cell on the line, when not clear.
    };
    // Walks the cells between two points (a supercover line, so a corner cannot be slipped
    // through) and reports whether any is opaque. Cells outside the grid block.
    SightLine lineOfSight(int level, Vec2 from, Vec2 to) const;
    // The fraction of a sound that arrives along the straight line, 0..1: the product of what each
    // cell on the way lets through, ignoring distance falloff (the caller applies that).
    float soundTransmission(int level, Vec2 from, Vec2 to) const;
    // Every cell on the line from `from` to `to`, in order.
    std::vector<std::pair<int, int>> lineCells(Vec2 from, Vec2 to) const;

    // ---- Change tracking -----------------------------------------------------------------------
    // Everything that changes a cell's passability marks its rectangle; the navigation world
    // collects them to refresh what it derives (clearance).
    void markDirty(const CellRect &rect);
    std::vector<CellRect> takeDirty();
    bool hasDirty() const {
        return !dirty_.empty();
    }
    std::uint64_t revision() const {
        return revision_;
    }

    // Adds `delta` (+1 or -1) to the dynamic blocker count of every cell of a rectangle (clipped to
    // the grid), marking it dirty. Counts never go below zero.
    void addBlockers(const CellRect &rect, int delta);
    void addSightBlockers(const CellRect &rect, int delta);

  private:
    GridSpec spec_;
    std::vector<GridCell> cells_;
    std::vector<CellRect> dirty_;
    std::uint64_t revision_{};
};
} // namespace yk
