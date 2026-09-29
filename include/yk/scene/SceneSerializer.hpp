#pragma once
#include "yk/core/Json.hpp"
#include "yk/scene/Scene.hpp"
#include <filesystem>
#include <memory>
#include <optional>

namespace yk {
inline constexpr int sceneFormatVersion = 1;
inline constexpr const char *sceneFormatName = "yk.scene";
inline constexpr const char *prefabFormatName = "yk.prefab";
inline constexpr const char *sceneExtension = ".ykscene";
inline constexpr const char *prefabExtension = ".ykprefab";

// Whole-scene documents. Loading validates structure and reports the entity/component/property that
// is wrong; it never returns a partially built scene.
Json sceneToJson(const Scene &scene);
Result<std::unique_ptr<Scene>>
sceneFromJson(const Json &document, const ComponentRegistry &registry, std::uint64_t idSeed = 0);
Result<std::unique_ptr<Scene>> loadScene(const std::filesystem::path &path,
                                         const ComponentRegistry &registry);
Status saveScene(const Scene &scene, const std::filesystem::path &path);

// Reusable entities ("prefabs"): an entity subtree that can be instantiated any number of times.
// Instantiation assigns fresh ids and remaps references between the copied entities; references to
// entities outside the subtree are cleared because they mean nothing in the destination.
Json subtreeToJson(const Scene &scene, EntityId root);
Result<EntityId> instantiateSubtree(Scene &scene, const Json &prefab, EntityId parent = {},
                                    std::optional<Vec2> worldPosition = std::nullopt);
Result<Json> loadPrefabDocument(const std::filesystem::path &path);
Status savePrefab(const Scene &scene, EntityId root, const std::filesystem::path &path);
} // namespace yk
