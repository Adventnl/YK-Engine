#include "ui/Panels.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <set>

namespace yk::editor::ui {
namespace {
struct Inspect {
    EditorState &state;
    const Scene &scene;
    EditorDocument *doc; // Null while playing: the panel is read-only.
    EntityId entity;
};

struct PropertyView {
    Inspect &inspect;
    std::size_t componentIndex;
    const PropertyInfo &property;
    std::string changeLabel;
};

// Writes `value` into the property being edited (inside the change commitEdit opened).
void write(const PropertyView &view, PropertyValue value) {
    Entity *entity = view.inspect.doc->edit().find(view.inspect.entity);
    if (!entity || view.componentIndex >= entity->components().size())
        return;
    Component &component = *entity->components()[view.componentIndex];
    view.property.assign(component, std::move(value));
}

// One-shot edit for widgets that change discretely (a list item removed, an entity picked).
void writeOnce(const PropertyView &view, PropertyValue value) {
    view.inspect.doc->change(view.changeLabel, [&](Scene &) { write(view, std::move(value)); });
}

bool containsInsensitive(const std::string &text, const std::string &needle) {
    if (needle.empty())
        return true;
    return std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           }) != text.end();
}

// A searchable list of the scene's entities. Returns the chosen id (or the null id for "None").
std::optional<EntityId> entityPicker(const Inspect &inspect, const char *popupId,
                                     EntityId exclude) {
    std::optional<EntityId> chosen;
    if (!ImGui::BeginPopup(popupId))
        return chosen;
    static std::string filter;
    if (ImGui::IsWindowAppearing()) {
        filter.clear();
        ImGui::SetKeyboardFocusHere();
    }
    inputText("##filter", filter, 0, "Search entities");
    ImGui::BeginChild("##list", {260.0F, 220.0F}, ImGuiChildFlags_Borders);
    if (ImGui::Selectable("(None)"))
        chosen = EntityId{};
    for (const EntityId id : inspect.scene.hierarchyOrder()) {
        if (id == exclude)
            continue;
        const Entity *candidate = inspect.scene.find(id);
        if (!containsInsensitive(candidate->name(), filter))
            continue;
        ImGui::PushID(reinterpret_cast<const void *>(static_cast<std::uintptr_t>(id.value)));
        // Indent by depth so duplicate names stay distinguishable.
        int depth = 0;
        for (const Entity *up = candidate->parent(); up; up = up->parent())
            ++depth;
        ImGui::Indent(static_cast<float>(depth) * 8.0F);
        const bool picked = ImGui::Selectable(entityLabel(inspect.scene, id).c_str());
        markItem("picker/" + candidate->name());
        if (picked)
            chosen = id;
        ImGui::Unindent(static_cast<float>(depth) * 8.0F);
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (chosen)
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return chosen;
}

void startPick(const PropertyView &view, std::function<void(EntityId)> onPick) {
    view.inspect.state.pick = PickRequest{"Click an entity in the Scene view to assign '" +
                                              label(view.property.name) + "'  (Esc cancels)",
                                          std::move(onPick)};
}

// Accepts an entity dragged from the hierarchy onto the last item.
std::optional<EntityId> droppedEntity() {
    std::optional<EntityId> dropped;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("YK_ENTITY")) {
            EntityId id;
            std::memcpy(&id, payload->Data, sizeof id);
            dropped = id;
        }
        ImGui::EndDragDropTarget();
    }
    return dropped;
}

