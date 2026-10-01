#include "yk/data/Definitions.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/data/GameData.hpp"
#include "yk/world/Tileset.hpp"
#include <filesystem>

namespace yk {
namespace {
using Severity = ProjectIssue::Severity;

Result<Json> readDefinition(const Project &project, const std::string &path) {
    const auto resolved = project.resolve(path);
    if (!resolved)
        return Error{resolved.error()};
    auto text = readTextFile(resolved.value());
    if (!text)
        return Error{text.error()};
    auto document = Json::parse(text.value());
    if (!document)
        return Error{path + ": " + document.error()};
    return document;
}
bool fileExists(const Project &project, const std::string &path) {
    const auto resolved = project.resolve(path);
    std::error_code error;
    return resolved && std::filesystem::exists(resolved.value(), error);
}
} // namespace

bool isDefinitionKind(AssetKind kind) {
    switch (kind) {
    case AssetKind::Tileset:
    case AssetKind::Data:
    case AssetKind::Item:
    case AssetKind::Recipe:
    case AssetKind::Loot:
    case AssetKind::Quest:
    case AssetKind::Schedule:
    case AssetKind::Sequence:
        return true;
    default:
        return false;
    }
}

void validateDefinitionFile(const Project &project, const AssetEntry &entry,
                            std::vector<ProjectIssue> &issues) {
    auto document = readDefinition(project, entry.path);
    if (!document) {
        issues.push_back({Severity::Error, entry.path, document.error()});
        return;
    }
    switch (entry.kind) {
    case AssetKind::Tileset: {
        auto set = Tileset::fromJson(document.value());
        if (!set) {
            issues.push_back({Severity::Error, entry.path, set.error()});
            return;
        }
        if (set.value().texture.empty())
            issues.push_back({Severity::Error, entry.path, "the tileset names no texture"});
        else if (!fileExists(project, set.value().texture))
            issues.push_back({Severity::Error, entry.path,
                              "missing sheet texture '" + set.value().texture + "'"});
        break;
    }
    default:
        break;
    }
}

DefinitionSummary describeDefinition(const Project &project, const std::string &path) {
    DefinitionSummary summary;
    auto document = readDefinition(project, path);
    if (!document) {
        summary.error = document.error();
        return summary;
    }
    switch (classifyAsset(path)) {
    case AssetKind::Tileset: {
        auto set = Tileset::fromJson(document.value());
        if (!set) {
            summary.error = set.error();
            return summary;
        }
        const Tileset &tiles = set.value();
        summary.rows.push_back({"Name", tiles.name.empty() ? "(unnamed)" : tiles.name});
        summary.rows.push_back({"Sheet", tiles.texture});
        summary.rows.push_back({"Tile size", std::to_string(tiles.tileWidth) + " x " +
                                                 std::to_string(tiles.tileHeight) + " px"});
        summary.rows.push_back({"Tiles", std::to_string(tiles.tileCount()) + " (" +
                                             std::to_string(tiles.columns) + " x " +
                                             std::to_string(tiles.rows) + ")"});
        std::size_t solid = 0, opaque = 0, animated = 0, modifiable = 0;
        for (const auto &[index, props] : tiles.tiles) {
            (void)index;
            solid += props.solid ? 1U : 0U;
            opaque += props.opaque ? 1U : 0U;
            animated += props.animation.frames.empty() ? 0U : 1U;
            modifiable += props.modify.action.empty() ? 0U : 1U;
        }
        summary.rows.push_back({"With properties", std::to_string(tiles.tiles.size())});
        summary.rows.push_back(
            {"Solid / opaque", std::to_string(solid) + " / " + std::to_string(opaque)});
        summary.rows.push_back({"Animated / breakable",
                                std::to_string(animated) + " / " + std::to_string(modifiable)});
        break;
    }
    case AssetKind::Data:
    case AssetKind::Item:
    case AssetKind::Recipe:
    case AssetKind::Loot:
    case AssetKind::Quest:
    case AssetKind::Schedule:
    case AssetKind::Sequence: {
        GameData scratch;
        std::vector<DataProblem> problems;
        scratch.add(document.value(), path, problems);
        summary.rows = scratch.summary();
        std::size_t errors = 0, warnings = 0;
        for (const DataProblem &problem : problems)
            (problem.error ? errors : warnings)++;
        if (errors + warnings > 0)
            summary.rows.push_back({"Problems", std::to_string(errors) + " errors, " +
                                                    std::to_string(warnings) + " warnings"});
        if (!problems.empty())
            summary.rows.push_back({"First", problems.front().message});
        break;
    }
    default:
        break;
    }
    return summary;
}
} // namespace yk
