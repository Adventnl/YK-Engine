#include "ui/Panels.hpp"
#include "yk/core/AppPaths.hpp"
#include <SDL3/SDL.h>
#include <algorithm>

namespace yk::editor::ui {
namespace {
// Project discovery reads ordinary project manifests. The editor knows no game names.
struct AvailableProject {
    std::filesystem::path root;
    std::string name;
};
std::vector<AvailableProject> availableProjects() {
    const auto here = yk::executableDirectory();
    std::vector<std::filesystem::path> roots;
    if (const auto resources = yk::bundleResourcesFor(here); !resources.empty())
        roots.push_back(resources / "games");
    roots.insert(roots.end(), {here / "games", here / ".." / "games", here / ".." / ".." / "games",
                               here / ".." / "share" / "yk-engine" / "games",
                               std::filesystem::current_path() / "games"});
    std::vector<AvailableProject> projects;
    std::vector<std::string> folders;
    for (const auto &root : roots) {
        std::error_code error;
        std::filesystem::directory_iterator entries(root, error);
        for (auto it = entries; !error && it != std::filesystem::directory_iterator{};
             it.increment(error)) {
            const auto folder = it->path().filename().string();
            if (std::find(folders.begin(), folders.end(), folder) != folders.end())
                continue;
            if (!std::filesystem::is_regular_file(it->path() / Project::fileName, error)) {
                error.clear();
                continue;
            }
            auto project = Project::load(it->path());
            if (project) {
                folders.push_back(folder);
                projects.push_back({project.value().root, project.value().name});
            }
        }
    }
    std::sort(projects.begin(), projects.end(),
              [](const auto &a, const auto &b) { return a.name < b.name; });
    return projects;
}

// A link-style action row: an icon, a title and a description, like VS Code's start page.
bool startAction(const char *id, Icon icon, const char *title, const char *description) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::min(ImGui::GetContentRegionAvail().x, 460.0F);
    const float height = 44.0F;
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##start", {width, height});
    const bool hovered = ImGui::IsItemHovered();
    markItem(id);
    ImDrawList &list = *ImGui::GetWindowDrawList();
    if (hovered)
        list.AddRectFilled(origin, {origin.x + width, origin.y + height}, packed(vs::listHover),
                           4.0F);
    drawIcon(list, icon, {origin.x + 22.0F, origin.y + height * 0.5F}, 20.0F, packed(vs::focus));
    list.AddText(fonts().semibold, 14.0F, {origin.x + 44.0F, origin.y + 6.0F}, packed(vs::text),
                 title);
    list.AddText(fonts().ui, 12.5F, {origin.x + 44.0F, origin.y + 24.0F}, packed(vs::textDim),
                 description);
    ImGui::PopID();
    return clicked;
}
} // namespace

void gamesMenu(EditorState &state) {
    const auto projects = availableProjects();
    if (projects.empty())
        return;
    const bool open = ImGui::BeginMenu("Games");
    markItem("menu/Games");
    if (open) {
        for (const auto &project : projects) {
            if (ImGui::MenuItem(project.name.c_str()))
                state.guarded([&state, root = project.root] { state.openProject(root); });
            markItem("menu/Games/" + project.root.filename().string());
        }
        ImGui::EndMenu();
    }
}

void welcomePage(EditorState &state) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImDrawList &list = *ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    list.AddRectFilled(origin, {origin.x + avail.x, origin.y + avail.y}, packed(vs::editorBg));
    const float left = origin.x + std::max(32.0F, (avail.x - 720.0F) * 0.5F);
    ImGui::SetCursorScreenPos({left, origin.y + std::max(24.0F, avail.y * 0.14F)});
    ImGui::BeginGroup();
    ImGui::PushFont(fonts().semibold, 30.0F);
    ImGui::TextUnformatted("YK Engine");
    ImGui::PopFont();
    ImGui::TextColored(imColor(vs::textDim), "A 2D game engine and editor");
    ImGui::Dummy({1.0F, 22.0F});

    ImGui::PushFont(fonts().semibold, 16.0F);
    ImGui::TextUnformatted("Start");
    ImGui::PopFont();
    ImGui::Dummy({1.0F, 4.0F});
    if (startAction("welcome/New Project", Icon::Plus, "New Project...",
                    "Create an empty project with a first scene"))
        showDialog(state, DialogKind::NewProject);
    if (startAction("welcome/Open Project", Icon::FolderOpen, "Open Project...",
                    "Open a folder that holds a project.ykproj"))
        showDialog(state, DialogKind::OpenProject);
    const auto projects = availableProjects();
    if (!projects.empty()) {
        ImGui::Dummy({1.0F, 14.0F});
        ImGui::PushFont(fonts().semibold, 16.0F);
        ImGui::TextUnformatted("Demo games");
        ImGui::PopFont();
        for (const auto &project : projects) {
            const auto id = "welcome/game/" + project.root.filename().string();
            const auto title = "Open " + project.name;
            if (startAction(id.c_str(), Icon::Play, title.c_str(),
                            "Open project; press F5 to play"))
                state.openProject(project.root);
        }
    }

    if (!state.recent.paths().empty()) {
        ImGui::Dummy({1.0F, 18.0F});
        ImGui::PushFont(fonts().semibold, 16.0F);
        ImGui::TextUnformatted("Recent");
        ImGui::PopFont();
        ImGui::Dummy({1.0F, 4.0F});
        const std::vector<std::string> recent = state.recent.paths();
        for (const std::string &path : recent) {
            const std::filesystem::path folder(path);
            ImGui::PushID(path.c_str());
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton(
                "##recent", {std::min(ImGui::GetContentRegionAvail().x, 560.0F), 24.0F});
            const bool hovered = ImGui::IsItemHovered();
            markItem("welcome/recent/" + path);
            if (hovered)
                list.AddRectFilled(at, {at.x + std::min(avail.x, 560.0F), at.y + 24.0F},
                                   packed(vs::listHover), 3.0F);
            list.AddText(fonts().ui, 14.0F, {at.x + 8.0F, at.y + 4.0F}, packed(vs::link),
                         folder.filename().string().c_str());
            list.AddText(fonts().ui, 12.5F, {at.x + 8.0F + 160.0F, at.y + 5.0F},
                         packed(vs::textDim), path.c_str());
            ImGui::PopID();
            if (clicked) {
                state.openProject(path);
                break;
            }
        }
    }
    ImGui::Dummy({1.0F, 22.0F});
    ImGui::PushFont(fonts().mono, 12.0F);
    ImGui::TextColored(
        imColor(vs::textFaint), "%s",
        shortcutText(
            "Ctrl+O open   Ctrl+Shift+B build   F5 play   Ctrl+J panel   Ctrl+B side bar"));
    ImGui::PopFont();
    ImGui::EndGroup();
}
} // namespace yk::editor::ui
