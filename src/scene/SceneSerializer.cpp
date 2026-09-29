#include "yk/scene/SceneSerializer.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace yk {
namespace {
Json vec2Json(Vec2 value) {
    Json array = Json::array();
    array.push(value.x);
    array.push(value.y);
    return array;
}
std::optional<Vec2> vec2FromJson(const Json &json) {
    if (!json.isArray() || json.size() != 2 || !json.at(0).isNumber() || !json.at(1).isNumber())
        return std::nullopt;
    return Vec2{static_cast<float>(json.at(0).asNumber()),
                static_cast<float>(json.at(1).asNumber())};
}

Json entityRecord(const Entity &entity) {
    Json json = Json::object();
    json.set("id", toString(entity.id()));
    json.set("name", entity.name());
    if (entity.parentId())
        json.set("parent", toString(entity.parentId()));
    if (!entity.active())
        json.set("active", false);
    if (!entity.tags().empty()) {
        Json tags = Json::array();
        for (const std::string &tag : entity.tags())
            tags.push(tag);
        json.set("tags", tags);
    }
    Json transform = Json::object();
    transform.set("position", vec2Json(entity.transform().position));
    transform.set("rotation", entity.transform().rotationDegrees);
    transform.set("scale", vec2Json(entity.transform().scale));
    json.set("transform", transform);
    Json components = Json::array();
    for (const auto &component : entity.components()) {
        Json record = Json::object();
        record.set("type", component->type().name);
        if (!component->enabled)
            record.set("enabled", false);
        Json properties = Json::object();
        for (const PropertyInfo &property : component->type().properties)
            if (!property.readOnly)
                properties.set(property.name, propertyToJson(property, property.get(*component)));
        record.set("properties", properties);
        components.push(record);
    }
    json.set("components", components);
    return json;
}

Result<Vec2> requireVec2(const Json &json, const char *what, const std::string &context) {
    const auto value = vec2FromJson(json);
    if (!value || !finite(*value))
        return Error{context + ": " + what + " must be [x, y]"};
    return *value;
}

// Fills `entity` (already created) from its JSON record. `context` names the entity for errors.
Status applyEntityRecord(Entity &entity, const Json &record, const std::string &context) {
    if (const Json *active = record.find("active")) {
        if (!active->isBool())
            return Error{context + ": 'active' must be a boolean"};
        entity.setActive(active->asBool());
    }
    if (const Json *tags = record.find("tags")) {
        if (!tags->isArray())
            return Error{context + ": 'tags' must be an array of strings"};
        for (const Json &tag : tags->items()) {
            if (!tag.isString())
                return Error{context + ": 'tags' must be an array of strings"};
            entity.addTag(tag.asString());
        }
    }
    if (const Json *transform = record.find("transform")) {
        if (!transform->isObject())
            return Error{context + ": 'transform' must be an object"};
        if (const Json *position = transform->find("position")) {
            auto value = requireVec2(*position, "transform.position", context);
            if (!value)
                return Error{value.error()};
            entity.transform().position = value.value();
        }
        if (const Json *scale = transform->find("scale")) {
            auto value = requireVec2(*scale, "transform.scale", context);
            if (!value)
                return Error{value.error()};
            entity.transform().scale = value.value();
        }
        if (const Json *rotation = transform->find("rotation")) {
            if (!rotation->isNumber() || !std::isfinite(rotation->asNumber()))
                return Error{context + ": transform.rotation must be a number"};
            entity.transform().rotationDegrees = static_cast<float>(rotation->asNumber());
        }
    }
    const Json &components = record.get("components");
    if (record.contains("components") && !components.isArray())
        return Error{context + ": 'components' must be an array"};
    for (const Json &item : components.items()) {
        const std::string &typeName = item.get("type").asString();
        const ComponentType *type = entity.scene().registry().find(typeName);
        if (!type)
            return Error{context + ": unknown component type '" + typeName + "'"};
        Component *component = nullptr;
        if (type->allowMultiple || !entity.findComponent(typeName))
            component = entity.addComponent(typeName);
        else
            component =
                entity.findComponent(typeName); // Dependency added it earlier; adopt its values.
        if (!component)
            return Error{context + ": cannot add component '" + typeName + "'"};
        if (const Json *enabled = item.find("enabled"))
            component->enabled = enabled->asBool(true);
        const Json &properties = item.get("properties");
        for (std::size_t i = 0; i < properties.size(); ++i) {
            const PropertyInfo *property = type->find(properties.keyAt(i));
            if (!property || property->readOnly) {
                log(LogLevel::Warning, "scene",
                    context + ": ignoring unknown property '" + properties.keyAt(i) + "' on '" +
                        typeName + "'");
                continue;
            }
            auto value = propertyFromJson(*property, properties.valueAt(i));
            if (!value)
                return Error{context + ": " + typeName + ": " + value.error()};
            if (!property->assign(*component, std::move(value.value())))
                return Error{context + ": " + typeName + ": value for '" + property->name +
                             "' is not acceptable"};
        }
    }
    return success();
}

std::string describe(const Json &record, std::size_t index) {
    const std::string &name = record.get("name").asString();
    return "entity #" + std::to_string(index) + (name.empty() ? "" : " '" + name + "'");
}

// Creates every entity in `entities`. With preserveIds the file's ids are kept; otherwise each gets
// a fresh id and `remap` records old -> new. Entities whose parent is absent from the set attach to
// `attachRoot`. Returns the created ids in file order.
Result<std::vector<EntityId>> populate(Scene &scene, const Json &entities, bool preserveIds,
                                       EntityId attachRoot,
                                       std::unordered_map<EntityId, EntityId> &remap) {
    if (!entities.isArray())
        return Error{"'entities' must be an array"};
    struct Pending {
        EntityId id;
        EntityId oldParent;
        Entity *entity;
        const Json *record;
        std::string context;
    };
    std::vector<Pending> pending;
    std::vector<EntityId> created;
    const auto rollback = [&] {
        for (const EntityId id : created)
            scene.destroy(id);
    };
    std::unordered_set<EntityId> fileIds;
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const Json &record = entities.at(i);
        const std::string context = describe(record, i);
        if (!record.isObject()) {
            rollback();
            return Error{context + " must be an object"};
        }
        const auto oldId =
            record.get("id").isString() ? parseEntityId(record.get("id").asString()) : std::nullopt;
        if (!oldId || !*oldId || !fileIds.insert(*oldId).second) {
            rollback();
            return Error{context + " has a missing, malformed or duplicate id"};
        }
        EntityId oldParent;
        if (const Json *parent = record.find("parent")) {
            const auto parsed =
                parent->isString() ? parseEntityId(parent->asString()) : std::nullopt;
            if (!parsed) {
                rollback();
                return Error{context + " has a malformed parent id"};
            }
            oldParent = *parsed;
        }
        const std::string name = record.contains("name") ? record.get("name").asString() : "Entity";
        auto entity = scene.createEntityWithId(preserveIds ? *oldId : scene.newId(), name, {});
        if (!entity) {
            rollback();
            return Error{context + ": " + entity.error()};
        }
        created.push_back(entity.value()->id());
        remap.emplace(*oldId, entity.value()->id());
        pending.push_back({entity.value()->id(), oldParent, entity.value(), &record, context});
    }
    // Second pass: parent links in file order, so siblings keep their authored order.
    for (const Pending &item : pending) {
        EntityId parent = attachRoot;
        if (item.oldParent) {
            const auto mapped = remap.find(item.oldParent);
            if (mapped == remap.end() && preserveIds) {
                rollback();
                return Error{item.context + " refers to a parent that does not exist"};
            }
            if (mapped != remap.end())
                parent = mapped->second;
        }
        if (auto status = scene.setParent(item.id, parent); !status) {
            rollback();
            return Error{item.context + ": " + status.error()};
        }
    }
    for (const Pending &item : pending) {
        if (auto status = applyEntityRecord(*item.entity, *item.record, item.context); !status) {
            rollback();
            return Error{status.error()};
        }
    }
    return created;
}

