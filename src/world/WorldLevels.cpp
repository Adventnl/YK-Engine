#include "yk/world/WorldLevels.hpp"
#include "yk/runtime/GameContext.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace yk {
const std::vector<std::string> &levelKindNames() {
    static const std::vector<std::string> names{"Floor", "Underground", "Roof", "Vent"};
    return names;
}
const std::vector<std::string> &levelViewModeNames() {
    static const std::vector<std::string> names{"All", "FocusOnly", "FocusAndBelow"};
    return names;
}

int WorldLevelSet::indexOf(std::string_view id) const {
    if (id.empty())
        return 0;
    if (id == everyLevelId)
        return everyLevel;
    for (std::size_t i = 0; i < levels.size(); ++i)
        if (levels[i].id == id)
            return static_cast<int>(i);
    return unknownLevel;
}
const WorldLevelDef *WorldLevelSet::at(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= levels.size())
        return nullptr;
    return &levels[static_cast<std::size_t>(index)];
}
std::string WorldLevelSet::idOf(int index) const {
    const WorldLevelDef *level = at(index);
    return level ? level->id : std::string{};
}
Status WorldLevelSet::validate() const {
    std::set<std::string> seen;
    for (const WorldLevelDef &level : levels) {
        if (level.id.empty())
            return Error{"a world level needs an id"};
        const bool valid = std::all_of(level.id.begin(), level.id.end(), [](char c) {
            return (std::islower(static_cast<unsigned char>(c)) != 0) ||
                   (std::isdigit(static_cast<unsigned char>(c)) != 0) || c == '_' || c == '-';
        });
        if (!valid)
            return Error{"world level id '" + level.id +
                         "' may only use lower case letters, digits, '_' and '-'"};
        if (!seen.insert(level.id).second)
            return Error{"world level id '" + level.id + "' is used twice"};
    }
    if (!(belowAlpha >= 0.0F && belowAlpha <= 1.0F))
        return Error{"the faded alpha of lower levels must be between 0 and 1"};
    return success();
}
Json WorldLevelSet::toJson() const {
    Json object = Json::object();
    Json list = Json::array();
    for (const WorldLevelDef &level : levels) {
        Json entry = Json::object();
        entry.set("id", level.id);
        if (!level.name.empty())
            entry.set("name", level.name);
        entry.set("kind", levelKindNames()[static_cast<std::size_t>(level.kind)]);
        entry.set("elevation", level.elevation);
        list.push(entry);
    }
    object.set("list", list);
    object.set("view", levelViewModeNames()[static_cast<std::size_t>(viewMode)]);
    object.set("belowAlpha", belowAlpha);
    return object;
}
Result<WorldLevelSet> WorldLevelSet::fromJson(const Json &json) {
    if (!json.isObject())
        return Error{"'levels' must be an object"};
    WorldLevelSet set;
    if (const Json *list = json.find("list")) {
        if (!list->isArray())
            return Error{"levels.list must be an array"};
        for (std::size_t i = 0; i < list->size(); ++i) {
            const Json &entry = list->at(i);
            const std::string where = "levels.list[" + std::to_string(i) + "]";
            if (!entry.isObject() || !entry.get("id").isString())
                return Error{where + " needs an id"};
            WorldLevelDef level;
            level.id = entry.get("id").asString();
            level.name = entry.get("name").asString();
            const std::string kind =
                entry.contains("kind") ? entry.get("kind").asString() : "Floor";
            const auto found = std::find(levelKindNames().begin(), levelKindNames().end(), kind);
            if (found == levelKindNames().end())
                return Error{where + ": unknown kind '" + kind + "'"};
            level.kind = static_cast<LevelKind>(found - levelKindNames().begin());
            level.elevation = static_cast<float>(entry.get("elevation").asNumber(0.0));
            set.levels.push_back(std::move(level));
        }
    }
    if (const Json *view = json.find("view")) {
        const auto found =
            std::find(levelViewModeNames().begin(), levelViewModeNames().end(), view->asString());
        if (found == levelViewModeNames().end())
            return Error{"levels.view: unknown mode '" + view->asString() + "'"};
        set.viewMode = static_cast<LevelViewMode>(found - levelViewModeNames().begin());
    }
    set.belowAlpha = static_cast<float>(json.get("belowAlpha").asNumber(0.35));
    if (auto status = set.validate(); !status)
        return Error{status.error()};
    return set;
}

std::vector<float> levelVisibility(const WorldLevelSet &levels, int focus) {
    std::vector<float> alpha(static_cast<std::size_t>(levels.count()), 1.0F);
    if (levels.empty() || levels.viewMode == LevelViewMode::All)
        return alpha;
    for (std::size_t i = 0; i < alpha.size(); ++i) {
        const int index = static_cast<int>(i);
        if (index == focus)
            alpha[i] = 1.0F;
        else if (levels.viewMode == LevelViewMode::FocusAndBelow && index < focus)
            alpha[i] = levels.belowAlpha;
        else
            alpha[i] = 0.0F;
    }
    return alpha;
}

int levelOf(const Entity &entity) {
    const WorldLevelSet &levels = entity.scene().settings.levels;
    for (const Entity *node = &entity; node; node = node->parent())
        if (const auto *layer = node->get<WorldLayer>(); layer && !layer->level.empty()) {
            const int index = levels.indexOf(layer->level);
            return index == unknownLevel ? 0 : index;
        }
    return 0;
}

void WorldLayer::describe(TypeBuilder<WorldLayer> &type) {
    type.category("World").description(
        "Puts this entity (and everything below it) on a world level: a floor, a roof, the vents, "
        "underground. Entities on different levels never collide, see each other or share "
        "navigation. Without it an entity is on its parent's level, or the first level.");
    type.field("level", &WorldLayer::level)
        .ref("level")
        .tooltip(
            "A level id from the scene's World Levels. Empty: the parent's level or the first.");
}
bool WorldLayer::moveTo(GameContext &context, const std::string &levelId) {
    const WorldLevelSet &levels = entity().scene().settings.levels;
    const int to = levels.indexOf(levelId);
    if (to == unknownLevel)
        return false;
    const int from = levelOf(entity());
    level = levelId;
    if (from != to)
        context.notifyLevelChanged(entity(), from, to);
    return true;
}
} // namespace yk
