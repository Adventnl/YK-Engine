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
// Duplicating inside the same scene passes keepExternalReferences so a copied plate still opens its
// door; references to entities the destination scene does not have are cleared either way.
//
// An instance remembers where it came from: the root of an instantiated prefab gets
// Entity::prefabSource (the prefab's project-relative path, when the caller passes one), which the
// scene stores as "prefab". Copies made with subtreeToJson keep that link; a saved prefab file
// never names itself, so savePrefab leaves it off the root.
Json subtreeToJson(const Scene &scene, EntityId root);
Result<EntityId> instantiateSubtree(Scene &scene, const Json &prefab, EntityId parent = {},
                                    std::optional<Vec2> worldPosition = std::nullopt,
                                    bool keepExternalReferences = false,
                                    const std::string &source = {});
// Instantiates several prefab documents in one step, each under its own parent, and remaps
// references across all of them: copies of a plate and a door refer to each other, not to the
// originals. Nothing is created when any document is invalid. Returns the new roots in order.
struct PrefabPlacement {
    const Json *prefab;
    EntityId parent;
    std::string source{}; // Project-relative prefab path recorded on the new root; may be empty.
};
Result<std::vector<EntityId>> instantiateSubtrees(Scene &scene,
                                                  const std::vector<PrefabPlacement> &placements,
                                                  bool keepExternalReferences = false);
Result<Json> loadPrefabDocument(const std::filesystem::path &path);
Status savePrefab(const Scene &scene, EntityId root, const std::filesystem::path &path);
} // namespace yk
