#include "yk/scene/Registry.hpp"
#include <algorithm>

namespace yk {
const char *updatePhaseName(UpdatePhase phase) {
    switch (phase) {
    case UpdatePhase::Clock:
        return "Clock";
    case UpdatePhase::PreUpdate:
        return "PreUpdate";
    case UpdatePhase::Decision:
        return "Decision";
    case UpdatePhase::Gameplay:
        return "Gameplay";
    case UpdatePhase::Steering:
        return "Steering";
    case UpdatePhase::Perception:
        return "Perception";
    case UpdatePhase::PostSimulation:
        return "PostSimulation";
    }
    return "Gameplay";
}
const ComponentType *ComponentRegistry::find(std::string_view name) const {
    for (const auto &type : types_)
        if (type->name == name)
            return type.get();
    return nullptr;
}
const ComponentType *ComponentRegistry::find(std::type_index index) const {
    for (const auto &type : types_)
        if (type->type == index)
            return type.get();
    return nullptr;
}
void ComponentRegistry::addTemplate(EntityTemplate value) {
    const auto duplicate =
        std::find_if(templates_.begin(), templates_.end(), [&](const EntityTemplate &t) {
            return t.name == value.name && t.category == value.category;
        });
    if (duplicate != templates_.end())
        throw std::logic_error("entity template '" + value.name + "' is already registered");
    templates_.push_back(std::move(value));
}
Status ComponentRegistry::validate() const {
    for (const auto &type : types_) {
        for (const auto &dependency : type->dependencies) {
            if (!find(dependency))
                return Error{"component '" + type->name + "' depends on unregistered '" +
                             dependency + "'"};
            if (dependency == type->name)
                return Error{"component '" + type->name + "' depends on itself"};
        }
    }
    // Reject dependency cycles: depth-first search with in-progress / finished marks.
    std::vector<int> state(types_.size(), 0);
    const auto indexOf = [&](const std::string &name) {
        for (std::size_t i = 0; i < types_.size(); ++i)
            if (types_[i]->name == name)
                return i;
        return types_.size();
    };
    const std::function<bool(std::size_t)> acyclic = [&](std::size_t index) {
        if (state[index] == 1)
            return false;
        if (state[index] == 2)
            return true;
        state[index] = 1;
        for (const auto &dependency : types_[index]->dependencies)
            if (!acyclic(indexOf(dependency)))
                return false;
        state[index] = 2;
        return true;
    };
    for (std::size_t i = 0; i < types_.size(); ++i)
        if (!acyclic(i))
            return Error{"component dependencies form a cycle through '" + types_[i]->name + "'"};
    return success();
}
} // namespace yk
