#pragma once
#include "yk/core/Json.hpp"
#include "yk/core/Result.hpp"
#include "yk/scene/Component.hpp"
#include "yk/scene/Registry.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace yk {
class Entity;
class GameContext;

// A world level is one floor of a single continuous simulation: ground, an upper floor, a roof, a
// vent crawlspace, a tunnel below ground. It is not a render layer: it decides what collides with
// what, what an agent can walk on or see, which tiles are drawn, where a noise carries, and what a
// stair connects. A game with one floor never mentions levels; an old scene has none.
enum class LevelKind { Floor, Underground, Roof, Vent };
const std::vector<std::string> &levelKindNames();

// What an entity's level can be besides an index: present on every level (an outer wall, a stair
// well that spans floors; written "*" in data), or an id the scene does not define.
inline constexpr int everyLevel = -1;
inline constexpr int unknownLevel = -2;
inline constexpr const char *everyLevelId = "*";

struct WorldLevelDef {
    std::string id;   // Stable key ("ground", "floor1", "vents"); lower case letters, digits, _ and -.
    std::string name; // For people ("Ground floor"); the id when empty.
    LevelKind kind{LevelKind::Floor};
    float elevation{0.0F}; // Meters above the ground floor (negative below), for falls and the map.
    friend bool operator==(const WorldLevelDef &, const WorldLevelDef &) = default;
    std::string label() const {
        return name.empty() ? id : name;
    }
};

// What the game view shows of the levels other than the one being looked at (the focus).
//   All            every level is drawn (the default: a one-floor game, or a stacked map)
//   FocusOnly      only the focus level
//   FocusAndBelow  the focus level and, faded, the levels below it (and nothing above)
enum class LevelViewMode { All, FocusOnly, FocusAndBelow };
const std::vector<std::string> &levelViewModeNames();

struct WorldLevelSet {
    std::vector<WorldLevelDef> levels; // In vertical order, lowest first. Empty: one implicit level.
    LevelViewMode viewMode{LevelViewMode::All};
    float belowAlpha{0.35F};

    bool empty() const {
        return levels.empty();
    }
    // Number of levels a simulation has: at least one.
    int count() const {
        return levels.empty() ? 1 : static_cast<int>(levels.size());
    }
    // Index of the level with that id; an empty id names the first level (index 0), "*" is
    // everyLevel; unknownLevel when no level has that id.
    int indexOf(std::string_view id) const;
    // The level at an index, or null (the implicit level has no definition).
    const WorldLevelDef *at(int index) const;
    std::string idOf(int index) const;
    Status validate() const;
    Json toJson() const;
    static Result<WorldLevelSet> fromJson(const Json &json);
    friend bool operator==(const WorldLevelSet &, const WorldLevelSet &) = default;
};

// How strongly each level is drawn (0 hidden .. 1 fully) when the game looks at level `focus`:
// all of them for LevelViewMode::All, only the focus for FocusOnly, the focus and the faded levels
// below it for FocusAndBelow. One entry per level; a scene with no levels has one.
std::vector<float> levelVisibility(const WorldLevelSet &levels, int focus);

// The level an entity is on: the nearest `WorldLayer` at or above it, else the first level (an
// unknown id also reads as the first; validation reports it). everyLevel for "*".
int levelOf(const Entity &entity);

// Puts an entity (and what it carries) on a level. The level an entity occupies matters to
// physics (shapes of different levels never touch), navigation, perception and drawing; this is
// the one place that changes it at run time, so all of them hear about it ("level_changed").
class WorldLayer final : public Component {
  public:
    std::string level; // A level id of the scene, or "*" for every level; empty: the parent's or the first.
    static void describe(TypeBuilder<WorldLayer> &type);
    // At run time: moves the entity and everything below it to `levelId`. False for an unknown id.
    bool moveTo(GameContext &context, const std::string &levelId);
};
} // namespace yk
