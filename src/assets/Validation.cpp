#include "yk/assets/Validation.hpp"
#include "yk/animation/AnimationController.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Icon.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/Definitions.hpp"
#include "yk/data/GameData.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include "yk/sim/Dialogue.hpp"
#include "yk/sim/Identity.hpp"
#include "yk/sim/Sequence.hpp"
#include "yk/sim/Zones.hpp"
#include <algorithm>
#include <filesystem>
#include <map>

namespace yk {
namespace {
using Severity = ProjectIssue::Severity;

bool isProcedural(const std::string &path) {
    return path.rfind("tone:", 0) == 0 || path.rfind("builtin:", 0) == 0;
}

bool fileExists(const Project &project, const std::string &path) {
    const auto resolved = project.resolve(path);
    std::error_code error;
    return resolved && std::filesystem::exists(resolved.value(), error);
}

Result<Json> readJson(const Project &project, const std::string &path) {
    const auto resolved = project.resolve(path);
    if (!resolved)
        return Error{resolved.error()};
    auto text = readTextFile(resolved.value());
    if (!text)
        return Error{text.error()};
    return Json::parse(text.value());
}

// The animation asset of an AnimatedSprite and its controller must load and fit together, and the
// sprite's own texture should be the sheet the animation draws.
void checkAnimatedSprite(const Project &project, const AnimatedSprite &animated,
                         const SpriteRenderer *sprite, const std::string &where,
                         const std::string &file, std::vector<ProjectIssue> &issues) {
    if (animated.animation.path.empty() || !fileExists(project, animated.animation.path))
        return; // Missing files are reported by the generic asset check.
    auto document = readJson(project, animated.animation.path);
    auto set = document ? parseAnimationSet(document.value())
                        : Result<AnimationSet>(Error{document.error()});
    if (!set)
        return; // The animation asset itself is reported once, by its own check.
    if (!animated.controller.path.empty() && fileExists(project, animated.controller.path)) {
        auto controllerDocument = readJson(project, animated.controller.path);
        auto controller = controllerDocument
                              ? AnimationController::fromJson(controllerDocument.value())
                              : Result<AnimationController>(Error{controllerDocument.error()});
        if (controller) {
            if (auto fits = controller.value().validateAgainst(set.value()); !fits)
                issues.push_back(
                    {Severity::Error, file,
                     where + ": controller does not fit the animation: " + fits.error()});
        }
    }
    if (sprite && !set.value().texture.empty() && !sprite->texture.path.empty() &&
        sprite->texture.path != set.value().texture)
        issues.push_back({Severity::Warning, file,
                          where + ": the sprite shows '" + sprite->texture.path +
                              "' but the animation plays '" + set.value().texture +
                              "' (the animation's sheet wins when the game runs)"});
}

void checkScene(const Project &project, const GameData &data, const Scene &scene,
                const std::string &file, std::vector<ProjectIssue> &issues, bool prefab) {
    scene.forEach([&](const Entity &entity) {
        const std::string owner = "'" + entity.name() + "'";
        // Every finding remembers the entity it is about, so the editor can select it.
        const auto report = [&](ProjectIssue::Severity severity, std::string message) {
            issues.push_back({severity, file, std::move(message), entity.id()});
        };
        if (const std::string &source = entity.prefabSource(); !source.empty()) {
            // The scene holds the instance's whole data, so a missing prefab only breaks
            // Revert/Apply in the editor; still worth knowing about.
            if (!fileExists(project, source))
                report(Severity::Warning,
                       owner + " is an instance of prefab '" + source + "', which does not exist");
            else if (source.size() < 9 || source.compare(source.size() - 9, 9, ".ykprefab") != 0)
                report(Severity::Warning, owner + " names '" + source +
                                              "' as its prefab, which is not a " + prefabExtension +
                                              " file");
        }
        for (const auto &component : entity.components()) {
            const std::string where = owner + " " + component->type().name;
            if (component->type().check) {
                std::vector<std::string> problems;
                CheckContext checking{prefab,
                                      [&](const std::string &message) {
                                          report(Severity::Error, where + ": " + message);
                                      },
                                      [&](std::string_view kind, std::string_view id) {
                                          return data.known(kind, id);
                                      }};
                component->type().check(entity, *component, checking, problems);
                for (const std::string &problem : problems)
                    report(Severity::Warning, where + ": " + problem);
            }
            if (const auto *animated = dynamic_cast<const AnimatedSprite *>(component.get())) {
                const std::size_t before = issues.size();
                checkAnimatedSprite(project, *animated, entity.get<SpriteRenderer>(), where, file,
                                    issues);
                for (std::size_t i = before; i < issues.size(); ++i)
                    issues[i].entity = entity.id();
            }
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
                        report(Severity::Error,
                               where + "." + property.name + ": missing asset '" + path + "'");
                } else if ((property.isInputSet || property.isInputAction) &&
                           property.type == PropertyType::String) {
                    const std::string &name = std::get<std::string>(value);
                    if (name.empty())
                        continue;
                    if (property.isInputSet && !project.input.findSet(name))
                        report(Severity::Warning, where + "." + property.name + ": input set '" +
                                                      name + "' is not defined in the project");
                    if (property.isInputAction) {
                        const auto known = project.input.actionNames();
                        if (std::find(known.begin(), known.end(), name) == known.end())
                            report(Severity::Warning, where + "." + property.name +
                                                          ": input action '" + name +
                                                          "' is not defined in the project");
                    }
                } else if (property.isLayer && property.type == PropertyType::String) {
                    const std::string &layer = std::get<std::string>(value);
                    if (project.layers.indexOf(layer) < 0)
                        report(Severity::Warning, where + "." + property.name +
                                                      ": unknown collision layer '" + layer +
                                                      "' (falls back to layer 0)");
                } else if (property.type == PropertyType::EntityReference) {
                    const EntityId target = std::get<EntityId>(value);
                    if (target && !scene.find(target))
                        report(Severity::Error,
                               where + "." + property.name + ": refers to a missing entity");
                } else if (property.type == PropertyType::EntityReferenceList) {
                    for (const EntityId target : std::get<std::vector<EntityId>>(value))
                        if (target && !scene.find(target))
                            report(Severity::Error,
                                   where + "." + property.name + ": refers to a missing entity");
                }
            }
        }
    });
    // A zone is found by its id (or its entity's name): two with one name make "the cafeteria"
    // ambiguous for schedules and rules.
    if (!prefab) {
        std::map<std::string, const Entity *> zones;
        scene.forEach([&](const Entity &entity) {
            const auto *zone = entity.get<Zone>();
            if (!zone)
                return;
            const auto [first, fresh] = zones.emplace(zone->zoneId(), &entity);
            if (!fresh)
                issues.push_back({Severity::Error, file,
                                  "'" + entity.name() + "' Zone: the id '" + zone->zoneId() +
                                      "' is also used by '" + first->second->name() + "'",
                                  entity.id()});
        });
    }
    // A persistent id names one character for the rest of the game; two with one name are confused
    // with each other (a prefab says nothing: each instance gets its own).
    if (!prefab) {
        std::map<std::string, const Entity *> named;
        scene.forEach([&](const Entity &entity) {
            const auto *who = entity.get<Identity>();
            if (!who || who->id.empty())
                return;
            const auto [first, fresh] = named.emplace(who->id, &entity);
            if (!fresh)
                issues.push_back({Severity::Error, file,
                                  "'" + entity.name() + "' Identity: the persistent id '" +
                                      who->id + "' is also used by '" + first->second->name() + "'",
                                  entity.id()});
        });
    }
}
} // namespace