void entityField(const PropertyView &view, EntityId current) {
    const Inspect &inspect = view.inspect;
    const float button = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float nameWidth = ImGui::GetContentRegionAvail().x - 2.0F * (button + spacing);
    const Entity *target = current ? inspect.scene.find(current) : nullptr;
    const std::string text = !current ? "None"
                             : target ? entityLabel(inspect.scene, current)
                                      : "(missing)";
    ImGui::PushStyleColor(ImGuiCol_Text, imColor(!current ? palette::dim
                                                 : target ? Color{216, 220, 230, 255}
                                                          : palette::error));
    const bool clicked =
        ImGui::Button((text + "##target").c_str(), {std::max(20.0F, nameWidth), 0.0F});
    ImGui::PopStyleColor();
    markItem("prop/" + view.property.name);
    tooltip(current ? "Click to choose another entity. Ctrl+click selects it."
                    : "Click to choose an entity, or drop one here.");
    if (clicked) {
        if (ImGui::GetIO().KeyCtrl && target)
            inspect.state.inspect(current);
        else if (inspect.doc)
            ImGui::OpenPopup("entity_picker");
    }
    if (inspect.doc) {
        if (const auto dropped = droppedEntity())
            writeOnce(view, *dropped);
        if (const auto picked = entityPicker(inspect, "entity_picker", inspect.entity))
            writeOnce(view, *picked);
    }
    ImGui::SameLine();
    if (iconButton(("prop/" + view.property.name + "/pick").c_str(), Icon::Target, false,
                   "Pick an entity in the Scene view", 0, button))
        startPick(view,
                  [state = &inspect.state, entity = inspect.entity, component = view.componentIndex,
                   property = view.property.name](EntityId id) {
                      if (state->document && !state->playing())
                          state->document->setProperty(entity, component, property, id);
                  });
    ImGui::SameLine();
    ImGui::BeginDisabled(!current);
    if (iconButton(("prop/" + view.property.name + "/clear").c_str(), Icon::Cross, false, "Clear",
                   0, button))
        writeOnce(view, EntityId{});
    ImGui::EndDisabled();
}

void entityListField(const PropertyView &view, const std::vector<EntityId> &list) {
    const Inspect &inspect = view.inspect;
    std::optional<std::size_t> removeAt;
    for (std::size_t i = 0; i < list.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const Entity *target = inspect.scene.find(list[i]);
        const float button = ImGui::GetFrameHeight();
        ImGui::PushStyleColor(ImGuiCol_Text,
                              imColor(target ? Color{216, 220, 230, 255} : palette::error));
        if (ImGui::Button((entityLabel(inspect.scene, list[i]) + "##item").c_str(),
                          {std::max(20.0F, ImGui::GetContentRegionAvail().x - button -
                                               ImGui::GetStyle().ItemSpacing.x),
                           0.0F}) &&
            target)
            inspect.state.inspect(list[i]);
        ImGui::PopStyleColor();
        markItem("prop/" + view.property.name + "/" + entityLabel(inspect.scene, list[i]));
        tooltip("Click to select this entity.");
        ImGui::SameLine();
        if (iconButton(("prop/" + view.property.name + "/remove/" + std::to_string(i)).c_str(),
                       Icon::Cross, false, "Remove from the list", 0, button))
            removeAt = i;
        ImGui::PopID();
    }
    const float button = ImGui::GetFrameHeight();
    const bool add = ImGui::Button(
        "+ Add",
        {ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x, 0.0F});
    markItem("prop/" + view.property.name + "/add");
    tooltip("Choose an entity to add, or drop one here.");
    if (add && inspect.doc)
        ImGui::OpenPopup("entity_picker");
    // An entity dragged from the hierarchy can be dropped on the Add button.
    const std::optional<EntityId> dropped = inspect.doc ? droppedEntity() : std::nullopt;
    ImGui::SameLine();
    if (iconButton(("prop/" + view.property.name + "/pick").c_str(), Icon::Target, false,
                   "Pick entities in the Scene view", 0, button)) {
        startPick(view, [state = &inspect.state, entity = inspect.entity,
                         component = view.componentIndex,
                         property = view.property.name](EntityId id) {
            if (!state->document || state->playing())
                return;
            // Read the list when the click happens, not when the button was pressed.
            const Entity *owner = state->document->scene().find(entity);
            if (!owner || component >= owner->components().size())
                return;
            const Component &target = *owner->components()[component];
            const PropertyInfo *info = target.type().find(property);
            if (!info)
                return;
            std::vector<EntityId> extended = std::get<std::vector<EntityId>>(info->get(target));
            if (std::find(extended.begin(), extended.end(), id) == extended.end())
                extended.push_back(id);
            state->document->setProperty(entity, component, property, extended);
        });
    }
    if (inspect.doc) {
        std::vector<EntityId> updated = list;
        bool changed = false;
        if (dropped && std::find(updated.begin(), updated.end(), *dropped) == updated.end()) {
            updated.push_back(*dropped);
            changed = true;
        }
        if (const auto picked = entityPicker(inspect, "entity_picker", inspect.entity)) {
            if (*picked && std::find(updated.begin(), updated.end(), *picked) == updated.end()) {
                updated.push_back(*picked);
                changed = true;
            }
        }
        if (removeAt) {
            updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(*removeAt));
            changed = true;
        }
        if (changed)
            writeOnce(view, updated);
    }
}

