#include "ui/Panels.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

// The side bar views that are not the scene tree or the project files: prefabs, the component
// catalog and the build panel.
namespace yk::editor::ui {
namespace {
bool contains(const std::string &text, const std::string &needle) {
    if (needle.empty())
        return true;
    return std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           }) != text.end();
}

// The search box every list view starts with.
void searchBox(const char *id, std::string &value, const char *hint) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    inputText((std::string("##") + id).c_str(), value, 0, hint);
    markItem(std::string(id) + "/filter");
}

// A section title with a chevron, like VS Code's collapsible view sections.
bool section(const std::string &title, const std::string &id, bool openByDefault = true) {
    ImGui::SetNextItemOpen(openByDefault, ImGuiCond_Once);
    ImGui::PushStyleColor(ImGuiCol_Header, imColor(Color{0, 0, 0, 0}));
    ImGui::PushFont(fonts().semibold, 12.0F);
    const bool open = ImGui::CollapsingHeader((headerText(title) + "###" + id).c_str(),
                                              ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopFont();
    ImGui::PopStyleColor();
    markItem("section/" + id);
    return open;
}

void sectionGap() {
    ImGui::Dummy({1.0F, 4.0F});
}
} // namespace

void prefabsPanel(EditorState &state) {
    if (!state.project) {
        ImGui::TextDisabled("No project is open.");
        return;
    }
    searchBox("prefabs", state.prefabFilter, "Search prefabs");
    std::map<std::string, std::vector<std::string>> folders;
    for (const std::string &path : state.prefabPaths())
        if (contains(path, state.prefabFilter))
            folders[std::filesystem::path(path).parent_path().generic_string()].push_back(path);
    ImGui::BeginChild("##prefablist", {0.0F, 0.0F}, ImGuiChildFlags_None);
    if (folders.empty())
        ImGui::TextDisabled(state.prefabFilter.empty()
                                ? "The project has no prefabs yet.\nSelect an entity and choose "
                                  "Entity > Save as Prefab."
                                : "Nothing matches.");
    for (const auto &[folder, paths] : folders) {
        if (!section(folder.empty() ? "(project root)" : folder, "prefabs/" + folder))
            continue;
        for (const std::string &path : paths) {
            const std::string name = std::filesystem::path(path).stem().string();
            ImGui::PushID(path.c_str());
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            ImGui::Selectable("##prefab", false, ImGuiSelectableFlags_AllowDoubleClick,
                              {width, 22.0F});
            const bool doubleClicked =
                ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            markItem("asset/" + path);
            tooltip(path + "\nDouble-click to add to the scene, or drag it into the scene view.");
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("YK_PREFAB", path.c_str(), path.size() + 1);
                ImGui::TextUnformatted(name.c_str());
                ImGui::TextDisabled("Drop into the scene view");
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginPopupContextItem("prefab_menu")) {
                {
                    const PopupLook look;
                    if (ImGui::MenuItem("Add to Scene", nullptr, false,
                                        state.document != nullptr && !state.playing()))
                        state.instantiatePrefab(path, defaultSpawnPoint(state));
                    if (ImGui::MenuItem("Show in Explorer")) {
                        state.selectedAsset = path;
                        state.layout.sideView = SideView::Explorer;
                    }
                    if (ImGui::MenuItem("Copy Path"))
                        ImGui::SetClipboardText(path.c_str());
                }
                ImGui::EndPopup();
            }
            ImDrawList &list = *ImGui::GetWindowDrawList();
            drawIcon(list, Icon::Prefab, {at.x + 14.0F, at.y + 11.0F}, 15.0F,
                     packed(Color{110, 210, 150, 255}));
            list.AddText({at.x + 30.0F, at.y + 3.0F}, packed(vs::text), name.c_str());
            ImGui::PopID();
            if (doubleClicked && state.document && !state.playing())
                state.instantiatePrefab(path, defaultSpawnPoint(state));
        }
    }
    ImGui::EndChild();
}

