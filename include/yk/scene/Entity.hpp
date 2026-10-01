#pragma once
#include "yk/core/Math.hpp"
#include "yk/scene/Component.hpp"
#include "yk/scene/EntityId.hpp"
#include "yk/scene/Registry.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace yk {
class Scene;

// A named node in the scene hierarchy carrying a local transform, tags and components. Entities
// are created and destroyed through Scene; pointers and references stay valid until the entity is
// destroyed. Long-lived references between entities must use EntityId.
class Entity {
  public:
    Entity(const Entity &) = delete;
    Entity &operator=(const Entity &) = delete;
    ~Entity();

    EntityId id() const {
        return id_;
    }
    Scene &scene() const {
        return *scene_;
    }
    const std::string &name() const {
        return name_;
    }
    void setName(std::string name) {
        name_ = std::move(name);
    }
    // An inactive entity, and everything below it, is invisible to the runtime.
    bool active() const {
        return active_;
    }
    void setActive(bool active) {
        active_ = active;
    }
    bool activeInHierarchy() const;
    // An editor hint saved with the scene: a locked entity (and everything below it) cannot be
    // picked or moved in the scene view, so a huge backdrop does not swallow every click. The
    // runtime ignores it.
    bool locked() const {
        return locked_;
    }
    void setLocked(bool locked) {
        locked_ = locked;
    }
    bool lockedInHierarchy() const;
    // Another editor hint saved with the scene: an entity hidden in the editor (and everything
    // below it) is not drawn or picked in the scene view. The running game still shows it.
    bool editorHidden() const {
        return editorHidden_;
    }
    void setEditorHidden(bool hidden) {
        editorHidden_ = hidden;
    }
    bool hiddenInHierarchy() const;

    // The project-relative prefab this entity was created from. It is set on the root of an
    // instance and empty everywhere else. The editor uses it to show, revert and apply an instance
    // and the validator checks it; the runtime ignores it.
    const std::string &prefabSource() const {
        return prefabSource_;
    }
    void setPrefabSource(std::string path) {
        prefabSource_ = std::move(path);
    }

    const std::vector<std::string> &tags() const {
        return tags_;
    }
    bool hasTag(std::string_view tag) const;
    void addTag(std::string tag);
    void removeTag(std::string_view tag);
    void setTags(std::vector<std::string> tags);

    // Local transform relative to the parent (or the world for roots). +X right, +Y down, meters.
    Transform2D &transform() {
        return local_;
    }
    const Transform2D &transform() const {
        return local_;
    }
    Transform2D worldTransform() const;
    void setWorldTransform(const Transform2D &world);
    Vec2 worldPosition() const {
        return worldTransform().position;
    }
    void setWorldPosition(Vec2 position);

    EntityId parentId() const {
        return parent_;
    }
    Entity *parent() const;
    const std::vector<EntityId> &childIds() const {
        return children_;
    }

    // ---- Render interpolation (set by the running game only; never saved or edited) -----------
    // GameRuntime snapshots every entity's world transform at the end of each fixed tick, so a
    // display that refreshes faster than the simulation can blend between the last two ticks. An
    // entity that has no snapshot, or whose transform (or an ancestor's) was changed after it
    // because something moved it between ticks, is drawn where it really is.
    void captureRenderState(float snapDistance);
    // The next capture puts the entity where it is now without blending: a teleport.
    void snapRenderState() {
        renderSnap_ = true;
    }
    void clearRenderState() {
        renderValid_ = false;
        renderSnap_ = false;
    }
    bool hasRenderState() const {
        return renderValid_;
    }
    // World transform `alpha` (0..1) of the way from the previous tick to the current one.
    Transform2D renderTransform(float alpha) const;
    Vec2 renderPosition(float alpha) const {
        return renderTransform(alpha).position;
    }

    const std::vector<std::unique_ptr<Component>> &components() const {
        return components_;
    }
    // Adds a component by registered type name, first adding its dependencies. Returns the existing
    // instance when the type forbids duplicates. Null (with a logged reason) for unknown types.
    // `applyDefaults` runs the types' onAdd hooks; loaders pass false so saved values win.
    Component *addComponent(std::string_view typeName, bool applyDefaults = true);
    Component *findComponent(std::string_view typeName) const;
    // Empty when the component may be removed; otherwise names the component that requires it.
    std::string removalBlocker(const Component &component) const;
    bool removeComponent(Component *component);

    // Typed convenience for addComponent. Throws std::logic_error if T was never registered.
    template <class T> T &add() {
        return *static_cast<T *>(addComponentOfClass(std::type_index(typeid(T))));
    }
    template <class T> T *get() const {
        for (const auto &component : components_)
            if (T *match = dynamic_cast<T *>(component.get()))
                return match;
        return nullptr;
    }
    template <class T> bool has() const {
        return get<T>() != nullptr;
    }
    template <class T> std::vector<T *> getAll() const {
        std::vector<T *> matches;
        for (const auto &component : components_)
            if (T *match = dynamic_cast<T *>(component.get()))
                matches.push_back(match);
        return matches;
    }

  private:
    friend class Scene;
    Entity(Scene &scene, EntityId id, std::string name)
        : scene_(&scene), id_(id), name_(std::move(name)) {}
    Component *attach(const ComponentType &type);
    Component *addComponentOfClass(std::type_index type);

    Scene *scene_;
    EntityId id_;
    std::string name_;
    bool active_{true};
    bool locked_{false};
    bool editorHidden_{false};
    std::string prefabSource_;
    std::vector<std::string> tags_;
    Transform2D local_;
    EntityId parent_{};
    std::vector<EntityId> children_;
    std::vector<std::unique_ptr<Component>> components_;
    bool renderFresh() const;
    Transform2D renderPrevious_, renderCurrent_, capturedLocal_;
    bool renderValid_{false};
    bool renderSnap_{false};
};
} // namespace yk
