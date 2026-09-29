#include "ui/Panels.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>
#include <cctype>
#include <map>

namespace yk::editor::ui {
namespace {
Icon iconFor(AssetKind kind) {
    switch (kind) {
    case AssetKind::Scene:
        return Icon::Scene;
    case AssetKind::Prefab:
        return Icon::Prefab;
    case AssetKind::Texture:
        return Icon::Image;
    case AssetKind::Sound:
        return Icon::Sound;
    default:
        return Icon::File;
    }
}

Color colorFor(AssetKind kind) {
    switch (kind) {
    case AssetKind::Scene:
        return {110, 180, 240, 255};
    case AssetKind::Prefab:
        return {110, 210, 150, 255};
    case AssetKind::Texture:
        return {220, 170, 110, 255};
    case AssetKind::Sound:
        return {200, 140, 230, 255};
    default:
        return palette::dim;
    }
}

bool matches(const std::string &text, const std::string &needle) {
    if (needle.empty())
        return true;
    return std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           }) != text.end();
}

void fileRow(EditorState &state, const AssetEntry &entry) {
    const std::string name = std::filesystem::path(entry.path).filename().string();
    ImGui::PushID(entry.path.c_str());
    const bool isCurrent = state.document && state.document->path() == entry.path;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Selectable("##file", isCurrent, ImGuiSelectableFlags_AllowDoubleClick);
    const bool doubleClicked =
        ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    markItem("asset/" + entry.path);
    tooltip(entry.path);
    // Drag and context menu belong to the selectable, so they come before the label is laid over
    // it.
    if (entry.kind == AssetKind::Prefab && ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("YK_PREFAB", entry.path.c_str(), entry.path.size() + 1);
        ImGui::TextUnformatted(name.c_str());
        ImGui::TextDisabled("Drop into the Scene view");
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginPopupContextItem("asset_menu")) {
        if (entry.kind == AssetKind::Scene) {
            if (ImGui::MenuItem("Open", nullptr, false, !state.playing()))
                state.guarded([&state, path = entry.path] { state.openScene(path); });
            if (ImGui::MenuItem("Set as Start Scene", nullptr, false, state.project != nullptr)) {
                state.project->project().startScene = entry.path;
                if (auto saved = state.project->save(); !saved)
                    log(LogLevel::Error, "editor", saved.error());
                else
                    log(LogLevel::Info, "editor", "Start scene is now " + entry.path);
            }
        }
        if (entry.kind == AssetKind::Prefab &&
            ImGui::MenuItem("Add to Scene", nullptr, false,
                            state.document != nullptr && !state.playing()))
            state.instantiatePrefab(entry.path, defaultSpawnPoint(state));
        if (ImGui::MenuItem("Copy Path"))
            ImGui::SetClipboardText(entry.path.c_str());
        ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleColor(ImGuiCol_Text,
                          imColor(isCurrent ? palette::selection : Color{216, 220, 230, 255}));
    iconLabel(iconFor(entry.kind), name.c_str(), packed(colorFor(entry.kind)));
    ImGui::PopStyleColor();

    if (doubleClicked) {
        if (entry.kind == AssetKind::Scene && !state.playing())
            state.guarded([&state, path = entry.path] { state.openScene(path); });
        else if (entry.kind == AssetKind::Prefab && state.document && !state.playing())
            state.instantiatePrefab(entry.path, defaultSpawnPoint(state));
    }
    ImGui::PopID();
}
} // namespace

void assetsPanel(EditorState &state) {
    if (!state.showAssets)
        return;
    if (!ImGui::Begin("Assets", &state.showAssets)) {
        ImGui::End();
        return;
    }
    markWindow("panel/Assets");
    if (!state.project) {
        ImGui::TextDisabled("No project is open.");
        ImGui::End();
        return;
    }
    const float button = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x -
                            2.0F * (button + ImGui::GetStyle().ItemSpacing.x));
    inputText("##assetfilter", state.assetFilter, 0, "Search");
    markItem("assets/filter");
    ImGui::SameLine();
    if (iconButton("assets/refresh", Icon::Restart, false, "Rescan the project folder", 0, button))
        state.project->refresh();
    ImGui::SameLine();
    ImGui::BeginDisabled(state.playing());
    if (iconButton("assets/newscene", Icon::Plus, false, "New scene", 0, button))
        state.guarded([&state] { showDialog(state, DialogKind::NewScene); });
    ImGui::EndDisabled();

    std::map<std::string, std::vector<const AssetEntry *>> folders;
    for (const AssetEntry &entry : state.project->files()) {
        if (!matches(entry.path, state.assetFilter))
            continue;
        const std::string folder = std::filesystem::path(entry.path).parent_path().generic_string();
        folders[folder].push_back(&entry);
    }
    ImGui::BeginChild("##assettree");
    if (folders.empty())
        ImGui::TextDisabled(state.assetFilter.empty() ? "The project has no files yet."
                                                      : "Nothing matches.");
    for (const auto &[folder, entries] : folders) {
        const std::string title = folder.empty() ? state.project->project().name : folder;
        ImGui::PushID(folder.c_str());
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        const bool open = ImGui::TreeNodeEx("##folder", ImGuiTreeNodeFlags_SpanFullWidth |
                                                            ImGuiTreeNodeFlags_FramePadding);
        ImGui::SameLine(0.0F, 2.0F);
        iconLabel(Icon::Folder, title.c_str(), IM_COL32(200, 190, 130, 255));
        markItem("assets/folder/" + folder);
        if (open) {
            for (const AssetEntry *entry : entries)
                fileRow(state, *entry);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}
} // namespace yk::editor::ui
