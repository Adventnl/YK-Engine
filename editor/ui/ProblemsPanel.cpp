#include "ui/Panels.hpp"
#include "yk/core/Log.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <algorithm>

namespace yk::editor::ui {
namespace {
// Shows what a problem is about: a scene opens in a tab with the entity selected and framed, the
// project file opens its settings.
void reveal(EditorState &state, const ProjectIssue &issue) {
    if (issue.path == Project::fileName) {
        showDialog(state, DialogKind::ProjectSettings);
    } else if (issue.path.ends_with(sceneExtension)) {
        if (!state.openScene(issue.path))
            return;
        if (issue.entity && state.document && state.document->path() == issue.path &&
            state.document->scene().find(issue.entity)) {
            state.document->select(issue.entity);
            state.interaction.frameSelection();
        }
    } else {
        state.showAssetInExplorer(issue.path);
    }
}
} // namespace

void problemsPanel(EditorState &state) {
    if (!state.project) {
        ImGui::TextDisabled("No project is open.");
        return;
    }
    static bool showErrors = true, showWarnings = true;
    std::size_t errors = 0, warnings = 0;
    for (const ProjectIssue &issue : state.problems)
        (issue.severity == ProjectIssue::Severity::Error ? errors : warnings)++;

    if (iconButton("problems/refresh", Icon::Refresh, false, "Check the whole project again", 0,
                   24.0F))
        state.refreshProblems();
    ImGui::SameLine(0.0F, 6.0F);
    const auto toggle = [&](const char *id, Icon icon, Color tone, std::size_t count, bool &value) {
        ImGui::PushID(id);
        const std::string text = std::to_string(count);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float width = 46.0F;
        if (ImGui::InvisibleButton("##toggle", {width, 22.0F}))
            value = !value;
        markItem(std::string("problems/") + id);
        ImDrawList &list = *ImGui::GetWindowDrawList();
        if (value)
            list.AddRectFilled(at, {at.x + width, at.y + 22.0F}, IM_COL32(255, 255, 255, 16), 3.0F);
        drawIcon(list, icon, {at.x + 12.0F, at.y + 11.0F}, 14.0F,
                 packed(value ? tone : vs::textDim));
        list.AddText({at.x + 24.0F, at.y + 3.0F}, packed(value ? vs::text : vs::textDim),
                     text.c_str());
        ImGui::PopID();
    };
    toggle("errors", Icon::Error, vs::error, errors, showErrors);
    ImGui::SameLine(0.0F, 4.0F);
    toggle("warnings", Icon::Warning, vs::warning, warnings, showWarnings);
    ImGui::SameLine(0.0F, 12.0F);
    ImGui::AlignTextToFramePadding();
    if (!state.problemsChecked)
        ImGui::TextDisabled("Not checked yet.");
    else if (state.problems.empty())
        ImGui::TextColored(imColor(vs::success), "No problems found in the project.");

    ImGui::BeginChild("##problems", {0.0F, 0.0F}, ImGuiChildFlags_None);
    int index = 0;
    for (const ProjectIssue &issue : state.problems) {
        const bool isError = issue.severity == ProjectIssue::Severity::Error;
        if ((isError && !showErrors) || (!isError && !showWarnings))
            continue;
        ImGui::PushID(index++);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const bool clicked =
            ImGui::Selectable("##issue", false, ImGuiSelectableFlags_None, {width, 22.0F});
        markItem("problems/row/" + std::to_string(index - 1));
        tooltip(issue.path + ": " + issue.message);
        ImDrawList &list = *ImGui::GetWindowDrawList();
        drawIcon(list, isError ? Icon::Error : Icon::Warning, {at.x + 12.0F, at.y + 11.0F}, 14.0F,
                 packed(isError ? vs::error : vs::warning));
        list.AddText({at.x + 26.0F, at.y + 3.0F}, packed(vs::text), issue.message.c_str());
        const float textWidth = ImGui::CalcTextSize(issue.message.c_str()).x;
        list.AddText({at.x + 26.0F + textWidth + 12.0F, at.y + 3.0F}, packed(vs::textDim),
                     issue.path.c_str());
        ImGui::PopID();
        if (clicked)
            reveal(state, issue);
    }
    ImGui::EndChild();
}

void outputPanel(EditorState &state) {
    if (iconButton("output/clear", Icon::Trash, false, "Clear the output", 0, 24.0F))
        state.output.clear();
    ImGui::SameLine(0.0F, 6.0F);
    if (iconButton("output/copy", Icon::Copy, false, "Copy everything to the clipboard", 0,
                   24.0F)) {
        std::string all;
        for (const OutputLine &line : state.output)
            all += line.text + "\n";
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine(0.0F, 12.0F);
    ImGui::AlignTextToFramePadding();
    if (state.output.empty())
        ImGui::TextDisabled(
            "Nothing has been built yet. Exports, validation and player runs report here.");
    ImGui::BeginChild("##output", {0.0F, 0.0F}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0F;
    ImGui::PushFont(fonts().mono, 12.5F);
    for (const OutputLine &line : state.output) {
        const Color tone = line.level == LogLevel::Error     ? vs::error
                           : line.level == LogLevel::Warning ? vs::warning
                                                             : vs::text;
        ImGui::TextColored(imColor(tone), "%s", line.text.c_str());
    }
    ImGui::PopFont();
    if (atBottom && !state.output.empty())
        ImGui::SetScrollHereY(1.0F);
    ImGui::EndChild();
}
} // namespace yk::editor::ui