// Rewrites entity references inside the created entities through `remap` (unknown targets clear).
void remapReferences(Scene &scene, const std::vector<EntityId> &created,
                     const std::unordered_map<EntityId, EntityId> &remap) {
    const auto translate = [&](EntityId old) {
        const auto found = remap.find(old);
        return found == remap.end() ? EntityId{} : found->second;
    };
    for (const EntityId id : created) {
        Entity *entity = scene.find(id);
        for (const auto &component : entity->components()) {
            for (const PropertyInfo &property : component->type().properties) {
                if (property.readOnly)
                    continue;
                if (property.type == PropertyType::EntityReference) {
                    const auto old = std::get<EntityId>(property.get(*component));
                    property.assign(*component, translate(old));
                } else if (property.type == PropertyType::EntityReferenceList) {
                    auto list = std::get<std::vector<EntityId>>(property.get(*component));
                    for (EntityId &item : list)
                        item = translate(item);
                    std::erase(list, EntityId{});
                    property.assign(*component, std::move(list));
                }
            }
        }
    }
}

void warnAboutDanglingReferences(const Scene &scene) {
    scene.forEach([&](const Entity &entity) {
        for (const auto &component : entity.components()) {
            for (const PropertyInfo &property : component->type().properties) {
                if (property.readOnly)
                    continue;
                std::vector<EntityId> targets;
                if (property.type == PropertyType::EntityReference)
                    targets.push_back(std::get<EntityId>(property.get(*component)));
                else if (property.type == PropertyType::EntityReferenceList)
                    targets = std::get<std::vector<EntityId>>(property.get(*component));
                for (const EntityId target : targets)
                    if (target && !scene.find(target))
                        log(LogLevel::Warning, "scene",
                            "'" + entity.name() + "' " + component->type().name + "." +
                                property.name + " refers to a missing entity " + toString(target));
            }
        }
    });
}

