#include "core/EditorDocument.hpp"
#include "yk/core/Log.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <unordered_set>

namespace yk::editor {
namespace {
Json vec2Json(Vec2 value) {
    Json array = Json::array();
    array.push(value.x);
    array.push(value.y);
    return array;
}
Vec2 vec2FromJson(const Json &json) {
    if (!json.isArray() || json.size() != 2)
        return {};
    return {static_cast<float>(json.at(0).asNumber()), static_cast<float>(json.at(1).asNumber())};
}

// Clears every reference to `gone` held by entities that remain, so deleting a door leaves no plate
// pointing at nothing.
void clearReferencesTo(Scene &scene, const std::unordered_set<EntityId> &gone) {
    scene.forEach([&](Entity &entity) {
        if (gone.contains(entity.id()))
            return;
        for (const auto &component : entity.components()) {
            for (const PropertyInfo &property : component->type().properties) {
                if (property.readOnly)
                    continue;
                if (property.type == PropertyType::EntityReference) {
                    if (gone.contains(std::get<EntityId>(property.get(*component))))
                        property.assign(*component, EntityId{});
                } else if (property.type == PropertyType::EntityReferenceList) {
                    auto list = std::get<std::vector<EntityId>>(property.get(*component));
                    const auto removed =
                        std::erase_if(list, [&](EntityId item) { return gone.contains(item); });
                    if (removed != 0)
                        property.assign(*component, std::move(list));
                }
            }
        }
    });
}

std::vector<EntityId> siblingsOf(const Scene &scene, const Entity &entity) {
    return entity.parentId() ? scene.find(entity.parentId())->childIds() : scene.roots();
}
} // namespace

EditorDocument::EditorDocument(const ComponentRegistry &registry, std::unique_ptr<Scene> scene,
                               std::string path)
    : registry_(&registry), scene_(std::move(scene)), path_(std::move(path)) {
    assert(scene_);
}

Scene &EditorDocument::edit() {
    assert(open_ && "Scene modified outside beginChange/endChange");
    return *scene_;
}

std::string EditorDocument::displayName() const {
    if (path_.empty())
        return "Untitled";
    return std::filesystem::path(path_).stem().string();
}

bool EditorDocument::isSelected(EntityId id) const {
    return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
}

void EditorDocument::select(EntityId id, SelectMode mode) {
    select(std::vector<EntityId>{id}, mode);
}

void EditorDocument::select(const std::vector<EntityId> &ids, SelectMode mode) {
    if (mode == SelectMode::Replace)
        selection_.clear();
    for (const EntityId id : ids) {
        if (!id || !scene_->find(id))
            continue;
        const auto found = std::find(selection_.begin(), selection_.end(), id);
        if (mode == SelectMode::Toggle) {
            if (found != selection_.end())
                selection_.erase(found);
            else
                selection_.push_back(id);
        } else if (found == selection_.end()) {
            selection_.push_back(id);
        } else {
            // Re-selecting makes it the primary selection.
            selection_.erase(found);
            selection_.push_back(id);
        }
    }
}

void EditorDocument::selectAll() {
    selection_ = scene_->hierarchyOrder();
}

std::vector<EntityId> EditorDocument::selectionRoots() const {
    std::vector<EntityId> roots;
    for (const EntityId id : scene_->hierarchyOrder()) {
        if (!isSelected(id))
            continue;
        bool nested = false;
        for (const EntityId other : selection_)
            if (other != id && scene_->isAncestor(other, id)) {
                nested = true;
                break;
            }
        if (!nested)
            roots.push_back(id);
    }
    return roots;
}

void EditorDocument::pruneSelection() {
    std::erase_if(selection_, [&](EntityId id) { return !scene_->find(id); });
}

EditorDocument::Snapshot EditorDocument::capture() const {
    return {sceneToJson(*scene_), selection_};
}

bool EditorDocument::restore(const Snapshot &snapshot) {
    auto scene = sceneFromJson(snapshot.scene, *registry_);
    if (!scene) {
        log(LogLevel::Error, "editor", "Could not restore scene state: " + scene.error());
        return false;
    }
    scene_ = std::move(scene.value());
    selection_ = snapshot.selection;
    pruneSelection();
    return true;
}

void EditorDocument::trimHistory() {
    if (undo_.size() > maxUndoSteps)
        undo_.erase(undo_.begin(),
                    undo_.begin() + static_cast<std::ptrdiff_t>(undo_.size() - maxUndoSteps));
}

void EditorDocument::beginChange(std::string label) {
    if (open_)
        return;
    open_ = true;
    openLabel_ = std::move(label);
    before_ = capture();
}

void EditorDocument::endChange() {
    if (!open_)
        return;
    open_ = false;
    if (sceneToJson(*scene_) == before_.scene) {
        before_ = {};
        return;
    }
    undo_.push_back({std::move(openLabel_), std::move(before_), version_});
    before_ = {};
    redo_.clear();
    version_ = nextVersion_++;
    ++revision_;
    trimHistory();
    pruneSelection();
}

void EditorDocument::cancelChange() {
    if (!open_)
        return;
    open_ = false;
    restore(before_);
    before_ = {};
    ++revision_;
}

void EditorDocument::change(const std::string &label, const std::function<void(Scene &)> &edit) {
    const bool owner = !open_;
    if (owner)
        beginChange(label);
    edit(*scene_);
    if (owner)
        endChange();
}

bool EditorDocument::undo() {
    if (open_ || undo_.empty())
        return false;
    Step step = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back({step.label, capture(), version_});
    if (!restore(step.state)) {
        redo_.pop_back();
        undo_.push_back(std::move(step));
        return false;
    }
    version_ = step.version;
    ++revision_;
    return true;
}

bool EditorDocument::redo() {
    if (open_ || redo_.empty())
        return false;
    Step step = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back({step.label, capture(), version_});
    if (!restore(step.state)) {
        undo_.pop_back();
        redo_.push_back(std::move(step));
        return false;
    }
    version_ = step.version;
    ++revision_;
    return true;
}

std::string EditorDocument::uniqueName(const std::string &base) const {
    std::unordered_set<std::string> used;
    scene_->forEach([&](const Entity &entity) { used.insert(entity.name()); });
    if (!used.contains(base))
        return base;
    std::string stem = base;
    // "Platform (3)" continues the numbering of "Platform".
    if (stem.size() > 4 && stem.back() == ')') {
        const auto open = stem.rfind(" (");
        if (open != std::string::npos && open + 3 < stem.size() &&
            std::all_of(stem.begin() + static_cast<std::ptrdiff_t>(open) + 2, stem.end() - 1,
                        [](unsigned char c) { return std::isdigit(c) != 0; }))
            stem.erase(open);
    }
    for (int n = 1;; ++n) {
        std::string candidate = stem + " (" + std::to_string(n) + ")";
        if (!used.contains(candidate))
            return candidate;
    }
}

EntityId EditorDocument::createEntity(const std::string &name, EntityId parent,
                                      std::optional<Vec2> worldPosition) {
    EntityId id;
    change("Create Entity", [&](Scene &scene) {
        Entity &entity = scene.createEntity(uniqueName(name), parent);
        if (worldPosition)
            entity.setWorldPosition(*worldPosition);
        id = entity.id();
    });
    select(id);
    return id;
}

Result<EntityId> EditorDocument::createFromTemplate(const EntityTemplate &entityTemplate,
                                                    Vec2 worldPosition, EntityId parent) {
    if (!entityTemplate.create)
        return Error{"Template '" + entityTemplate.name + "' cannot create entities"};
    EntityId id;
    change("Create " + entityTemplate.name, [&](Scene &scene) {
        id = entityTemplate.create(scene, worldPosition);
        Entity *entity = scene.find(id);
        if (!entity)
            return;
        if (parent)
            scene.setParent(id, parent, {}, true);
        const std::string base = entity->name();
        entity->setName({});
        entity->setName(uniqueName(base));
    });
    if (!scene_->find(id))
        return Error{"Template '" + entityTemplate.name + "' did not create an entity"};
    select(id);
    return id;
}

Result<EntityId> EditorDocument::instantiatePrefab(const Json &prefab, Vec2 worldPosition,
                                                   EntityId parent, const std::string &source) {
    std::optional<Result<EntityId>> outcome;
    change("Add Prefab", [&](Scene &scene) {
        outcome = instantiateSubtree(scene, prefab, parent, worldPosition, false, source);
        if (!*outcome)
            return;
        Entity &root = *scene.find(outcome->value());
        const std::string base = root.name();
        root.setName({});
        root.setName(uniqueName(base));
    });
    if (outcome && *outcome)
        select(outcome->value());
    return *outcome;
}

std::vector<EntityId> EditorDocument::duplicateSelection(Vec2 offset) {
    const std::vector<EntityId> roots = selectionRoots();
    std::vector<EntityId> copies;
    change("Duplicate", [&](Scene &scene) {
        std::vector<Json> documents;
        std::vector<std::size_t> indices;
        std::vector<EntityId> parents;
        for (const EntityId original : roots) {
            const Entity *source = scene.find(original);
            const auto siblings = siblingsOf(scene, *source);
            const auto position = std::find(siblings.begin(), siblings.end(), original);
            indices.push_back(static_cast<std::size_t>(std::distance(siblings.begin(), position)) +
                              1);
            parents.push_back(source->parentId());
            documents.push_back(subtreeToJson(scene, original));
        }
        std::vector<PrefabPlacement> placements;
        for (std::size_t i = 0; i < documents.size(); ++i)
            placements.push_back({&documents[i], parents[i]});
        auto made = instantiateSubtrees(scene, placements, true);
        if (!made) {
            log(LogLevel::Error, "editor", "Duplicate failed: " + made.error());
            return;
        }
        copies = made.value();
        for (std::size_t i = 0; i < copies.size(); ++i) {
            Entity &entity = *scene.find(copies[i]);
            const std::string base = entity.name();
            entity.setName({});
            entity.setName(uniqueName(base));
            entity.setWorldPosition(entity.worldPosition() + offset);
            scene.setParent(copies[i], parents[i], indices[i]);
        }
    });
    if (!copies.empty())
        select(copies);
    return copies;
}

void EditorDocument::deleteSelection() {
    const std::vector<EntityId> roots = selectionRoots();
    if (roots.empty())
        return;
    change(roots.size() == 1 ? "Delete " + scene_->find(roots.front())->name() : "Delete Entities",
           [&](Scene &scene) {
               std::unordered_set<EntityId> gone;
               for (const EntityId root : roots)
                   for (const EntityId id : scene.subtree(root))
                       gone.insert(id);
               clearReferencesTo(scene, gone);
               for (const EntityId root : roots)
                   scene.destroy(root);
           });
    pruneSelection();
}

Status EditorDocument::reparent(EntityId child, EntityId newParent,
                                std::optional<std::size_t> index) {
    std::optional<Status> outcome;
    change("Reparent",
           [&](Scene &scene) { outcome = scene.setParent(child, newParent, index, true); });
    return outcome ? *outcome : Status(Error{"Reparent did not run"});
}

Status EditorDocument::moveAmongSiblings(EntityId id, int steps) {
    const Entity *entity = scene_->find(id);
    if (!entity)
        return Error{"Unknown entity"};
    const auto siblings = siblingsOf(*scene_, *entity);
    const auto position = static_cast<std::ptrdiff_t>(
        std::distance(siblings.begin(), std::find(siblings.begin(), siblings.end(), id)));
    const auto target = std::clamp<std::ptrdiff_t>(
        position + steps, 0, static_cast<std::ptrdiff_t>(siblings.size()) - 1);
    std::optional<Status> outcome;
    change("Reorder", [&](Scene &scene) {
        outcome = scene.setSiblingIndex(id, static_cast<std::size_t>(target));
    });
    return outcome ? *outcome : Status(Error{"Reorder did not run"});
}

void EditorDocument::rename(EntityId id, const std::string &name) {
    change("Rename", [&](Scene &scene) {
        if (Entity *entity = scene.find(id))
            entity->setName(name);
    });
}

void EditorDocument::setEntityActive(EntityId id, bool active) {
    change(active ? "Activate" : "Deactivate", [&](Scene &scene) {
        if (Entity *entity = scene.find(id))
            entity->setActive(active);
    });
}

bool EditorDocument::setProperty(EntityId id, std::size_t componentIndex,
                                 const std::string &property, PropertyValue value) {
    bool written = false;
    change("Edit " + prettifyName(property), [&](Scene &scene) {
        Entity *entity = scene.find(id);
        if (!entity || componentIndex >= entity->components().size())
            return;
        Component &component = *entity->components()[componentIndex];
        if (const PropertyInfo *info = component.type().find(property))
            written = info->assign(component, std::move(value));
    });
    return written;
}

bool EditorDocument::addComponent(EntityId id, const std::string &typeName) {
    bool added = false;
    change("Add " + typeName, [&](Scene &scene) {
        if (Entity *entity = scene.find(id))
            added = entity->addComponent(typeName, true) != nullptr;
    });
    return added;
}

Status EditorDocument::removeComponent(EntityId id, std::size_t componentIndex) {
    const Entity *entity = scene_->find(id);
    if (!entity || componentIndex >= entity->components().size())
        return Error{"Unknown component"};
    const Component &component = *entity->components()[componentIndex];
    if (const std::string blocker = entity->removalBlocker(component); !blocker.empty())
        return Error{"'" + component.type().name + "' is required by '" + blocker + "'"};
    const std::string name = component.type().name;
    change("Remove " + name, [&](Scene &scene) {
        Entity *target = scene.find(id);
        if (target && componentIndex < target->components().size())
            target->removeComponent(target->components()[componentIndex].get());
    });
    return success();
}

Json EditorDocument::copySelection() const {
    Json document = Json::object();
    document.set("format", clipboardFormat);
    document.set("version", 1);
    Json items = Json::array();
    for (const EntityId root : selectionRoots()) {
        Json item = Json::object();
        item.set("world", vec2Json(scene_->find(root)->worldPosition()));
        item.set("prefab", subtreeToJson(*scene_, root));
        items.push(std::move(item));
    }
    document.set("items", items);
    return document;
}

Result<std::vector<EntityId>> EditorDocument::paste(const Json &clipboard, EntityId parent,
                                                    std::optional<Vec2> center) {
    if (!clipboard.isObject() || clipboard.get("format").asString() != clipboardFormat)
        return Error{"The clipboard does not hold copied entities"};
    const Json &items = clipboard.get("items");
    if (!items.isArray() || items.size() == 0)
        return Error{"The clipboard is empty"};
    std::vector<EntityId> created;
    std::string failure;
    change("Paste", [&](Scene &scene) {
        std::vector<PrefabPlacement> placements;
        std::vector<Vec2> original;
        for (const Json &item : items.items()) {
            placements.push_back({&item.get("prefab"), parent});
            original.push_back(vec2FromJson(item.get("world")));
        }
        auto made = instantiateSubtrees(scene, placements, true);
        if (!made) {
            failure = made.error();
            return;
        }
        created = made.value();
        Vec2 shift{0.5F, 0.5F};
        if (center) {
            Vec2 middle{};
            for (const Vec2 point : original)
                middle += point;
            shift = *center - middle / static_cast<float>(original.size());
        }
        for (std::size_t i = 0; i < created.size(); ++i) {
            Entity &entity = *scene.find(created[i]);
            const std::string base = entity.name();
            entity.setName({});
            entity.setName(uniqueName(base));
            entity.setWorldPosition(original[i] + shift);
        }
    });
    if (!failure.empty())
        return Error{failure};
    select(created);
    return created;
}
} // namespace yk::editor