std::vector<ProjectIssue> validateProject(const Project &project,
                                          const ComponentRegistry &registry) {
    std::vector<ProjectIssue> issues;
    if (auto layers = project.layers.validate(); !layers)
        issues.push_back({Severity::Error, Project::fileName, layers.error()});
    if (auto input = project.input.validate(); !input)
        issues.push_back({Severity::Error, Project::fileName, input.error()});
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
    // The app icon: a real, square PNG (an unusable one would fail only when exporting).
    if (!project.build.icon.empty()) {
        const std::string &icon = project.build.icon;
        if (!fileExists(project, icon)) {
            issues.push_back(
                {Severity::Error, Project::fileName, "the app icon '" + icon + "' does not exist"});
        } else if (auto text = readTextFile(project.resolve(icon).value()); !text) {
            issues.push_back({Severity::Error, Project::fileName,
                              "the app icon '" + icon + "' cannot be read: " + text.error()});
        } else if (auto size = pngSize(text.value()); !size) {
            issues.push_back({Severity::Error, Project::fileName,
                              "the app icon '" + icon + "' is " + size.error()});
        } else if (size.value().width != size.value().height || size.value().width < 128) {
            issues.push_back({Severity::Error, Project::fileName,
                              "the app icon '" + icon +
                                  "' must be square and at least 128 x 128 "
                                  "pixels (it is " +
                                  std::to_string(size.value().width) + " x " +
                                  std::to_string(size.value().height) + ")"});
        } else if (size.value().width < recommendedIconSize) {
            issues.push_back({Severity::Warning, Project::fileName,
                              "the app icon '" + icon + "' is only " +
                                  std::to_string(size.value().width) +
                                  " pixels wide; 512 or 1024 looks sharper on a Retina display"});
        }
    }
    // The project's definitions load together (they refer to each other), and what scenes name is
    // checked against them.
    std::vector<DataProblem> dataProblems;
    const ProjectAssets projectAssets(project);
    const GameData data = GameData::load(projectAssets, dataProblems);
    data.check(registry.extension<RuleCatalog>(), dataProblems);
    for (const DataProblem &problem : dataProblems)
        issues.push_back(
            {problem.error ? Severity::Error : Severity::Warning, problem.file, problem.message});
    // Standalone asset documents: they must parse, and what they point at must exist.
    for (const AssetEntry &entry : assets) {
        if (entry.kind == AssetKind::Animation) {
            auto document = readJson(project, entry.path);
            auto set = document ? parseAnimationSet(document.value())
                                : Result<AnimationSet>(Error{document.error()});
            if (!set)
                issues.push_back({Severity::Error, entry.path, set.error()});
            else if (!set.value().texture.empty() && !isProcedural(set.value().texture) &&
                     !fileExists(project, set.value().texture))
                issues.push_back({Severity::Error, entry.path,
                                  "missing sheet texture '" + set.value().texture + "'"});
        } else if (entry.kind == AssetKind::Controller) {
            auto document = readJson(project, entry.path);
            auto controller = document ? AnimationController::fromJson(document.value())
                                       : Result<AnimationController>(Error{document.error()});
            if (!controller)
                issues.push_back({Severity::Error, entry.path, controller.error()});
        } else if (entry.kind == AssetKind::Dialogue) {
            auto document = readJson(project, entry.path);
            if (!document) {
                issues.push_back({Severity::Error, entry.path, document.error()});
                continue;
            }
            // Pages and graphs are one thing to the validator: a page list is a chain of nodes.
            std::vector<std::string> warnings;
            auto graph = DialogueGraph::fromJson(document.value(), warnings);
            if (!graph) {
                issues.push_back({Severity::Error, entry.path, graph.error()});
                continue;
            }
            graph.value().check(warnings);
            for (const std::string &warning : warnings)
                issues.push_back({Severity::Warning, entry.path, warning});
            for (const std::string &image : graph.value().images())
                if (!image.empty() && !fileExists(project, image))
                    issues.push_back(
                        {Severity::Error, entry.path, "has missing portrait '" + image + "'"});
            if (const RuleCatalog *catalog = registry.extension<RuleCatalog>()) {
                std::vector<DataProblem> found;
                graph.value().visitRules(entry.path, [&](const RuleSource &source) {
                    data.checkRules(*catalog, source, found);
                });
                for (const DataProblem &problem : found)
                    issues.push_back({problem.error ? Severity::Error : Severity::Warning,
                                      problem.file, problem.message});
            }
        } else if (entry.kind == AssetKind::Sequence) {
            auto document = readJson(project, entry.path);
            if (!document)
                continue; // validateDefinitionFile reports a file that cannot be read.
            std::vector<std::string> warnings;
            auto sequence = SequenceDefinition::fromJson(document.value(), warnings);
            if (!sequence) {
                issues.push_back({Severity::Error, entry.path, sequence.error()});
                continue;
            }
            for (const std::string &warning : warnings)
                issues.push_back({Severity::Warning, entry.path, warning});
            if (const RuleCatalog *catalog = registry.extension<RuleCatalog>()) {
                std::vector<DataProblem> found;
                sequence.value().visitRules(entry.path, [&](const RuleSource &source) {
                    data.checkRules(*catalog, source, found);
                });
                for (const DataProblem &problem : found)
                    issues.push_back({problem.error ? Severity::Error : Severity::Warning,
                                      problem.file, problem.message});
            }
        } else if (entry.kind == AssetKind::TextureMeta) {
            auto document = readJson(project, entry.path);
            auto meta = document ? TextureMeta::fromJson(document.value())
                                 : Result<TextureMeta>(Error{document.error()});
            if (!meta)
                issues.push_back({Severity::Error, entry.path, meta.error()});
            const std::string texture = entry.path.substr(
                0, entry.path.size() - std::string(TextureMeta::extension).size());
            if (!fileExists(project, texture))
                issues.push_back({Severity::Warning, entry.path,
                                  "import settings for '" + texture + "', which does not exist"});
        }
    }
    for (const AssetEntry &entry : assets)
        if (isDefinitionKind(entry.kind))
            validateDefinitionFile(project, entry, issues);
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
                checkScene(project, data, *scene.value(), entry.path, issues, false);
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
            else {
                // The entities of a prefab exist only in this scratch scene: nothing to select.
                const std::size_t before = issues.size();
                checkScene(project, data, scratch, entry.path, issues, true);
                for (std::size_t i = before; i < issues.size(); ++i)
                    issues[i].entity = {};
            }
        }
    }
    return issues;
}

bool hasErrors(const std::vector<ProjectIssue> &issues) {
    return std::any_of(issues.begin(), issues.end(),
                       [](const ProjectIssue &issue) { return issue.severity == Severity::Error; });
}
} // namespace yk
