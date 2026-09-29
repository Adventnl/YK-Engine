#include "yk/assets/Validation.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <filesystem>

namespace yk {
namespace {
using Severity = ProjectIssue::Severity;

bool isProcedural(const std::string &path) {
    return path.rfind("tone:", 0) == 0 || path.rfind("builtin:", 0) == 0;
}

void checkScene(const Project &project, const Scene &scene, const std::string &file,
                std::vector<ProjectIssue> &issues) {
    scene.forEach([&](const Entity &entity) {
        const std::string owner = "'" + entity.name() + "'";
        for (const auto &component : entity.components()) {
            const std::string where = owner + " " + component->type().name;
            for (const PropertyInfo &property : component->type().properties) {
                if (property.readOnly)
                    continue;
                const PropertyValue value = property.get(*component);
                if (property.type == PropertyType::Asset) {
                    const std::string &path = std::get<AssetRef>(value).path;
                    if (path.empty() || isProcedural(path))
                        continue;
                    const auto resolved = project.resolve(path);
                    std::error_code error;
                    if (!resolved || !std::filesystem::exists(resolved.value(), error))
                        issues.push_back(
                            {Severity::Error, file,
                             where + "." + property.name + ": missing asset '" + path + "'"});
                } else if (property.isLayer && property.type == PropertyType::String) {
                    const std::string &layer = std::get<std::string>(value);
                    if (project.layers.indexOf(layer) < 0)
                        issues.push_back({Severity::Warning, file,
                                          where + "." + property.name +
                                              ": unknown collision layer '" + layer +
                                              "' (falls back to layer 0)"});
                } else if (property.type == PropertyType::EntityReference) {
                    const EntityId target = std::get<EntityId>(value);
                    if (target && !scene.find(target))
                        issues.push_back(
                            {Severity::Error, file,
                             where + "." + property.name + ": refers to a missing entity"});
                } else if (property.type == PropertyType::EntityReferenceList) {
                    for (const EntityId target : std::get<std::vector<EntityId>>(value))
                        if (target && !scene.find(target))
                            issues.push_back(
                                {Severity::Error, file,
                                 where + "." + property.name + ": refers to a missing entity"});
                }
            }
        }
    });
}
} // namespace

std::vector<ProjectIssue> validateProject(const Project &project,
                                          const ComponentRegistry &registry) {
    std::vector<ProjectIssue> issues;
    if (auto layers = project.layers.validate(); !layers)
        issues.push_back({Severity::Error, Project::fileName, layers.error()});
    const auto assets = scanAssets(project);
    if (!project.startScene.empty()) {
        const bool exists = std::any_of(assets.begin(), assets.end(), [&](const AssetEntry &entry) {
            return entry.path == project.startScene && entry.kind == AssetKind::Scene;
        });
        if (!exists)
            issues.push_back({Severity::Error, Project::fileName,
                              "start scene '" + project.startScene + "' does not exist"});
    } else {
        issues.push_back({Severity::Warning, Project::fileName, "no start scene is set"});
    }
    for (const AssetEntry &entry : assets) {
        if (entry.kind != AssetKind::Scene && entry.kind != AssetKind::Prefab)
            continue;
        const auto absolute = project.resolve(entry.path).value();
        // Loading logs warnings for tolerated problems; only hard failures are reported here.
        if (entry.kind == AssetKind::Scene) {
            auto scene = loadScene(absolute, registry);
            if (!scene)
                issues.push_back({Severity::Error, entry.path, scene.error()});
            else
                checkScene(project, *scene.value(), entry.path, issues);
        } else {
            auto prefab = loadPrefabDocument(absolute);
            if (!prefab) {
                issues.push_back({Severity::Error, entry.path, prefab.error()});
                continue;
            }
            Scene scratch(registry, 1);
            auto instance = instantiateSubtree(scratch, prefab.value());
            if (!instance)
                issues.push_back({Severity::Error, entry.path, instance.error()});
            else
                checkScene(project, scratch, entry.path, issues);
        }
    }
    return issues;
}

bool hasErrors(const std::vector<ProjectIssue> &issues) {
    return std::any_of(issues.begin(), issues.end(),
                       [](const ProjectIssue &issue) { return issue.severity == Severity::Error; });
}
} // namespace yk
