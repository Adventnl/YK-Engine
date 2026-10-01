#pragma once
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Validation.hpp"
#include <string>
#include <utility>
#include <vector>

namespace yk {
// Project definitions are JSON assets with a format name and a version (tilesets, and the item,
// recipe, loot, quest, schedule, brain, rules and dialogue files that follow). This is the one
// place that knows how to check and describe each kind, so the validator, `yk validate`, the
// editor's Inspector and the project scanner all agree.

// What a definition file contains, in a few lines for the Inspector; `error` says why it does not
// load.
struct DefinitionSummary {
    std::string error;
    std::vector<std::pair<std::string, std::string>> rows;
};
DefinitionSummary describeDefinition(const Project &project, const std::string &path);

// True for the asset kinds this module owns.
bool isDefinitionKind(AssetKind kind);
// Checks one definition file (parse, structure, files it points at) and appends what is wrong.
void validateDefinitionFile(const Project &project, const AssetEntry &entry,
                            std::vector<ProjectIssue> &issues);
} // namespace yk