Status checkHeader(const Json &document, const char *expectedFormat) {
    if (!document.isObject())
        return Error{"Document must be a JSON object"};
    if (document.get("format").asString() != expectedFormat)
        return Error{std::string("Not a ") + expectedFormat + " document"};
    const Json &version = document.get("version");
    if (!version.isNumber() || version.asInt() < 1)
        return Error{"Missing or invalid format version"};
    if (version.asInt() > sceneFormatVersion)
        return Error{"Document version " + std::to_string(version.asInt()) +
                     " is newer than this build supports (" + std::to_string(sceneFormatVersion) +
                     ")"};
    return success();
}
} // namespace

Json sceneToJson(const Scene &scene) {
    Json document = Json::object();
    document.set("format", sceneFormatName);
    document.set("version", sceneFormatVersion);
    Json settings = Json::object();
    settings.set("name", scene.settings.name);
    settings.set("gravity", vec2Json(scene.settings.gravity));
    settings.set("background", formatColor(scene.settings.background));
    document.set("settings", settings);
    Json entities = Json::array();
    scene.forEach([&](const Entity &entity) { entities.push(entityRecord(entity)); });
    document.set("entities", entities);
    return document;
}

Result<std::unique_ptr<Scene>>
sceneFromJson(const Json &document, const ComponentRegistry &registry, std::uint64_t idSeed) {
    if (auto status = checkHeader(document, sceneFormatName); !status)
        return Error{status.error()};
    auto scene = std::make_unique<Scene>(registry, idSeed);
    if (const Json *settings = document.find("settings")) {
        if (!settings->isObject())
            return Error{"'settings' must be an object"};
        if (settings->contains("name"))
            scene->settings.name = settings->get("name").asString();
        if (const Json *gravity = settings->find("gravity")) {
            auto value = requireVec2(*gravity, "settings.gravity", "scene");
            if (!value)
                return Error{value.error()};
            scene->settings.gravity = value.value();
        }
        if (const Json *background = settings->find("background")) {
            const auto color =
                background->isString() ? parseColor(background->asString()) : std::nullopt;
            if (!color)
                return Error{"scene: settings.background must be a color like \"#rrggbb\""};
            scene->settings.background = *color;
        }
    }
    std::unordered_map<EntityId, EntityId> remap;
    auto created = populate(*scene, document.get("entities"), true, {}, remap);
    if (!created)
        return Error{created.error()};
    warnAboutDanglingReferences(*scene);
    return scene;
}

