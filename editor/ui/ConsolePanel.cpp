#include "ui/Panels.hpp"
#include <algorithm>
#include <cctype>

namespace yk::editor::ui {
void consolePanel(EditorState &state) {
    if (!state.showConsole)
        return;
    if (!ImGui::Begin("Console", &state.showConsole)) {
        ImGui::End();
        return;
    }
    markWindow("panel/Console");
    static bool showInfo = true, showWarnings = true, showErrors = true;
    static std::string filter;
    static std::vector<ConsoleLog::Entry> cache;
    static std::uint64_t cachedVersion = ~std::uint64_t{0};
    const std::uint64_t version = state.console.version();
    if (version != cachedVersion) {
        cache = state.console.snapshot();
        cachedVersion = version;
    }

    if (ImGui::Button("Clear"))
        state.console.clear();
    markItem("console/clear");
    ImGui::SameLine();
    const auto levelToggle = [&](const char *id, Icon icon, Color tone, bool &value,
                                 LogLevel level) {
        const std::size_t count = state.console.count(level);
        ImGui::PushID(id);
        ImGui::PushStyleColor(ImGuiCol_Text, imColor(value ? tone : palette::dim));
        ImGui::PushStyleColor(ImGuiCol_Button,
                              imColor(value ? Color{52, 58, 74, 255} : Color{36, 39, 48, 255}));
        const std::string text = std::to_string(count);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        if (ImGui::Button((std::string("    ") + text + "##toggle").c_str()))
            value = !value;
        drawIcon(*ImGui::GetWindowDrawList(), icon,
                 {start.x + 13.0F, start.y + ImGui::GetFrameHeight() * 0.5F}, 13.0F,
                 packed(value ? tone : palette::dim));
        markItem(std::string("console/") + id);
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    };
    levelToggle("info", Icon::Info, palette::info, showInfo, LogLevel::Info);
    ImGui::SameLine();
    levelToggle("warnings", Icon::Warning, palette::warning, showWarnings, LogLevel::Warning);
    ImGui::SameLine();
    levelToggle("errors", Icon::Error, palette::error, showErrors, LogLevel::Error);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0F);
    inputText("##consolefilter", filter, 0, "Filter");

    ImGui::BeginChild("##log", {0.0F, 0.0F}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0F;
    std::vector<const ConsoleLog::Entry *> rows;
    rows.reserve(cache.size());
    for (const ConsoleLog::Entry &entry : cache) {
        const bool levelShown = (entry.level == LogLevel::Info && showInfo) ||
                                (entry.level == LogLevel::Warning && showWarnings) ||
                                (entry.level == LogLevel::Error && showErrors);
        if (!levelShown)
            continue;
        if (!filter.empty()) {
            const std::string text = entry.subsystem + " " + entry.message;
            const auto found = std::search(text.begin(), text.end(), filter.begin(), filter.end(),
                                           [](char a, char b) {
                                               return std::tolower(static_cast<unsigned char>(a)) ==
                                                      std::tolower(static_cast<unsigned char>(b));
                                           });
            if (found == text.end())
                continue;
        }
        rows.push_back(&entry);
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const ConsoleLog::Entry &entry = *rows[static_cast<std::size_t>(i)];
            const Color tone = entry.level == LogLevel::Error     ? palette::error
                               : entry.level == LogLevel::Warning ? palette::warning
                                                                  : Color{190, 196, 210, 255};
            const Icon icon = entry.level == LogLevel::Error     ? Icon::Error
                              : entry.level == LogLevel::Warning ? Icon::Warning
                                                                 : Icon::Info;
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Text, imColor(tone));
            iconLabel(icon, "", packed(tone));
            ImGui::SameLine(0.0F, 0.0F);
            ImGui::TextColored(imColor(palette::dim), "[%s]", entry.subsystem.c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(entry.message.c_str());
            ImGui::PopStyleColor();
            if (ImGui::BeginPopupContextItem("row_menu")) {
                if (ImGui::MenuItem("Copy message"))
                    ImGui::SetClipboardText(entry.message.c_str());
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    if (atBottom && !rows.empty())
        ImGui::SetScrollHereY(1.0F);
    ImGui::EndChild();
    ImGui::End();
}
} // namespace yk::editor::ui
