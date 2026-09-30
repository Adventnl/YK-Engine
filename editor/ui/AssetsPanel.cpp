#include "ui/Panels.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

// The Explorer: the project's files as a folder tree, like VS Code's. A click selects a file and
// the Inspector shows it; a double click opens a scene or adds a prefab to the open scene; prefabs
// can be dragged into the scene view.
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
    case AssetKind::Animation:
        return Icon::Animation;
    case AssetKind::Controller:
        return Icon::Code;
    case AssetKind::Dialogue:
        return Icon::Code;
    case AssetKind::TextureMeta:
        return Icon::Settings;
    case AssetKind::Other:
        break;
    }
    return Icon::File;
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
    case AssetKind::Animation:
        return {240, 130, 160, 255};
    case AssetKind::Controller:
        return {240, 165, 90, 255};
    case AssetKind::Dialogue:
        return {198, 170, 235, 255};
    case AssetKind::TextureMeta:
    case AssetKind::Other:
        break;
    }
    return vs::textDim;
}

bool matches(const std::string &text, const std::string &needle) {
    if (needle.empty())
        return true;
    return std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           }) != text.end();
}

std::string lowered(std::string text) {
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// One folder or file of the tree.
struct Node {
    std::string name;
    std::string path; // Project-relative; the folder's own path for folders.
    const AssetEntry *entry{};
    std::vector<Node> children;
};

Node &childFolder(Node &parent, const std::string &name) {
    for (Node &child : parent.children)
        if (!child.entry && child.name == name)
            return child;
    Node folder;
    folder.name = name;
    folder.path = parent.path.empty() ? name : parent.path + "/" + name;
    parent.children.push_back(std::move(folder));
    return parent.children.back();
}

void sortTree(Node &node) {
    std::sort(node.children.begin(), node.children.end(), [](const Node &a, const Node &b) {
        if ((a.entry == nullptr) != (b.entry == nullptr))
            return a.entry == nullptr; // Folders first.
        return lowered(a.name) < lowered(b.name);
    });
    for (Node &child : node.children)
        sortTree(child);
}

Node buildTree(const std::vector<AssetEntry> &files, const std::string &filter) {
    Node root;
    for (const AssetEntry &entry : files) {
        if (!matches(entry.path, filter))
            continue;
        Node *at = &root;
        const std::filesystem::path path(entry.path);
        for (const auto &part : path.parent_path())
            if (!part.empty() && part != ".")
                at = &childFolder(*at, part.string());
        Node file;
        file.name = path.filename().string();
        file.path = entry.path;
        file.entry = &entry;
        at->children.push_back(std::move(file));
    }
    sortTree(root);
    return root;
}

bool startsWithFolder(const std::string &path, const std::string &folder) {
    return path.size() > folder.size() && path.compare(0, folder.size(), folder) == 0 &&
           path[folder.size()] == '/';
}

float rowPadding() {
    return std::max(2.0F, (metrics::row - ImGui::GetFontSize()) * 0.5F);
}

void fileRow(EditorState &state, const Node &node) {
    const AssetEntry &entry = *node.entry;
    ImGui::PushID(node.path.c_str());
    const bool isCurrent = state.document && state.document->path() == entry.path;
    const bool selected = state.selectedAsset == entry.path;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                               ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;
    if (selected)
        flags |= ImGuiTreeNodeFlags_Selected;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4.0F, rowPadding()});
    ImGui::TreeNodeEx("##file", flags);
    ImGui::PopStyleVar();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool doubleClicked =
        ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    markItem("asset/" + entry.path);
    tooltip(entry.path);
    if (state.explorerReveal == entry.path) {
        ImGui::SetScrollHereY(0.5F);
        state.explorerReveal.clear();
    }
    if (clicked) {
        state.showAssetInExplorer(entry.path);
        state.explorerReveal.clear(); // It is right here already.
    }
    // Drag and context menu belong to the row, so they come before the label is laid over it.
    if (entry.kind == AssetKind::Prefab && ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("YK_PREFAB", entry.path.c_str(), entry.path.size() + 1);
        ImGui::TextUnformatted(node.name.c_str());
        ImGui::TextDisabled("Drop into the Scene view");
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginPopupContextItem("asset_menu")) {
        {
            const PopupLook look;
            state.selectedAsset = entry.path;
            if (entry.kind == AssetKind::Scene) {
                if (ImGui::MenuItem("Open", nullptr, false, !state.playing()))
                    state.openScene(entry.path);
                if (ImGui::MenuItem("Set as Start Scene", nullptr, false,
                                    state.project != nullptr)) {
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
            if (ImGui::MenuItem("Show in File Manager") && state.project) {
                const auto absolute =
                    state.project->project().root / std::filesystem::path(entry.path);
                const std::string url = toFileUrl(absolute.parent_path());
                SDL_OpenURL(url.c_str());
            }
            ImGui::Separator();
            state.explorerTarget = entry.path;
            // Import settings belong to their picture: they are renamed with it.
            if (ImGui::MenuItem("Rename or Move...", "F2", false,
                                !state.playing() && entry.kind != AssetKind::TextureMeta))
                state.askToMoveAsset(entry.path);
            markItem("assets/menu/rename");
            if (ImGui::MenuItem("Delete", "Del", false, !state.playing()))
                state.askToDeleteAsset(entry.path);
            markItem("assets/menu/delete");
        }
        ImGui::EndPopup();
    }
    ImDrawList &list = *ImGui::GetWindowDrawList();
    ImGui::SameLine(0.0F, 2.0F);
    const bool dim = entry.kind == AssetKind::TextureMeta;
    ImGui::PushStyleColor(ImGuiCol_Text, imColor(dim ? vs::textDim : vs::text));
    iconLabel(iconFor(entry.kind), node.name.c_str(), packed(colorFor(entry.kind)));
    ImGui::PopStyleColor();
    if (isCurrent) {
        // The scene being edited gets a dot at the right edge.
        const ImVec2 max = ImGui::GetItemRectMax();
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - 10.0F;
        list.AddCircleFilled({right, (ImGui::GetItemRectMin().y + max.y) * 0.5F}, 3.0F,
                             packed(vs::focus));
    }
    if (doubleClicked) {
        if (entry.kind == AssetKind::Scene && !state.playing())
            state.openScene(entry.path);
        else if (entry.kind == AssetKind::Prefab && state.document && !state.playing())
            state.instantiatePrefab(entry.path, defaultSpawnPoint(state));
    }
    ImGui::PopID();
}

// The scenes that are open in tabs, like VS Code's "Open Editors": click to switch to one, the dot
// marks unsaved changes and the cross (on hover) closes it, asking first when there are any.
void openScenesSection(EditorState &state) {
    static bool open = true;
    if (state.sceneTabs.empty())
        return;
    if (!sectionHeader("assets/section/open", "Open Scenes", open, false) || !open)
        return;
    for (const std::string &path : std::vector<std::string>(state.sceneTabs)) {
        const std::string name = std::filesystem::path(path).filename().string();
        const bool current = state.document && state.document->path() == path;
        const EditorDocument *doc = nullptr;
        if (current)
            doc = state.document.get();
        else if (const auto found = state.background.find(path); found != state.background.end())
            doc = found->second.document.get();
        ImGui::PushID(path.c_str());
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                   ImGuiTreeNodeFlags_SpanFullWidth |
                                   ImGuiTreeNodeFlags_FramePadding |
                                   ImGuiTreeNodeFlags_AllowOverlap;
        if (current)
            flags |= ImGuiTreeNodeFlags_Selected;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {dp(14.0F), rowPadding()});
        ImGui::TreeNodeEx("##openscene", flags);
        ImGui::PopStyleVar();
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        markItem("assets/open/" + name);
        tooltip(path);
        ImGui::SameLine(0.0F, 2.0F);
        ImGui::PushStyleColor(ImGuiCol_Text, imColor(current ? vs::text : vs::textDim));
        iconLabel(Icon::Scene, name.c_str(), packed(colorFor(AssetKind::Scene)));
        ImGui::PopStyleColor();
        // The right end: a dot for unsaved changes, a cross to close while the row is hovered.
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        const ImVec2 mid{right - dp(14.0F), (min.y + max.y) * 0.5F};
        bool closed = false;
        if (hovered || (doc && doc->dirty())) {
            const float box = dp(18.0F);
            ImGui::SetCursorScreenPos({mid.x - box * 0.5F, mid.y - box * 0.5F});
            ImGui::InvisibleButton("##closescene", {box, box});
            const bool overClose = ImGui::IsItemHovered();
            markItem("assets/open/" + name + "/close");
            if (overClose)
                ImGui::GetWindowDrawList()->AddRectFilled({mid.x - box * 0.5F, mid.y - box * 0.5F},
                                                          {mid.x + box * 0.5F, mid.y + box * 0.5F},
                                                          packed(vs::pill), dp(4.0F));
            if (hovered || overClose)
                drawIcon(*ImGui::GetWindowDrawList(), Icon::Cross, mid, dp(12.0F),
                         packed(vs::text));
            else
                ImGui::GetWindowDrawList()->AddCircleFilled(mid, dp(3.5F), packed(vs::text));
            closed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        }
        ImGui::PopID();
        if (closed) {
            state.closeScene(path);
            break;
        }
        if (clicked && !current) {
            state.activateScene(path);
            break;
        }
    }
}

