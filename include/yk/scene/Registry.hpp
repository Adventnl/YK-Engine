#pragma once
#include "yk/scene/Component.hpp"
#include "yk/scene/Property.hpp"
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace yk {
class Scene;
class Entity;

// What a component's validation is told about where it is being checked.
struct CheckContext {
    // A prefab is checked on its own, before it is wired to anything: a plate or a door in a prefab
    // has no targets yet, and that is not a mistake.
    bool prefab{};
    // Reports something that is wrong rather than merely doubtful (a rule that names an action that
    // does not exist). Checks that only know warnings ignore it; when it is empty (a check run
    // outside the validator) the sentence is added to the plain list like any other.
    std::function<void(const std::string &)> error;
};

// Everything the engine knows about one component class.
struct ComponentType {
    std::string name;
    std::string category{"General"};
    std::string description;
    std::type_index type{typeid(void)};
    std::function<std::unique_ptr<Component>()> create;
    std::vector<PropertyInfo> properties;
    std::vector<std::string>
        dependencies; // Component type names added automatically before this one.
    // Sensible starting values when a user (or code) adds the component to an entity; receives the
    // entity so it can configure dependencies (a plate turns its Collider into a trigger). Never
    // runs while loading saved data, so it cannot overwrite what was saved.
    std::function<void(Entity &, Component &)> onAdded;
    // Mistakes this component can be checked for in a saved scene or prefab (a plate with nothing
    // to stand on, a hinge on a body that cannot swing), each appended as a plain sentence naming
    // what to change. Run by validateProject and shown in the editor's Problems panel; never at run
    // time.
    std::function<void(const Entity &, const Component &, const CheckContext &,
                       std::vector<std::string> &)>
        check;
    bool allowMultiple{};
    UpdatePhase phase{UpdatePhase::Gameplay}; // When onFixedUpdate runs within a tick.
    bool hiddenInMenus{};                     // Not offered by the editor's Add Component menu.
    bool screenSpace{}; // Positioned in screen pixels, not the world (UI); no world gizmo.

    const PropertyInfo *find(std::string_view propertyName) const {
        for (const PropertyInfo &property : properties)
            if (property.name == propertyName)
                return &property;
        return nullptr;
    }
};

namespace detail {
template <class M> constexpr PropertyType propertyTypeOf() {
    if constexpr (std::is_same_v<M, bool>)
        return PropertyType::Bool;
    else if constexpr (std::is_enum_v<M>)
        return PropertyType::Enum;
    else if constexpr (std::is_integral_v<M>)
        return PropertyType::Int;
    else if constexpr (std::is_floating_point_v<M>)
        return PropertyType::Float;
    else if constexpr (std::is_same_v<M, std::string>)
        return PropertyType::String;
    else if constexpr (std::is_same_v<M, Vec2>)
        return PropertyType::Vec2;
    else if constexpr (std::is_same_v<M, Color>)
        return PropertyType::Color;
    else if constexpr (std::is_same_v<M, EntityId>)
        return PropertyType::EntityReference;
    else if constexpr (std::is_same_v<M, std::vector<EntityId>>)
        return PropertyType::EntityReferenceList;
    else if constexpr (std::is_same_v<M, std::vector<std::string>>)
        return PropertyType::StringList;
    else if constexpr (std::is_same_v<M, AssetRef>)
        return PropertyType::Asset;
    else if constexpr (std::is_same_v<M, Json>)
        return PropertyType::Json;
    else
        static_assert(sizeof(M) == 0, "unsupported component field type");
}
template <class M> PropertyValue toPropertyValue(const M &value) {
    if constexpr (std::is_enum_v<M>)
        return static_cast<std::int64_t>(static_cast<std::underlying_type_t<M>>(value));
    else if constexpr (std::is_same_v<M, bool>)
        return value;
    else if constexpr (std::is_integral_v<M>)
        return static_cast<std::int64_t>(value);
    else if constexpr (std::is_floating_point_v<M>)
        return static_cast<double>(value);
    else
        return value;
}
template <class M, class Alternative> bool takeAlternative(M &out, const PropertyValue &value) {
    if (const auto *held = std::get_if<Alternative>(&value)) {
        out = *held;
        return true;
    }
    return false;
}
template <class M> bool fromPropertyValue(M &out, const PropertyValue &value) {
    if constexpr (std::is_enum_v<M>) {
        const auto *held = std::get_if<std::int64_t>(&value);
        if (!held)
            return false;
        out = static_cast<M>(*held);
        return true;
    } else if constexpr (std::is_same_v<M, bool>) {
        return takeAlternative<M, bool>(out, value);
    } else if constexpr (std::is_integral_v<M>) {
        const auto *held = std::get_if<std::int64_t>(&value);
        if (!held || !std::in_range<M>(*held))
            return false;
        out = static_cast<M>(*held);
        return true;
    } else if constexpr (std::is_floating_point_v<M>) {
        if (const auto *real = std::get_if<double>(&value)) {
            out = static_cast<M>(*real);
            return true;
        }
        if (const auto *integer = std::get_if<std::int64_t>(&value)) {
            out = static_cast<M>(*integer);
            return true;
        }
        return false;
    } else {
        return takeAlternative<M, M>(out, value);
    }
}
} // namespace detail

// Chained configuration of the field just declared with TypeBuilder::field. Only valid until the
// next field() call on the same builder.
class FieldBuilder {
  public:
    explicit FieldBuilder(PropertyInfo &info) : info_(&info) {}
    FieldBuilder &tooltip(std::string text) {
        info_->tooltip = std::move(text);
        return *this;
    }
    FieldBuilder &range(double low, double high, double step = 0) {
        if (!(low <= high))
            throw std::logic_error("property '" + info_->name + "' has an inverted range");
        info_->hasRange = true;
        info_->minValue = low;
        info_->maxValue = high;
        info_->step = step;
        return *this;
    }
    FieldBuilder &options(std::vector<std::string> labels) {
        info_->options = std::move(labels);
        return *this;
    }
    FieldBuilder &asset(std::string kind) {
        info_->assetKind = std::move(kind);
        return *this;
    }
    FieldBuilder &hidden() {
        info_->hidden = true;
        return *this;
    }
    FieldBuilder &readOnly() {
        info_->readOnly = true;
        return *this;
    }
    FieldBuilder &size() {
        info_->isSize = true;
        return *this;
    }
    FieldBuilder &offset() {
        info_->isOffset = true;
        return *this;
    }
    FieldBuilder &layer() {
        info_->isLayer = true;
        return *this;
    }
    // A world-space shift of the entity (a door's open offset, a platform's travel). The editor
    // shows where the entity would end up and lets the user drag it.
    FieldBuilder &displacement() {
        info_->isDisplacement = true;
        return *this;
    }
    // A point in the entity's own space (a hinge's anchor). The editor draws it as a pin in the
    // scene view and lets the user drag it.
    FieldBuilder &pin() {
        info_->isPin = true;
        return *this;
    }
    // A string naming a set ("Player1") of the project's input map; the editor offers a picker.
    FieldBuilder &inputSet() {
        info_->isInputSet = true;
        return *this;
    }
    // A string naming an action ("Jump") of the project's input map; the editor offers a picker.
    FieldBuilder &inputAction() {
        info_->isInputAction = true;
        return *this;
    }
    // The string (or each string of the list) names a definition of this kind ("item", "quest",
    // "level", "faction", ...): picker in the editor, existence checked by validation.
    FieldBuilder &ref(std::string kind) {
        info_->refKind = std::move(kind);
        return *this;
    }
    FieldBuilder &multiline() {
        info_->multiline = true;
        return *this;
    }

  private:
    PropertyInfo *info_;
};

template <class T> class TypeBuilder {
  public:
    explicit TypeBuilder(ComponentType &type) : type_(&type) {}
    TypeBuilder &category(std::string value) {
        type_->category = std::move(value);
        return *this;
    }
    TypeBuilder &description(std::string value) {
        type_->description = std::move(value);
        return *this;
    }
    TypeBuilder &dependsOn(std::string typeName) {
        type_->dependencies.push_back(std::move(typeName));
        return *this;
    }
    TypeBuilder &allowMultiple() {
        type_->allowMultiple = true;
        return *this;
    }
    // The phase of the fixed tick in which this component's onFixedUpdate runs (see UpdatePhase).
    TypeBuilder &updatePhase(UpdatePhase phase) {
        type_->phase = phase;
        return *this;
    }
    TypeBuilder &onAdd(std::function<void(Entity &, T &)> hook) {
        type_->onAdded = [hook = std::move(hook)](Entity &entity, Component &component) {
            hook(entity, static_cast<T &>(component));
        };
        return *this;
    }
    // Registers the component's own validation (see ComponentType::check).
    TypeBuilder &check(std::function<void(const Entity &, const T &, const CheckContext &,
                                          std::vector<std::string> &)>
                           hook) {
        type_->check = [hook = std::move(hook)](const Entity &entity, const Component &component,
                                                const CheckContext &context,
                                                std::vector<std::string> &problems) {
            hook(entity, static_cast<const T &>(component), context, problems);
        };
        return *this;
    }
    TypeBuilder &hiddenInMenus() {
        type_->hiddenInMenus = true;
        return *this;
    }
    TypeBuilder &screenSpace() {
        type_->screenSpace = true;
        return *this;
    }
    // Declares a field that is not a plain member: its value is produced by `read` and taken by
    // `write` (which returns false for a value it cannot accept). For data a component keeps in a
    // richer form than one of the reflected types and exposes as JSON (a tile map, a rule list).
    template <class M>
    FieldBuilder computed(std::string name, std::function<M(const T &)> read,
                          std::function<bool(T &, const M &)> write) {
        if (type_->find(name))
            throw std::logic_error("component '" + type_->name + "' declares field '" + name +
                                   "' twice");
        PropertyInfo info;
        info.name = std::move(name);
        info.type = detail::propertyTypeOf<M>();
        info.get = [read](const Component &component) {
            return detail::toPropertyValue(read(static_cast<const T &>(component)));
        };
        info.set = [write](Component &component, const PropertyValue &value) {
            M converted{};
            return detail::fromPropertyValue(converted, value) &&
                   write(static_cast<T &>(component), converted);
        };
        const T reference{};
        info.defaultValue = detail::toPropertyValue(read(reference));
        type_->properties.push_back(std::move(info));
        return FieldBuilder(type_->properties.back());
    }
    // Declares an editable field. Members of base classes are accepted.
    template <class C, class M> FieldBuilder field(std::string name, M C::*member) {
        static_assert(std::is_base_of_v<C, T>, "field must belong to the component class");
        if (type_->find(name))
            throw std::logic_error("component '" + type_->name + "' declares field '" + name +
                                   "' twice");
        PropertyInfo info;
        info.name = std::move(name);
        info.type = detail::propertyTypeOf<M>();
        info.get = [member](const Component &component) {
            return detail::toPropertyValue(static_cast<const T &>(component).*member);
        };
        info.set = [member](Component &component, const PropertyValue &value) {
            return detail::fromPropertyValue(static_cast<T &>(component).*member, value);
        };
        const T reference{};
        info.defaultValue = detail::toPropertyValue(reference.*member);
        type_->properties.push_back(std::move(info));
        return FieldBuilder(type_->properties.back());
    }

  private:
    ComponentType *type_;
};

// A named way to create a ready-to-use entity ("Platform", "Pressure Plate", ...).
struct EntityTemplate {
    std::string name;
    std::string category;
    std::function<EntityId(Scene &, Vec2 worldPosition)> create;
};

// Explicitly owned catalogue of component classes and entity templates. The engine, gameplay
// library and game modules each contribute through a registerXxx(ComponentRegistry &) function; an
// application composes the registry it needs and hands it to scenes.
class ComponentRegistry {
  public:
    // Registers T under `name` and lets T::describe(TypeBuilder<T> &) declare its fields when
    // present. Registration mistakes are programming errors and throw std::logic_error.
    template <class T> TypeBuilder<T> add(std::string name) {
        static_assert(std::is_base_of_v<Component, T> && std::is_default_constructible_v<T>);
        if (find(name) || find(std::type_index(typeid(T))))
            throw std::logic_error("component '" + name + "' or its class is already registered");
        auto type = std::make_unique<ComponentType>();
        type->name = std::move(name);
        type->type = std::type_index(typeid(T));
        type->create = [] { return std::make_unique<T>(); };
        ComponentType &reference = *type;
        types_.push_back(std::move(type));
        TypeBuilder<T> builder(reference);
        if constexpr (requires { T::describe(builder); })
            T::describe(builder);
        for (const PropertyInfo &property : reference.properties)
            if (property.type == PropertyType::Enum && property.options.empty())
                throw std::logic_error("enum field '" + property.name + "' of '" + reference.name +
                                       "' needs options()");
        return builder;
    }
    template <class T> const ComponentType *find() const {
        return find(std::type_index(typeid(T)));
    }
    const ComponentType *find(std::string_view name) const;
    const ComponentType *find(std::type_index type) const;
    const std::vector<std::unique_ptr<ComponentType>> &types() const {
        return types_;
    }

    // Catalogues other modules hang on the registry (the rule catalog, ...): one instance per type,
    // created by the first extend<T>() and only read afterwards. Registration code extends; running
    // code looks up with extension<T>(), which is null when nothing registered one.
    template <class T> T &extend() {
        auto &slot = extensions_[std::type_index(typeid(T))];
        if (!slot)
            slot = std::make_shared<T>();
        return *static_cast<T *>(slot.get());
    }
    template <class T> const T *extension() const {
        const auto found = extensions_.find(std::type_index(typeid(T)));
        return found == extensions_.end() ? nullptr : static_cast<const T *>(found->second.get());
    }

    void addTemplate(EntityTemplate value);
    const std::vector<EntityTemplate> &templates() const {
        return templates_;
    }
    // Checks cross references (dependencies exist, no dependency cycles).
    Status validate() const;

  private:
    std::vector<std::unique_ptr<ComponentType>> types_;
    std::vector<EntityTemplate> templates_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> extensions_;
};
} // namespace yk
