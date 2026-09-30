#pragma once
#include "yk/core/Json.hpp"
#include "yk/scene/Scene.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace yk::editor {
enum class SelectMode { Replace, Add, Toggle };

// A scene open in the editor: the scene itself, the selection, unsaved-change tracking and undo.
//
// Reading goes through scene(). Every modification happens inside a change (beginChange/endChange,
// or change() with a callable, or one of the ready-made edit functions below). A change remembers
// the scene before it started and, when it ends with something different, becomes one undo step. A
// change that ends identical to its start leaves no trace. Dragging a gizmo or a slider is one long
// change, so one Undo reverts the whole drag.
//
// Undo and redo rebuild the scene from a snapshot, so Scene, Entity and Component pointers do not
// survive them. Keep EntityIds between frames and look entities up through scene().
class EditorDocument {
  public:
    // Takes ownership. `path` is project-relative; empty for a scene that was never saved.
    EditorDocument(const ComponentRegistry &registry, std::unique_ptr<Scene> scene,
                   std::string path = {});
    EditorDocument(const EditorDocument &) = delete;
    EditorDocument &operator=(const EditorDocument &) = delete;

    const ComponentRegistry &registry() const {
        return *registry_;
    }
    const Scene &scene() const {
        return *scene_;
    }
    // The scene, for modification. Only valid between beginChange and endChange.
    Scene &edit();

    const std::string &path() const {
        return path_;
    }
    void setPath(std::string path) {
        path_ = std::move(path);
    }
    // File name without directory or extension, or "Untitled" for an unsaved scene.
    std::string displayName() const;

    // True when the scene differs from what was last saved (undoing back to it clears this).
    bool dirty() const {
        return version_ != savedVersion_;
    }
    void markSaved() {
        savedVersion_ = version_;
    }
    // Bumps on every committed change, undo and redo, so views can refresh derived data.
    std::uint64_t revision() const {
        return revision_;
    }

    // ----- Selection ---------------------------------------------------------------------------
    // Ordered; the last entry is the primary selection (the one the inspector and gizmos use).
    const std::vector<EntityId> &selection() const {
        return selection_;
    }
    EntityId primary() const {
        return selection_.empty() ? EntityId{} : selection_.back();
    }
    bool isSelected(EntityId id) const;
    void select(EntityId id, SelectMode mode = SelectMode::Replace);
    void select(const std::vector<EntityId> &ids, SelectMode mode = SelectMode::Replace);
    void selectAll();
    void clearSelection() {
        selection_.clear();
    }
    // Selected entities that have no selected ancestor: the ones a move or delete acts on.
    std::vector<EntityId> selectionRoots() const;

    // ----- Changes -----------------------------------------------------------------------------
    void beginChange(std::string label);
    // Commits the open change as an undo step if it changed anything.
    void endChange();
    bool inChange() const {
        return open_;
    }
    // Abandons the open change and restores the scene as it was (a drag cancelled with Escape).
    void cancelChange();
    // Runs `edit` as one change, or inside the already open one.
    void change(const std::string &label, const std::function<void(Scene &)> &edit);

    // ----- Undo --------------------------------------------------------------------------------
    bool canUndo() const {
        return !undo_.empty() && !open_;
    }
    bool canRedo() const {
        return !redo_.empty() && !open_;
    }
    std::string undoLabel() const {
        return undo_.empty() ? std::string{} : undo_.back().label;
    }
    std::string redoLabel() const {
        return redo_.empty() ? std::string{} : redo_.back().label;
    }
    bool undo();
    bool redo();
    std::size_t undoDepth() const {
        return undo_.size();
    }
    static constexpr std::size_t maxUndoSteps = 200;