void stringListField(const PropertyView &view, const std::vector<std::string> &list) {
    const Inspect &inspect = view.inspect;
    std::vector<std::string> updated = list;
    std::optional<std::size_t> removeAt;
    const float button = ImGui::GetFrameHeight();
    for (std::size_t i = 0; i < updated.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button -
                                ImGui::GetStyle().ItemSpacing.x);
        const bool changed = inputText("##item", updated[i]);
        markItem("prop/" + view.property.name + "/" + std::to_string(i));
        if (inspect.doc)
            commitEdit(*inspect.doc, view.changeLabel, changed, [&] { write(view, updated); });
        ImGui::SameLine();
        if (iconButton(("prop/" + view.property.name + "/remove/" + std::to_string(i)).c_str(),
                       Icon::Cross, false, "Remove", 0, button))
            removeAt = i;
        ImGui::PopID();
    }
    // Tags already used in the scene, offered as quick additions.
    std::set<std::string> known;
    inspect.scene.forEach([&](const Entity &entity) {
        for (const std::string &tag : entity.tags())
            known.insert(tag);
    });
    const bool add = ImGui::Button(
        "+ Add",
        {ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x, 0.0F});
    markItem("prop/" + view.property.name + "/add");
    ImGui::SameLine();
    const bool browse = iconButton(("prop/" + view.property.name + "/known").c_str(), Icon::Search,
                                   false, "Choose from tags used in this scene", 0, button);
    if (browse && !known.empty())
        ImGui::OpenPopup("known_tags");
    bool changed = false;
    if (add) {
        updated.emplace_back();
        changed = true;
    }
    if (ImGui::BeginPopup("known_tags")) {
        for (const std::string &tag : known)
            if (ImGui::Selectable(tag.c_str())) {
                updated.push_back(tag);
                changed = true;
            }
        ImGui::EndPopup();
    }
    if (removeAt) {
        updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(*removeAt));
        changed = true;
    }
    if (changed && inspect.doc)
        writeOnce(view, updated);
}

bool assetExists(const EditorState &state, const std::string &path) {
    if (path.empty() || path.starts_with("builtin:") || path.starts_with("tone:"))
        return true;
    if (!state.project)
        return false;
    const auto absolute = state.project->project().resolve(path);
    std::error_code error;
    return absolute && std::filesystem::exists(absolute.value(), error);
}

