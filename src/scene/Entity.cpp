#include "yk/scene/Entity.hpp"
#include "yk/core/Log.hpp"
#include "yk/scene/Scene.hpp"
#include <algorithm>

namespace yk {
Scene &Component::scene() const {
    return entity_->scene();
}
Entity::~Entity() = default;

bool Entity::lockedInHierarchy() const {
    for (const Entity *entity = this; entity; entity = entity->parent())
        if (entity->locked_)
            return true;
    return false;
}
bool Entity::hiddenInHierarchy() const {
    for (const Entity *entity = this; entity; entity = entity->parent())
        if (entity->editorHidden_)
            return true;
    return false;
}
bool Entity::activeInHierarchy() const {
    for (const Entity *node = this; node; node = node->parent())
        if (!node->active_)
            return false;
    return true;
}
Entity *Entity::parent() const {
    return parent_ ? scene_->find(parent_) : nullptr;
}
bool Entity::hasTag(std::string_view tag) const {
    return std::find(tags_.begin(), tags_.end(), tag) != tags_.end();
}
void Entity::addTag(std::string tag) {
    if (!tag.empty() && !hasTag(tag))
        tags_.push_back(std::move(tag));
}
void Entity::removeTag(std::string_view tag) {
    std::erase(tags_, tag);
}
void Entity::setTags(std::vector<std::string> tags) {
    tags_.clear();
    for (auto &tag : tags)
        addTag(std::move(tag));
}
Transform2D Entity::worldTransform() const {
    Transform2D result = local_;
    for (const Entity *node = parent(); node; node = node->parent())
        result = compose(node->local_, result);
    return result;
}
void Entity::setWorldTransform(const Transform2D &world) {
    const Entity *above = parent();
    local_ = above ? relativeTo(above->worldTransform(), world) : world;
}
void Entity::setWorldPosition(Vec2 position) {
    Transform2D world = worldTransform();
    world.position = position;
    setWorldTransform(world);
}

Component *Entity::findComponent(std::string_view typeName) const {
    for (const auto &component : components_)
        if (component->type().name == typeName)
            return component.get();
    return nullptr;
}
Component *Entity::attach(const ComponentType &type) {
    auto component = type.create();
    component->entity_ = this;
    component->type_ = &type;
    components_.push_back(std::move(component));
    scene_->touch();
    return components_.back().get();
}
Component *Entity::addComponent(std::string_view typeName, bool applyDefaults) {
    const ComponentType *type = scene_->registry().find(typeName);
    if (!type) {
        log(LogLevel::Error, "scene", "Unknown component type '" + std::string(typeName) + "'");
        return nullptr;
    }
    if (!type->allowMultiple)
        if (Component *existing = findComponent(type->name))
            return existing;
    for (const std::string &dependency : type->dependencies) {
        // An existing instance satisfies a dependency, even for types that allow several.
        if (!findComponent(dependency) && !addComponent(dependency, applyDefaults))
            return nullptr;
    }
    Component *component = attach(*type);
    if (applyDefaults && type->onAdded)
        type->onAdded(*this, *component);
    return component;
}
Component *Entity::addComponentOfClass(std::type_index index) {
    const ComponentType *type = scene_->registry().find(index);
    if (!type)
        throw std::logic_error(std::string("component class is not registered: ") + index.name());
    return addComponent(type->name);
}
std::string Entity::removalBlocker(const Component &component) const {
    const std::string &name = component.type().name;
    for (const auto &other : components_)
        if (other.get() != &component)
            for (const std::string &dependency : other->type().dependencies)
                if (dependency == name)
                    return other->type().name;
    return {};
}
bool Entity::removeComponent(Component *component) {
    const auto found = std::find_if(components_.begin(), components_.end(),
                                    [&](const auto &owned) { return owned.get() == component; });
    if (found == components_.end() || !removalBlocker(*component).empty())
        return false;
    components_.erase(found);
    scene_->touch();
    return true;
}

void Entity::captureRenderState(float snapDistance) {
    const Entity *owner = parent();
    const Transform2D world =
        owner && owner->renderValid_ ? compose(owner->renderCurrent_, local_) : worldTransform();
    capturedLocal_ = local_;
    if (!renderValid_ || renderSnap_) {
        renderPrevious_ = renderCurrent_ = world;
        renderValid_ = true;
        renderSnap_ = false;
        return;
    }
    renderPrevious_ = renderCurrent_;
    renderCurrent_ = world;
    // A jump too long for one tick of motion is a teleport the caller did not announce.
    if (distance(renderPrevious_.position, world.position) > snapDistance)
        renderPrevious_ = world;
}
bool Entity::renderFresh() const {
    for (const Entity *node = this; node; node = node->parent())
        if (!node->renderValid_ || !(node->local_ == node->capturedLocal_))
            return false;
    return true;
}
Transform2D Entity::renderTransform(float alpha) const {
    if (!renderFresh())
        return worldTransform();
    const float t = std::clamp(alpha, 0.0F, 1.0F);
    Transform2D result;
    result.position = lerp(renderPrevious_.position, renderCurrent_.position, t);
    result.scale = lerp(renderPrevious_.scale, renderCurrent_.scale, t);
    float turn =
        std::fmod(renderCurrent_.rotationDegrees - renderPrevious_.rotationDegrees, 360.0F);
    if (turn > 180.0F)
        turn -= 360.0F;
    else if (turn < -180.0F)
        turn += 360.0F;
    result.rotationDegrees = renderPrevious_.rotationDegrees + turn * t;
    return result;
}
} // namespace yk