    // ----- Ready-made edits (each one undo step) -----------------------------------------------
    // New entities are named uniquely ("Platform", "Platform (1)", ...) and selected.
    EntityId createEntity(const std::string &name, EntityId parent = {},
                          std::optional<Vec2> worldPosition = std::nullopt);
    Result<EntityId> createFromTemplate(const EntityTemplate &entityTemplate, Vec2 worldPosition,
                                        EntityId parent = {});
    // `prefab` is a subtreeToJson document. References to entities outside it are cleared. `source`
    // is the prefab's project-relative path; the new root remembers it as its prefab.
    Result<EntityId> instantiatePrefab(const Json &prefab, Vec2 worldPosition, EntityId parent = {},
                                       const std::string &source = {});
    // Copies each selected root next to its original (offset in world units) and selects the
    // copies.
    std::vector<EntityId> duplicateSelection(Vec2 offset = {0.5F, 0.5F});
    // Deletes the selection and everything below it; references to deleted entities are cleared.
    void deleteSelection();
    // Keeps the entity where it is in the world.
    Status reparent(EntityId child, EntityId newParent, std::optional<std::size_t> index = {});
    Status moveAmongSiblings(EntityId id, int steps);
    // False when the entity is already first (steps < 0) or last (steps > 0) among its siblings,
    // so menus can offer the move only when it does something.
    bool canMoveAmongSiblings(EntityId id, int steps) const;
    void rename(EntityId id, const std::string &name);
    void setEntityActive(EntityId id, bool active);
    // Editor hints saved with the scene (see Entity::locked, Entity::editorHidden).
    void setLocked(const std::vector<EntityId> &ids, bool locked);
    void setEditorHidden(const std::vector<EntityId> &ids, bool hidden);
    // Prefab instances. An instance is an entity subtree whose root remembers the prefab it came
    // from. `prefabRootOf` finds the instance a selected entity belongs to (null when it is not
    // part of one); the others change the scene in one undo step each.
    EntityId prefabRootOf(EntityId id) const;
    // Puts the instance back to what `prefab` says, keeping the root's identity, name, transform
    // and place (see reapplyPrefab in SceneSerializer.hpp).
    Status revertToPrefab(EntityId root, const Json &prefab);
    // After the prefab `source` changed on disk: every other instance of it in this scene is put
    // back to `prefab` the same way. Returns how many were updated.
    Result<std::size_t> updatePrefabInstances(const std::string &source, const Json &prefab,
                                              EntityId except);
    // Forgets the prefab link of an instance; its entities stay as they are.
    void unpackPrefab(EntityId root);
    // Scene-wide settings (name, gravity, background): one undo step per change.
    void editSettings(const std::string &label, const std::function<void(SceneSettings &)> &edit);
    // Value is validated and clamped like any other property write. False when nothing was written.
    bool setProperty(EntityId id, std::size_t componentIndex, const std::string &property,
                     PropertyValue value);
    bool addComponent(EntityId id, const std::string &typeName);
    // Fails (naming the component that needs it) when another component depends on this one.
    Status removeComponent(EntityId id, std::size_t componentIndex);

    // Selected roots as a portable clipboard document (survives closing the scene).
    Json copySelection() const;
    // Instantiates a clipboard document; with `center`, the group's midpoint lands there, otherwise
    // the copies are offset slightly from the originals. Returns the new roots (selected).
    Result<std::vector<EntityId>> paste(const Json &clipboard, EntityId parent = {},
                                        std::optional<Vec2> center = std::nullopt);

    static constexpr const char *clipboardFormat = "yk.clipboard";

  private:
    struct Snapshot {
        Json scene;
        std::vector<EntityId> selection;
    };
    struct Step {
        std::string label;
        Snapshot state;          // What to restore to undo (or redo) this step.
        std::uint64_t version{}; // Document version that state corresponds to.
    };
    Snapshot capture() const;
    bool restore(const Snapshot &snapshot);
    void pruneSelection();
    std::string uniqueName(const std::string &base) const;
    void trimHistory();

    const ComponentRegistry *registry_;
    std::unique_ptr<Scene> scene_;
    std::string path_;
    std::vector<EntityId> selection_;
    bool open_{};
    std::string openLabel_;
    Snapshot before_;
    std::vector<Step> undo_, redo_;
    std::uint64_t version_{};
    std::uint64_t savedVersion_{};
    std::uint64_t nextVersion_{1};
    std::uint64_t revision_{};
};
} // namespace yk::editor