void drawNode(EditorState &state, const Node &node, bool filtering);

void folderRow(EditorState &state, const Node &node, bool filtering) {
    ImGui::PushID(node.path.c_str());
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;
    if (state.explorerFolder == node.path)
        flags |= ImGuiTreeNodeFlags_Selected;
    // A search shows every match; a file that was just imported or asked for is revealed.
    if (filtering ||
        (!state.explorerReveal.empty() && startsWithFolder(state.explorerReveal, node.path)))
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4.0F, rowPadding()});
    const bool open = ImGui::TreeNodeEx("##folder", flags);
    ImGui::PopStyleVar();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        state.explorerFolder = node.path;
        state.explorerTarget = node.path;
    }
    markItem("assets/folder/" + node.path);
    tooltip(node.path);
    if (ImGui::BeginPopupContextItem("folder_menu")) {
        {
            const PopupLook look;
            state.explorerFolder = node.path;
            if (ImGui::MenuItem("New Scene Here...", nullptr, false, !state.playing()))
                showDialog(state, DialogKind::NewScene);
            if (ImGui::MenuItem("Import Assets Here..."))
                state.chooseAssetsToImport(nullptr);
            if (ImGui::MenuItem("Copy Path"))
                ImGui::SetClipboardText(node.path.c_str());
            ImGui::Separator();
            state.explorerTarget = node.path;
            if (ImGui::MenuItem("Rename or Move...", "F2", false, !state.playing()))
                state.askToMoveAsset(node.path);
            markItem("assets/menu/rename");
            if (ImGui::MenuItem("Delete Folder", "Del", false, !state.playing()))
                state.askToDeleteAsset(node.path);
            markItem("assets/menu/delete");
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine(0.0F, 2.0F);
    iconLabel(open ? Icon::FolderOpen : Icon::Folder, node.name.c_str(),
              packed(Color{200, 190, 130, 255}));
    if (open) {
        for (const Node &child : node.children)
            drawNode(state, child, filtering);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void drawNode(EditorState &state, const Node &node, bool filtering) {
    if (node.entry)
        fileRow(state, node);
    else
        folderRow(state, node, filtering);
}
} // namespace

void assetsPanel(EditorState &state) {
    if (!state.project) {
        ImGui::TextDisabled("No project is open.");
        return;
    }
    const float button = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x -
                            3.0F * (button + ImGui::GetStyle().ItemSpacing.x));
    inputText("##assetfilter", state.assetFilter, 0, "Search files");
    markItem("assets/filter");
    ImGui::SameLine();
    if (iconButton("assets/refresh", Icon::Refresh, false, "Rescan the project folder", 0, button))
        state.project->refresh();
    ImGui::SameLine();
    ImGui::BeginDisabled(state.playing());
    if (iconButton("assets/newscene", Icon::Plus, false, "New scene", 0, button))
        showDialog(state, DialogKind::NewScene);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (iconButton("assets/import", Icon::Copy, false,
                   "Import images and sounds into the project (or drop files on the window)", 0,
                   button))
        state.chooseAssetsToImport(nullptr);

    const Node tree = buildTree(state.project->files(), state.assetFilter);
    const bool filtering = !state.assetFilter.empty();
    ImGui::BeginChild("##assettree");
    openScenesSection(state);
    if (tree.children.empty())
        ImGui::TextDisabled(filtering ? "Nothing matches." : "The project has no files yet.");
    // The project itself is the root of the tree.
    ImGui::PushID("root");
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4.0F, rowPadding()});
    const bool open = ImGui::TreeNodeEx(
        "##root",
        ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_FramePadding |
            (state.explorerFolder.empty() ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None));
    ImGui::PopStyleVar();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        state.explorerFolder.clear();
        state.explorerTarget.clear();
    }
    markItem("assets/folder/");
    ImGui::SameLine(0.0F, 2.0F);
    ImGui::PushStyleColor(ImGuiCol_Text, imColor(vs::textBright));
    iconLabel(Icon::Folder, state.project->project().name.c_str(), packed(vs::textBright));
    ImGui::PopStyleColor();
    if (open) {
        for (const Node &child : tree.children)
            drawNode(state, child, filtering);
        ImGui::TreePop();
    }
    ImGui::PopID();
    ImGui::EndChild();
    state.explorerReveal.clear(); // Whatever was asked for has been shown (or does not exist).

    // F2 and Delete act on the row that was clicked last, when the Explorer has the keyboard.
    state.explorerFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (state.explorerFocused && !state.playing() && state.dialog.kind == DialogKind::None &&
        !ImGui::GetIO().WantTextInput && !state.explorerTarget.empty()) {
        const auto found = std::find_if(
            state.project->files().begin(), state.project->files().end(),
            [&](const AssetEntry &entry) { return entry.path == state.explorerTarget; });
        const bool isFile = found != state.project->files().end();
        const bool movable = !isFile || found->kind != AssetKind::TextureMeta;
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && movable)
            state.askToMoveAsset(state.explorerTarget);
        else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            state.askToDeleteAsset(state.explorerTarget);
    }
}
} // namespace yk::editor::ui
