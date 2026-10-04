#include "ui/Panels.hpp"
#include "yk/core/AppPaths.hpp"
#include <SDL3/SDL.h>
#include <algorithm>

namespace yk::editor::ui {
namespace {
// Bundled sample projects may live in an app, an installation, or the checkout.
std::optional<std::filesystem::path> findBundledProject(const char *folder) {
    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path here = yk::executableDirectory();
    if (const auto resources = yk::bundleResourcesFor(here); !resources.empty())
        candidates.push_back(resources / folder);
    candidates.push_back(here / folder);
    candidates.push_back(here / ".." / folder);
    candidates.push_back(here / ".." / ".." / folder);
    candidates.push_back(here / ".." / "share" / "yk-engine" / folder);
    candidates.push_back(folder);
    for (const auto &candidate : candidates) {
        std::error_code error;
        if (std::filesystem::exists(candidate / Project::fileName, error))
            return std::filesystem::weakly_canonical(candidate, error);
    }
    return std::nullopt;
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
    if (const auto demo = findBundledProject("YK-DemoGame"))
        if (startAction(
                "welcome/Open Sample", Icon::Play, "Open the Demo Game",
                "Cinder Vale: a two-player co-op puzzle level built from the engine's components"))
            state.openProject(*demo);
    if (const auto exploration = findBundledProject("YK-ExplorationDemo"))
        if (startAction("welcome/Open Exploration Sample", Icon::Play, "Open the Exploration Demo",
                        "Castle Paths: NPCs, dialogue, gates and connected top-down maps"))
            state.openProject(*exploration);
    if (const auto fireboy = findBundledProject("Fireboy-Watergirl-Demo"))
        if (startAction("welcome/Open Fireboy Sample", Icon::Play, "Open Fireboy and Watergirl",
                        "Pixel-art co-op platformer with switches, hazards and two exits"))
            state.openProject(*fireboy);

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
