#include "yk/scene/Scene.hpp"
#include <algorithm>

namespace yk {
Scene::Scene(const ComponentRegistry &registry, std::uint64_t idSeed)
    : registry_(&registry), random_(idSeed != 0 ? idSeed : std::random_device{}()) {}
Scene::~Scene() = default;

EntityId Scene::newId() {
    for (;;) {
        const EntityId id{random_()};
        if (id && !entities_.contains(id))
            return id;
    }
}
Entity *Scene::find(EntityId id) {
    const auto found = entities_.find(id);
    return found == entities_.end() ? nullptr : found->second.get();
}
const Entity *Scene::find(EntityId id) const {
    const auto found = entities_.find(id);
    return found == entities_.end() ? nullptr : found->second.get();
}
Entity *Scene::findByName(std::string_view name) const {
    for (const EntityId id : orderedIds())
        if (const Entity *entity = find(id); entity && entity->name() == name)
            return const_cast<Entity *>(entity);
    return nullptr;
}
Entity &Scene::createEntity(std::string name, EntityId parent) {
    return *createEntityWithId(newId(), std::move(name), parent).value();
}
Result<Entity *> Scene::createEntityWithId(EntityId id, std::string name, EntityId parent) {
    if (!id)
        return Error{"Entity id must not be null"};
    if (entities_.contains(id))
        return Error{"Duplicate entity id " + toString(id)};
    std::unique_ptr<Entity> entity(new Entity(*this, id, std::move(name)));
    Entity *raw = entity.get();
    entities_.emplace(id, std::move(entity));
    if (Entity *owner = parent ? find(parent) : nullptr) {
        raw->parent_ = parent;
        owner->children_.push_back(id);
    } else {
        roots_.push_back(id);
    }
    touch();
    return raw;
}
std::vector<EntityId> Scene::subtree(EntityId root) const {
    std::vector<EntityId> order;
    if (!find(root))
        return order;
    std::vector<EntityId> stack{root};
    while (!stack.empty()) {
        const EntityId id = stack.back();
        stack.pop_back();
        order.push_back(id);
        const auto &children = find(id)->childIds();
        for (auto child = children.rbegin(); child != children.rend(); ++child)
            stack.push_back(*child);
    }
    return order;
}
bool Scene::destroy(EntityId id) {
    Entity *entity = find(id);
    if (!entity)
        return false;
    const auto doomed = subtree(id);
    if (Entity *owner = entity->parent())
        std::erase(owner->children_, id);
    else
        std::erase(roots_, id);
    for (auto it = doomed.rbegin(); it != doomed.rend(); ++it)
        entities_.erase(*it);
    touch();
    return true;
}
bool Scene::isAncestor(EntityId ancestor, EntityId descendant) const {
    for (const Entity *node = find(descendant); node && node->parentId();
         node = find(node->parentId()))
        if (node->parentId() == ancestor)
            return true;
    return false;
}
Status Scene::setParent(EntityId childId, EntityId parentId, std::optional<std::size_t> index,
                        bool keepWorldTransform) {
    Entity *child = find(childId);
    if (!child)
        return Error{"Unknown entity " + toString(childId)};
    Entity *newParent = parentId ? find(parentId) : nullptr;
    if (parentId && !newParent)
        return Error{"Unknown parent entity " + toString(parentId)};
    if (parentId == childId || isAncestor(childId, parentId))
        return Error{"Cannot parent '" + child->name() + "' to itself or one of its descendants"};
    const Transform2D world = child->worldTransform();
    auto &oldSiblings = child->parent() ? child->parent()->children_ : roots_;
    std::erase(oldSiblings, childId);
    auto &siblings = newParent ? newParent->children_ : roots_;
    const std::size_t position = std::min(index.value_or(siblings.size()), siblings.size());
    siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(position), childId);
    child->parent_ = newParent ? parentId : EntityId{};
    if (keepWorldTransform)
        child->setWorldTransform(world);
    touch();
    return success();
}
Status Scene::setSiblingIndex(EntityId id, std::size_t index) {
    Entity *entity = find(id);
    if (!entity)
        return Error{"Unknown entity " + toString(id)};
    auto &siblings = entity->parent() ? entity->parent()->children_ : roots_;
    std::erase(siblings, id);
    siblings.insert(
        siblings.begin() + static_cast<std::ptrdiff_t>(std::min(index, siblings.size())), id);
    touch();
    return success();
}
const std::vector<EntityId> &Scene::orderedIds() const {
    if (orderRevision_ == revision_)
        return order_;
    order_.clear();
    order_.reserve(entities_.size());
    std::vector<EntityId> stack;
    for (auto root = roots_.rbegin(); root != roots_.rend(); ++root)
        stack.push_back(*root);
    while (!stack.empty()) {
        const EntityId id = stack.back();
        stack.pop_back();
        order_.push_back(id);
        const auto &children = find(id)->childIds();
        for (auto child = children.rbegin(); child != children.rend(); ++child)
            stack.push_back(*child);
    }
    orderRevision_ = revision_;
    return order_;
}
const std::vector<Entity *> &Scene::orderedEntities() {
    if (orderedEntitiesRevision_ == revision_)
        return orderedEntities_;
    orderedEntities_.clear();
    const auto &ids = orderedIds();
    orderedEntities_.reserve(ids.size());
    for (const EntityId id : ids)
        orderedEntities_.push_back(find(id));
    orderedEntitiesRevision_ = revision_;
    return orderedEntities_;
}
std::vector<EntityId> Scene::hierarchyOrder() const {
    return orderedIds();
}
void Scene::forEach(const std::function<void(Entity &)> &visit) {
    for (const EntityId id : hierarchyOrder())
        if (Entity *entity = find(id))
            visit(*entity);
}
void Scene::forEach(const std::function<void(const Entity &)> &visit) const {
    for (const EntityId id : hierarchyOrder())
        if (const Entity *entity = find(id))
            visit(*entity);
}
} // namespace yk