void assetField(const PropertyView &view, const AssetRef &current) {
    EditorState &state = view.inspect.state;
    const float button = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    std::string path = current.path;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 2.0F * (button + spacing));
    const bool missing = !assetExists(state, path);
    if (missing)
        ImGui::PushStyleColor(ImGuiCol_Text, imColor(palette::error));
    const bool changed =
        inputText("##asset", path, 0,
                  view.property.assetKind.empty() ? "path" : view.property.assetKind.c_str());
    if (missing)
        ImGui::PopStyleColor();
    markItem("prop/" + view.property.name);
    tooltip(missing ? "This file does not exist in the project."
                    : "Project-relative path, or a built-in placeholder.");
    if (view.inspect.doc)
        commitEdit(*view.inspect.doc, view.changeLabel, changed,
                   [&] { write(view, AssetRef{path}); });
    ImGui::SameLine();
    if (iconButton(("prop/" + view.property.name + "/browse").c_str(), Icon::Folder, false,
                   "Choose an asset", 0, button))
        ImGui::OpenPopup("asset_picker");
    ImGui::SameLine();
    ImGui::BeginDisabled(current.path.empty());
    if (iconButton(("prop/" + view.property.name + "/clear").c_str(), Icon::Cross, false, "Clear",
                   0, button) &&
        view.inspect.doc)
        writeOnce(view, AssetRef{});
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("asset_picker")) {
        const std::string &kind = view.property.assetKind;
        std::optional<std::string> chosen;
        if (kind == "texture") {
            if (ImGui::Selectable("builtin:white"))
                chosen = "builtin:white";
            if (ImGui::Selectable("builtin:circle"))
                chosen = "builtin:circle";
        } else if (kind == "sound") {
            struct Preset {
                const char *name;
                const char *tone;
            };
            for (const Preset &preset : {Preset{"Beep (placeholder)", "tone:660,0.12"},
                                         Preset{"Low beep (placeholder)", "tone:220,0.18,square"},
                                         Preset{"Chime (placeholder)", "tone:880,0.25"},
                                         Preset{"Noise burst (placeholder)", "tone:200,0.2,noise"}})
                if (ImGui::Selectable(preset.name))
                    chosen = preset.tone;
        }
        AssetKind wanted = AssetKind::Other;
        if (kind == "texture")
            wanted = AssetKind::Texture;
        else if (kind == "sound")
            wanted = AssetKind::Sound;
        else if (kind == "prefab")
            wanted = AssetKind::Prefab;
        else if (kind == "animation")
            wanted = AssetKind::Animation;
        else if (kind == "scene")
            wanted = AssetKind::Scene;
        if (state.project) {
            bool any = false;
            for (const AssetEntry &entry : state.project->files())
                if (entry.kind == wanted && wanted != AssetKind::Other) {
                    any = true;
                    if (ImGui::Selectable(entry.path.c_str()))
                        chosen = entry.path;
                }
            if (!any && kind != "texture" && kind != "sound")
                ImGui::TextDisabled("No %s assets in this project.", kind.c_str());
        }
        if (chosen && view.inspect.doc) {
            writeOnce(view, AssetRef{*chosen});
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void drawProperty(const PropertyView &view, const Component &component) {
    const PropertyInfo &property = view.property;
    EditorDocument *doc = view.inspect.doc;
    const PropertyValue value = property.get(component);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const std::string id = "prop/" + property.name;
    switch (property.type) {
    case PropertyType::Bool: {
        bool current = std::get<bool>(value);
        const bool changed = ImGui::Checkbox("##v", &current);
        markItem(id);
        if (doc)
            commitEdit(*doc, view.changeLabel, changed, [&] { write(view, current); });
        break;
    }
    case PropertyType::Int: {
        int current = static_cast<int>(std::get<std::int64_t>(value));
        const float low = property.hasRange ? static_cast<float>(property.minValue) : 0.0F;
        const float high = property.hasRange ? static_cast<float>(property.maxValue) : 0.0F;
        const bool changed =
            ImGui::DragInt("##v", &current, 0.25F, static_cast<int>(low), static_cast<int>(high));
        markItem(id);
        if (doc)
            commitEdit(*doc, view.changeLabel, changed,
                       [&] { write(view, static_cast<std::int64_t>(current)); });
        break;
    }
    case PropertyType::Float: {
        float current = static_cast<float>(std::get<double>(value));
        float speed = property.step > 0.0 ? static_cast<float>(property.step) : 0.02F;
        if (property.step <= 0.0 && property.hasRange)
            speed = std::clamp(static_cast<float>(property.maxValue - property.minValue) / 400.0F,
                               0.005F, 0.25F);
        const float low = property.hasRange ? static_cast<float>(property.minValue) : 0.0F;
        const float high = property.hasRange ? static_cast<float>(property.maxValue) : 0.0F;
        const bool changed = ImGui::DragFloat("##v", &current, speed, low, high, "%.3f",
                                              property.hasRange ? ImGuiSliderFlags_AlwaysClamp : 0);
        markItem(id);
        if (doc)
            commitEdit(*doc, view.changeLabel, changed,
                       [&] { write(view, static_cast<double>(current)); });
        break;
    }
    case PropertyType::String: {
        std::string current = std::get<std::string>(value);
        if (property.isLayer && view.inspect.state.project) {
            const LayerConfig &layers = view.inspect.state.project->project().layers;
            const bool known = layers.indexOf(current) >= 0;
            if (!known)
                ImGui::PushStyleColor(ImGuiCol_Text, imColor(palette::error));
            const bool open =
                ImGui::BeginCombo("##v", current.empty() ? "(none)" : current.c_str());
            if (!known)
                ImGui::PopStyleColor();
            markItem(id);
            if (open) {
                for (const std::string &name : layers.names)
                    if (ImGui::Selectable(name.c_str(), name == current) && doc)
                        writeOnce(view, name);
                ImGui::EndCombo();
            }
            if (!known)
                tooltip("This layer is not defined in the project; layer 0 is used instead.");
            break;
        }
        bool changed;
        if (property.multiline)
            changed =
                inputTextMultiline("##v", current, {-FLT_MIN, ImGui::GetTextLineHeight() * 4.5F});
        else
            changed = inputText("##v", current);
        markItem(id);
        if (doc)
            commitEdit(*doc, view.changeLabel, changed, [&] { write(view, current); });
        break;
    }
    case PropertyType::Vec2: {
        Vec2 current = std::get<Vec2>(value);
        float speed = property.step > 0.0 ? static_cast<float>(property.step) : 0.02F;
        if (property.isSize || property.isDisplacement || property.isOffset)
            speed = 0.05F;
        const float low = property.hasRange ? static_cast<float>(property.minValue) : 0.0F;
        const float high = property.hasRange ? static_cast<float>(property.maxValue) : 0.0F;
        const bool changed =
            ImGui::DragFloat2("##v", &current.x, speed, low, high, "%.3f",
                              property.hasRange ? ImGuiSliderFlags_AlwaysClamp : 0);
        markPair(id);
        if (doc)
            commitEdit(*doc, view.changeLabel, changed, [&] { write(view, current); });
        break;
    }
    case PropertyType::Color: {
        const Color current = std::get<Color>(value);
        float channels[4] = {
            static_cast<float>(current.r) / 255.0F, static_cast<float>(current.g) / 255.0F,
            static_cast<float>(current.b) / 255.0F, static_cast<float>(current.a) / 255.0F};
        const bool changed = ImGui::ColorEdit4(
            "##v", channels,
            ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayHex |
                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
        markItem(id);
        if (doc)
            commitEdit(*doc, view.changeLabel, changed, [&] {
                write(view, fromImColor({channels[0], channels[1], channels[2], channels[3]}));
            });
        break;
    }
    case PropertyType::Enum: {
        const auto current = static_cast<std::size_t>(std::get<std::int64_t>(value));
        const char *shown =
            current < property.options.size() ? property.options[current].c_str() : "?";
        const bool open = ImGui::BeginCombo("##v", shown);
        markItem(id);
        if (open) {
            for (std::size_t i = 0; i < property.options.size(); ++i) {
                const bool picked = ImGui::Selectable(property.options[i].c_str(), i == current);
                markItem(id + "/" + property.options[i]);
                if (picked && doc)
                    writeOnce(view, static_cast<std::int64_t>(i));
            }
            ImGui::EndCombo();
        }
        break;
    }
    case PropertyType::EntityReference:
        entityField(view, std::get<EntityId>(value));
        break;
    case PropertyType::EntityReferenceList:
        entityListField(view, std::get<std::vector<EntityId>>(value));
        break;
    case PropertyType::StringList:
        stringListField(view, std::get<std::vector<std::string>>(value));
        break;
    case PropertyType::Asset:
        assetField(view, std::get<AssetRef>(value));
        break;
    }
}

void drawComponent(Inspect &inspect, std::size_t index, std::optional<std::size_t> &removeRequest) {
    const Entity *entity = inspect.scene.find(inspect.entity);
    const Component &component = *entity->components()[index];
    const ComponentType &type = component.type();
    ImGui::PushID(static_cast<int>(index));
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                               ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth;
    const bool open = ImGui::CollapsingHeader((type.name + "###component").c_str(), flags);
    markItem("component/" + type.name);
    tooltip(type.description);
    const float scrollbar = ImGui::GetScrollMaxY() > 0.0F ? ImGui::GetStyle().ScrollbarSize : 0.0F;
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - scrollbar -
                    58.0F);
    bool enabled = component.enabled;
    if (ImGui::Checkbox("##enabled", &enabled) && inspect.doc) {
        const EntityId id = inspect.entity;
        inspect.doc->change("Toggle " + type.name, [&](Scene &scene) {
            scene.find(id)->components()[index]->enabled = enabled;
        });
    }
    tooltip("Enable or disable this component.");
    ImGui::SameLine();
    if (ImGui::SmallButton("...") && inspect.doc)
        ImGui::OpenPopup("component_menu");
    markItem("component/" + type.name + "/menu");
    if (ImGui::BeginPopup("component_menu")) {
        const std::string blocker = entity->removalBlocker(component);
        if (ImGui::MenuItem("Remove Component", nullptr, false, blocker.empty()))
            removeRequest = index;
        markItem("component/" + type.name + "/remove");
        if (!blocker.empty())
            tooltip("'" + blocker + "' needs this component.");
        if (ImGui::MenuItem("Reset to Defaults") && inspect.doc) {
            const EntityId id = inspect.entity;
            inspect.doc->change("Reset " + type.name, [&](Scene &scene) {
                Component &target = *scene.find(id)->components()[index];
                for (const PropertyInfo &property : target.type().properties)
                    if (!property.readOnly)
                        property.assign(target, property.defaultValue);
            });
        }
        ImGui::EndPopup();
    }
    if (open) {
        ImGui::Spacing();
        if (ImGui::BeginTable("##props", 2,
                              ImGuiTableFlags_NoSavedSettings |
                                  ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.46F);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.54F);
            for (const PropertyInfo &property : type.properties) {
                if (property.hidden)
                    continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                nameCell(label(property.name), property.tooltip);
                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(property.name.c_str());
                const PropertyView view{inspect, index, property, "Edit " + label(property.name)};
                if (property.readOnly)
                    ImGui::BeginDisabled();
                drawProperty(view, component);
                if (property.readOnly)
                    ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
    }
    ImGui::PopID();
}

void drawHeader(Inspect &inspect, const Entity &entity) {
    EditorDocument *doc = inspect.doc;
    bool active = entity.active();
    if (ImGui::Checkbox("##active", &active) && doc)
        doc->setEntityActive(inspect.entity, active);
    markItem("inspector/active");
    tooltip("An inactive entity and everything below it is ignored by the game.");
    ImGui::SameLine();
    std::string name = entity.name();
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool changed = inputText("##name", name);
    markItem("inspector/name");
    if (doc)
        commitEdit(*doc, "Rename", changed, [&] {
            if (Entity *target = doc->edit().find(inspect.entity))
                target->setName(name);
        });

    // Tags, edited as a comma separated list.
    std::string tags;
    for (const std::string &tag : entity.tags())
        tags += (tags.empty() ? "" : ", ") + tag;
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool tagsChanged = inputText("##tags", tags, 0, "Tags (comma separated)");
    markItem("inspector/tags");
    tooltip("Tags let hazards, plates and exits tell entities apart, e.g. fire, water.");
    if (doc)
        commitEdit(*doc, "Edit Tags", tagsChanged, [&] {
            std::vector<std::string> parsed;
            std::string current;
            const auto flush = [&] {
                const auto first = current.find_first_not_of(" \t");
                const auto last = current.find_last_not_of(" \t");
                if (first != std::string::npos)
                    parsed.push_back(current.substr(first, last - first + 1));
                current.clear();
            };
            for (const char c : tags) {
                if (c == ',')
                    flush();
                else
                    current += c;
            }
            flush();
            if (Entity *target = doc->edit().find(inspect.entity))
                target->setTags(parsed);
        });
}

void drawTransform(Inspect &inspect, const Entity &entity) {
    EditorDocument *doc = inspect.doc;
    if (!ImGui::CollapsingHeader("Transform",
                                 ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
        return;
    markItem("component/Transform");
    ImGui::Spacing();
    if (!ImGui::BeginTable("##transform", 2,
                           ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.46F);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.54F);
    const auto row = [&](const char *name, const char *tip) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name);
        tooltip(tip);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };
    Transform2D transform = entity.transform();
    const char *space = entity.parentId() ? "Relative to the parent entity."
                                          : "World position, in meters (+Y is down).";
    row("Position", space);
    bool changed =
        ImGui::DragFloat2("##position", &transform.position.x, 0.05F, 0.0F, 0.0F, "%.3f");
    markPair("inspector/position");
    if (doc)
        commitEdit(*doc, "Edit Position", changed, [&] {
            doc->edit().find(inspect.entity)->transform().position = transform.position;
        });
    row("Rotation", "Degrees, clockwise on screen.");
    changed =
        ImGui::DragFloat("##rotation", &transform.rotationDegrees, 0.5F, 0.0F, 0.0F, "%.2f deg");
    markItem("inspector/rotation");
    if (doc)
        commitEdit(*doc, "Edit Rotation", changed, [&] {
            doc->edit().find(inspect.entity)->transform().rotationDegrees =
                transform.rotationDegrees;
        });
    row("Scale", "Multiplies sizes and offsets of this entity's components.");
    changed = ImGui::DragFloat2("##scale", &transform.scale.x, 0.01F, 0.0F, 0.0F, "%.3f");
    markPair("inspector/scale");
    if (doc)
        commitEdit(*doc, "Edit Scale", changed,
                   [&] { doc->edit().find(inspect.entity)->transform().scale = transform.scale; });
    ImGui::EndTable();
    ImGui::Spacing();
}

void addComponentPopup(Inspect &inspect) {
    if (!ImGui::BeginPopup("add_component"))
        return;
    static std::string filter;
    if (ImGui::IsWindowAppearing()) {
        filter.clear();
        ImGui::SetKeyboardFocusHere();
    }
    inputText("##search", filter, 0, "Search components");
    ImGui::Separator();
    if (filter.empty()) {
        addComponentMenu(inspect.state, inspect.entity);
    } else {
        const Entity *entity = inspect.scene.find(inspect.entity);
        for (const auto &type : inspect.state.registry.types()) {
            if (type->hiddenInMenus || !(containsInsensitive(type->name, filter) ||
                                         containsInsensitive(type->category, filter)))
                continue;
            const bool present =
                entity && entity->findComponent(type->name) && !type->allowMultiple;
            if (ImGui::MenuItem((type->name + "  (" + type->category + ")").c_str(), nullptr, false,
                                !present)) {
                inspect.doc->addComponent(inspect.entity, type->name);
                ImGui::CloseCurrentPopup();
            }
            markItem("add/" + type->name);
            tooltip(type->description);
        }
    }
    ImGui::EndPopup();
}
} // namespace

void inspectorPanel(EditorState &state) {
    const Scene *scene = state.visibleScene();
    const EntityId id = state.inspected();
    const Entity *entity = scene ? scene->find(id) : nullptr;
    if (!entity) {
        ImGui::TextDisabled(scene ? "Select an entity to edit it." : "Nothing to inspect.");
        return;
    }
    EditorDocument *doc = state.playing() ? nullptr : state.document.get();
    Inspect inspect{state, *scene, doc, id};
    if (doc && doc->selection().size() > 1)
        ImGui::TextColored(imColor(palette::dim), "%zu selected. Showing '%s'.",
                           doc->selection().size(), entity->name().c_str());
    if (!doc)
        ImGui::TextColored(imColor(palette::good), "Running copy (read-only)");
    ImGui::BeginDisabled(!doc);
    drawHeader(inspect, *entity);
    ImGui::Spacing();
    drawTransform(inspect, *entity);
    std::optional<std::size_t> removeRequest;
    for (std::size_t i = 0; i < entity->components().size(); ++i)
        drawComponent(inspect, i, removeRequest);
    ImGui::Spacing();
    if (ImGui::Button("Add Component", {-FLT_MIN, 28.0F}) && doc)
        ImGui::OpenPopup("add_component");
    markItem("inspector/add-component");
    if (doc)
        addComponentPopup(inspect);
    ImGui::EndDisabled();
    ImGui::TextDisabled("id %s", toString(id).c_str());
    if (removeRequest && doc) {
        if (auto removed = doc->removeComponent(id, *removeRequest); !removed)
            log(LogLevel::Warning, "editor", removed.error());
    }
}
} // namespace yk::editor::ui
