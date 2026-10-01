#include "yk/world/WorldGrid.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
void WorldGrid::configure(const GridSpec &spec) {
    spec_ = spec;
    spec_.cellSize = spec.cellSize > 0.01F ? spec.cellSize : 0.01F;
    spec_.width = std::max(spec.width, 0);
    spec_.height = std::max(spec.height, 0);
    spec_.levels = std::max(spec.levels, 1);
    cells_.assign(static_cast<std::size_t>(spec_.width) * static_cast<std::size_t>(spec_.height) *
                      static_cast<std::size_t>(spec_.levels),
                  GridCell{});
    dirty_.clear();
    ++revision_;
}

void WorldGrid::worldToCell(Vec2 world, int &x, int &y) const {
    x = static_cast<int>(std::floor((world.x - spec_.origin.x) / spec_.cellSize));
    y = static_cast<int>(std::floor((world.y - spec_.origin.y) / spec_.cellSize));
}
Vec2 WorldGrid::cellCenter(int x, int y) const {
    return {spec_.origin.x + (static_cast<float>(x) + 0.5F) * spec_.cellSize,
            spec_.origin.y + (static_cast<float>(y) + 0.5F) * spec_.cellSize};
}
Rect WorldGrid::cellRect(int x, int y) const {
    return {{spec_.origin.x + static_cast<float>(x) * spec_.cellSize,
             spec_.origin.y + static_cast<float>(y) * spec_.cellSize},
            {spec_.cellSize, spec_.cellSize}};
}

bool WorldGrid::blocked(int level, int x, int y) const {
    if (!inside(level, x, y))
        return true;
    const GridCell &cell = at(level, x, y);
    return (cell.flags & cellflag::solid) != 0 || cell.blockers > 0;
}
bool WorldGrid::opaque(int level, int x, int y) const {
    if (!inside(level, x, y))
        return true;
    const GridCell &cell = at(level, x, y);
    return (cell.flags & cellflag::opaque) != 0 || cell.sightBlockers > 0;
}

std::vector<std::pair<int, int>> WorldGrid::lineCells(Vec2 from, Vec2 to) const {
    // Amanatides & Woo grid traversal, stepping across whichever cell boundary comes first; when
    // the line passes exactly through a corner both neighbors are visited (supercover), so a
    // diagonal wall cannot be slipped through.
    std::vector<std::pair<int, int>> cells;
    int x = 0, y = 0, endX = 0, endY = 0;
    worldToCell(from, x, y);
    worldToCell(to, endX, endY);
    cells.emplace_back(x, y);
    const Vec2 delta = to - from;
    const int stepX = delta.x > 0 ? 1 : (delta.x < 0 ? -1 : 0);
    const int stepY = delta.y > 0 ? 1 : (delta.y < 0 ? -1 : 0);
    const float cell = spec_.cellSize;
    const auto boundary = [&](int index, int step, float origin) {
        return origin + (static_cast<float>(index) + (step > 0 ? 1.0F : 0.0F)) * cell;
    };
    float tMaxX = stepX == 0 ? INFINITY : (boundary(x, stepX, spec_.origin.x) - from.x) / delta.x;
    float tMaxY = stepY == 0 ? INFINITY : (boundary(y, stepY, spec_.origin.y) - from.y) / delta.y;
    const float tDeltaX = stepX == 0 ? INFINITY : cell / std::fabs(delta.x);
    const float tDeltaY = stepY == 0 ? INFINITY : cell / std::fabs(delta.y);
    const int limit = std::abs(endX - x) + std::abs(endY - y) + 2;
    for (int i = 0; i < limit && (x != endX || y != endY); ++i) {
        const float epsilon = 1e-6F;
        if (std::fabs(tMaxX - tMaxY) <= epsilon && stepX != 0 && stepY != 0) {
            cells.emplace_back(x + stepX, y); // The corner: both cells beside it.
            cells.emplace_back(x, y + stepY);
            x += stepX;
            y += stepY;
            tMaxX += tDeltaX;
            tMaxY += tDeltaY;
        } else if (tMaxX < tMaxY) {
            x += stepX;
            tMaxX += tDeltaX;
        } else {
            y += stepY;
            tMaxY += tDeltaY;
        }
        cells.emplace_back(x, y);
    }
    return cells;
}

WorldGrid::SightLine WorldGrid::lineOfSight(int level, Vec2 from, Vec2 to) const {
    SightLine result;
    int startX = 0, startY = 0;
    worldToCell(from, startX, startY);
    for (const auto &[x, y] : lineCells(from, to)) {
        if (x == startX && y == startY)
            continue; // The viewer's own cell never blocks its view (standing in a doorway).
        if (opaque(level, x, y)) {
            result.clear = false;
            result.blockX = x;
            result.blockY = y;
            return result;
        }
    }
    return result;
}

float WorldGrid::soundTransmission(int level, Vec2 from, Vec2 to) const {
    float through = 1.0F;
    int startX = 0, startY = 0;
    worldToCell(from, startX, startY);
    for (const auto &[x, y] : lineCells(from, to)) {
        if (x == startX && y == startY)
            continue;
        if (!inside(level, x, y))
            return 0.0F;
        const GridCell &cell = at(level, x, y);
        float damping = static_cast<float>(cell.noiseDamping) / 255.0F;
        if (cell.sightBlockers > 0 || (cell.flags & cellflag::opaque) != 0)
            damping = std::max(damping, 0.5F); // Closed doors and walls muffle even if untuned.
        through *= 1.0F - damping;
        if (through <= 0.001F)
            return 0.0F;
    }
    return through;
}

void WorldGrid::markDirty(const CellRect &rect) {
    ++revision_;
    dirty_.push_back(rect);
}
std::vector<CellRect> WorldGrid::takeDirty() {
    std::vector<CellRect> result;
    result.swap(dirty_);
    return result;
}
void WorldGrid::addBlockers(const CellRect &rect, int delta) {
    const int minX = std::max(rect.minX, 0), minY = std::max(rect.minY, 0);
    const int maxX = std::min(rect.maxX, spec_.width - 1),
              maxY = std::min(rect.maxY, spec_.height - 1);
    if (rect.level < 0 || rect.level >= spec_.levels || minX > maxX || minY > maxY)
        return;
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x) {
            GridCell &cell = at(rect.level, x, y);
            const int next = std::clamp(static_cast<int>(cell.blockers) + delta, 0, 255);
            cell.blockers = static_cast<std::uint8_t>(next);
        }
    markDirty({rect.level, minX, minY, maxX, maxY});
}
void WorldGrid::addSightBlockers(const CellRect &rect, int delta) {
    const int minX = std::max(rect.minX, 0), minY = std::max(rect.minY, 0);
    const int maxX = std::min(rect.maxX, spec_.width - 1),
              maxY = std::min(rect.maxY, spec_.height - 1);
    if (rect.level < 0 || rect.level >= spec_.levels || minX > maxX || minY > maxY)
        return;
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x) {
            GridCell &cell = at(rect.level, x, y);
            const int next = std::clamp(static_cast<int>(cell.sightBlockers) + delta, 0, 255);
            cell.sightBlockers = static_cast<std::uint8_t>(next);
        }
    ++revision_;
}
} // namespace yk
