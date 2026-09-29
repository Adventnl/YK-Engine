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
// Puts an instance back to what its prefab says (the editor's Revert to Prefab and the update of
// other instances after Apply). The entity `root` keeps its identity: id, name, transform, tags,
// active/locked/hidden flags, place in the hierarchy, and so every reference from other entities to
// it stays valid. Everything else (its components and all its children) is replaced by the
// prefab's. References an instance's parts held to entities outside the instance (a plate wired to
// a door) survive when the prefab has no reference of its own in that spot. `source` becomes the
// root's prefab link. Nothing changes when the prefab is invalid.
Status reapplyPrefab(Scene &scene, EntityId root, const Json &prefab, const std::string &source);
// Loads `document` and writes it back the way the editor saves it (every property present, stable
// order) while keeping entity ids, so files written by scripts or by hand diff cleanly against
// editor-saved ones. Fail like the loaders do. `yk format` is built on these.
Result<Json> canonicalScene(const Json &document, const ComponentRegistry &registry);
Result<Json> canonicalPrefab(const Json &document, const ComponentRegistry &registry);
Result<Json> loadPrefabDocument(const std::filesystem::path &path);
Status savePrefab(const Scene &scene, EntityId root, const std::filesystem::path &path);
} // namespace yk
