#pragma once
#include "yk/assets/Project.hpp"
#include "yk/scene/Registry.hpp"
#include <string>
#include <vector>

namespace yk {
struct ProjectIssue {
    enum class Severity { Warning, Error };
    Severity severity{Severity::Error};
    std::string path; // Project-relative file (or "project.ykproj").
    std::string message;
    EntityId entity{}; // The entity the finding is about, in a scene (empty: none, or a prefab).
};
// Loads every scene and prefab in the project and checks what a game would trip over at run time:
// documents that fail to load, entity references to missing entities, asset paths with no file,
// unknown collision layers, a missing start scene, invalid layer configuration.
std::vector<ProjectIssue> validateProject(const Project &project,
                                          const ComponentRegistry &registry);
bool hasErrors(const std::vector<ProjectIssue> &issues);
} // namespace yk