Result<std::unique_ptr<Scene>> loadScene(const std::filesystem::path &path,
                                         const ComponentRegistry &registry) {
    auto text = readTextFile(path);
    if (!text)
        return Error{text.error()};
    auto document = Json::parse(text.value());
    if (!document)
        return Error{path.string() + ": " + document.error()};
    auto scene = sceneFromJson(document.value(), registry);
    if (!scene)
        return Error{path.string() + ": " + scene.error()};
    return scene;
}

Status saveScene(const Scene &scene, const std::filesystem::path &path) {
    return writeTextFileAtomic(path, sceneToJson(scene).dump(2) + "\n");
}

Json subtreeToJson(const Scene &scene, EntityId root) {
    Json document = Json::object();
    document.set("format", prefabFormatName);
    document.set("version", sceneFormatVersion);
    document.set("root", toString(root));
    Json entities = Json::array();
    for (const EntityId id : scene.subtree(root)) {
        Json record = entityRecord(*scene.find(id));
        if (id == root)
            record.erase("parent"); // The root of a prefab has no parent inside it.
        entities.push(std::move(record));
    }
    document.set("entities", entities);
    return document;
}

Result<EntityId> instantiateSubtree(Scene &scene, const Json &prefab, EntityId parent,
                                    std::optional<Vec2> worldPosition) {
    if (auto status = checkHeader(prefab, prefabFormatName); !status)
        return Error{status.error()};
    const auto rootId =
        prefab.get("root").isString() ? parseEntityId(prefab.get("root").asString()) : std::nullopt;
    if (!rootId || !*rootId)
        return Error{"Prefab has no valid 'root'"};
    std::unordered_map<EntityId, EntityId> remap;
    auto created = populate(scene, prefab.get("entities"), false, parent, remap);
    if (!created)
        return Error{created.error()};
    const auto root = remap.find(*rootId);
    if (root == remap.end()) {
        for (const EntityId id : created.value())
            scene.destroy(id);
        return Error{"Prefab root is not among its entities"};
    }
    remapReferences(scene, created.value(), remap);
    Entity *rootEntity = scene.find(root->second);
    if (rootEntity->parentId() != parent &&
        parent) // Root records never name a parent; attach explicitly.
        scene.setParent(root->second, parent);
    if (worldPosition)
        rootEntity->setWorldPosition(*worldPosition);
    return root->second;
}

Result<Json> loadPrefabDocument(const std::filesystem::path &path) {
    auto text = readTextFile(path);
    if (!text)
        return Error{text.error()};
    auto document = Json::parse(text.value());
    if (!document)
        return Error{path.string() + ": " + document.error()};
    if (auto status = checkHeader(document.value(), prefabFormatName); !status)
        return Error{path.string() + ": " + status.error()};
    return document;
}

Status savePrefab(const Scene &scene, EntityId root, const std::filesystem::path &path) {
    if (!scene.find(root))
        return Error{"Unknown entity " + toString(root)};
    return writeTextFileAtomic(path, subtreeToJson(scene, root).dump(2) + "\n");
}
} // namespace yk