void componentsPanel(EditorState &state) {
    searchBox("components", state.componentFilter, "Search components");
    static std::string selectedType;
    const ComponentType *chosen = nullptr;
    std::vector<std::string> categories;
    for (const auto &type : state.registry.types())
        if (!type->hiddenInMenus &&
            std::find(categories.begin(), categories.end(), type->category) == categories.end())
            categories.push_back(type->category);
    std::sort(categories.begin(), categories.end());

    const float detailHeight = std::min(250.0F, ImGui::GetContentRegionAvail().y * 0.5F);
    ImGui::BeginChild("##componentlist",
                      {0.0F, ImGui::GetContentRegionAvail().y - detailHeight - 4.0F},
                      ImGuiChildFlags_None);
    for (const std::string &category : categories) {
        bool any = false;
        for (const auto &type : state.registry.types())
            any = any || (!type->hiddenInMenus && type->category == category &&
                          (contains(type->name, state.componentFilter) ||
                           contains(type->description, state.componentFilter)));
        if (!any || !section(category, "components/" + category))
            continue;
        for (const auto &type : state.registry.types()) {
            if (type->hiddenInMenus || type->category != category ||
                !(contains(type->name, state.componentFilter) ||
                  contains(type->description, state.componentFilter)))
                continue;
            ImGui::PushID(type->name.c_str());
            const bool isSelected = selectedType == type->name;
            const ImVec2 at = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##type", isSelected, ImGuiSelectableFlags_AllowDoubleClick,
                                  {ImGui::GetContentRegionAvail().x, 22.0F}))
                selectedType = type->name;
            const bool doubleClicked =
                ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            markItem("component/" + type->name);
            tooltip(type->description);
            ImDrawList &list = *ImGui::GetWindowDrawList();
            drawIcon(list, Icon::Components, {at.x + 14.0F, at.y + 11.0F}, 15.0F,
                     packed(vs::textDim));
            list.AddText({at.x + 30.0F, at.y + 3.0F}, packed(vs::text), type->name.c_str());
            ImGui::PopID();
            if (doubleClicked && state.document && !state.playing() && state.document->primary())
                state.document->addComponent(state.document->primary(), type->name);
        }
    }
    ImGui::EndChild();
    chosen = selectedType.empty() ? nullptr : state.registry.find(selectedType);
    ImGui::BeginChild("##componentdetail", {0.0F, 0.0F}, ImGuiChildFlags_Borders);
    if (!chosen) {
        ImGui::TextDisabled("Select a component to see what it does.");
    } else {
        ImGui::PushFont(fonts().semibold, 14.0F);
        ImGui::TextUnformatted(chosen->name.c_str());
        ImGui::PopFont();
        ImGui::TextColored(imColor(vs::textDim), "%s", chosen->category.c_str());
        ImGui::TextWrapped("%s", chosen->description.c_str());
        if (!chosen->dependencies.empty()) {
            std::string needs;
            for (const std::string &dependency : chosen->dependencies)
                needs += (needs.empty() ? "" : ", ") + dependency;
            ImGui::TextColored(imColor(vs::textDim), "Also adds: %s", needs.c_str());
        }
        ImGui::Spacing();
        ImGui::TextColored(imColor(vs::textDim), "%zu properties", chosen->properties.size());
        std::string names;
        for (const PropertyInfo &property : chosen->properties)
            if (!property.readOnly)
                names += (names.empty() ? "" : ", ") + property.name;
        ImGui::PushFont(fonts().mono, 11.5F);
        ImGui::TextWrapped("%s", names.c_str());
        ImGui::PopFont();
        ImGui::Spacing();
        const EntityId target =
            state.document && !state.playing() ? state.document->primary() : EntityId{};
        const Entity *entity = target ? state.document->scene().find(target) : nullptr;
        const bool present =
            entity && entity->findComponent(chosen->name) && !chosen->allowMultiple;
        ImGui::BeginDisabled(!entity || present);
        const std::string label =
            entity ? "Add to " + entity->name() : "Select an entity to add it";
        if (ImGui::Button(label.c_str(), {-FLT_MIN, 0.0F}))
            state.document->addComponent(target, chosen->name);
        markItem("components/add");
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
}

void buildPanel(EditorState &state) {
    if (!state.project) {
        ImGui::TextDisabled("No project is open.");
        return;
    }
    const auto action = [](const char *id, Icon icon, const char *title, const char *description,
                           bool enabled) {
        ImGui::BeginDisabled(!enabled);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::PushID(id);
        const bool clicked = ImGui::InvisibleButton("##action", {width, 46.0F});
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        markItem(id);
        ImDrawList &list = *ImGui::GetWindowDrawList();
        if (hovered)
            list.AddRectFilled(at, {at.x + width, at.y + 46.0F}, packed(vs::listHover), 4.0F);
        drawIcon(list, icon, {at.x + 20.0F, at.y + 23.0F}, 20.0F, packed(vs::focus));
        list.AddText(fonts().semibold, 13.5F, {at.x + 40.0F, at.y + 6.0F}, packed(vs::text), title);
        list.AddText(fonts().ui, 12.0F, {at.x + 40.0F, at.y + 25.0F}, packed(vs::textDim),
                     description);
        ImGui::EndDisabled();
        return clicked;
    };
    if (section("Project", "build/project")) {
        sectionGap();
        if (action("build/validate", Icon::Check, "Validate Project",
                   "Check every scene, prefab and asset", true))
            state.validateProject();
        if (action("build/run", Icon::Play, "Run in Player",
                   "Start the standalone player on this project", !state.playing()))
            runInPlayer(state);
        sectionGap();
    }
    if (section("Export", "build/export")) {
        sectionGap();
        if (action("build/export", Icon::Build, "Export Game...",
                   "Package the game for another computer", !state.playing()))
            showDialog(state, DialogKind::Export);
        sectionGap();
    }
    if (section("Last Output", "build/output")) {
        ImGui::PushFont(fonts().mono, 11.5F);
        const std::size_t first = state.output.size() > 8 ? state.output.size() - 8 : 0;
        if (state.output.empty())
            ImGui::TextDisabled("Nothing yet.");
        for (std::size_t i = first; i < state.output.size(); ++i)
            ImGui::TextWrapped("%s", state.output[i].text.c_str());
        ImGui::PopFont();
    }
}
} // namespace yk::editor::ui
