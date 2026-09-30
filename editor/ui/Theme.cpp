#include "ui/Theme.hpp"
#include "ui/UiCommon.hpp"
#include <cstddef>
#include <string>

// Font bytes generated from editor/resources/fonts by cmake/YkEmbed.cmake.
namespace yk::editor::ui::fontdata {
extern const unsigned char interRegular[];
extern const std::size_t interRegularSize;
extern const unsigned char interSemiBold[];
extern const std::size_t interSemiBoldSize;
extern const unsigned char jetBrainsMono[];
extern const std::size_t jetBrainsMonoSize;
extern const unsigned char codicon[];
extern const std::size_t codiconSize;
} // namespace yk::editor::ui::fontdata

namespace yk::editor::ui {
Fonts &fonts() {
    static Fonts instance;
    return instance;
}

void loadFonts(ImGuiIO &io) {
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false; // The bytes live in the executable.
    const auto add = [&](const unsigned char *data, std::size_t size, const char *name,
                         const ImFontConfig &cfg) {
        ImFontConfig copy = cfg;
        std::snprintf(copy.Name, sizeof copy.Name, "%s", name);
        return io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(data),
                                              static_cast<int>(size), 0.0F, &copy);
    };
    Fonts &set = fonts();
    set.ui = add(fontdata::interRegular, fontdata::interRegularSize, "Inter", config);
    // Codicons share the UI font, so a glyph can sit inline in any label and be drawn at any size.
    ImFontConfig icons = config;
    icons.MergeMode = true;
    icons.GlyphMinAdvanceX = 0.0F;
    add(fontdata::codicon, fontdata::codiconSize, "Codicons", icons);
    set.semibold =
        add(fontdata::interSemiBold, fontdata::interSemiBoldSize, "Inter SemiBold", config);
    set.mono = add(fontdata::jetBrainsMono, fontdata::jetBrainsMonoSize, "JetBrains Mono", config);
    io.FontDefault = set.ui;
}

void applyTheme() {
    ImGuiStyle &style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    // Compact and quiet: small radii on controls, no window borders (the workbench outlines its own
    // cards), thin scrollbars, 22 point rows.
    style.WindowRounding = 0.0F;
    style.ChildRounding = 0.0F;
    style.FrameRounding = 4.0F;
    style.PopupRounding = 6.0F;
    style.ScrollbarRounding = 4.0F;
    style.GrabRounding = 3.0F;
    style.TabRounding = metrics::controlRadius + 1.0F;
    style.WindowPadding = {8.0F, 6.0F};
    style.FramePadding = {6.0F, 3.0F};
    style.ItemSpacing = {6.0F, 3.0F};
    style.ItemInnerSpacing = {4.0F, 3.0F};
    style.CellPadding = {4.0F, 2.0F};
    style.IndentSpacing = 14.0F;
    style.ScrollbarSize = 9.0F;
    style.GrabMinSize = 8.0F;
    style.WindowBorderSize = 0.0F;
    style.ChildBorderSize = 0.0F;
    style.FrameBorderSize = 0.0F;
    style.PopupBorderSize = 1.0F;
    style.TabBorderSize = 0.0F;
    style.TabBarBorderSize = 0.0F;
    style.TabBarOverlineSize = 0.0F;
    style.SeparatorTextBorderSize = 1.0F;
    style.SeparatorTextPadding = {12.0F, 3.0F};
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.DisabledAlpha = 0.5F;
    style.HoverDelayNormal = 0.5F;
    style.HoverStationaryDelay = 0.2F;

    const auto set = [&](ImGuiCol slot, Color color) { style.Colors[slot] = imColor(color); };
    const Color clear{0, 0, 0, 0};
    set(ImGuiCol_Text, vs::text);
    set(ImGuiCol_TextDisabled, vs::textFaint);
    set(ImGuiCol_WindowBg, vs::chromeBg);
    set(ImGuiCol_ChildBg, clear);
    set(ImGuiCol_PopupBg, vs::raisedBg);
    set(ImGuiCol_Border, vs::menuBorder);
    set(ImGuiCol_BorderShadow, clear);
    set(ImGuiCol_FrameBg, vs::inputBg);
    set(ImGuiCol_FrameBgHovered, {46, 48, 50, 255});
    set(ImGuiCol_FrameBgActive, {52, 54, 56, 255});
    set(ImGuiCol_TitleBg, vs::chromeBg);
    set(ImGuiCol_TitleBgActive, vs::chromeBg);
    set(ImGuiCol_TitleBgCollapsed, vs::chromeBg);
    set(ImGuiCol_MenuBarBg, clear);
    set(ImGuiCol_ScrollbarBg, clear);
    set(ImGuiCol_ScrollbarGrab, vs::scrollbar);
    set(ImGuiCol_ScrollbarGrabHovered, vs::scrollbarHover);
    set(ImGuiCol_ScrollbarGrabActive, vs::scrollbarActive);
    set(ImGuiCol_CheckMark, vs::focus);
    set(ImGuiCol_SliderGrab, vs::focus);
    set(ImGuiCol_SliderGrabActive, {40, 150, 235, 255});
    set(ImGuiCol_Button, vs::secondaryBg);
    set(ImGuiCol_ButtonHovered, vs::secondaryHover);
    set(ImGuiCol_ButtonActive, {72, 74, 76, 255});
    set(ImGuiCol_Header, vs::listSelection);
    set(ImGuiCol_HeaderHovered, vs::listHover);
    set(ImGuiCol_HeaderActive, {52, 54, 56, 255});
    set(ImGuiCol_Separator, vs::border);
    set(ImGuiCol_SeparatorHovered, vs::focus);
    set(ImGuiCol_SeparatorActive, vs::focus);
    set(ImGuiCol_ResizeGrip, clear);
    set(ImGuiCol_ResizeGripHovered, vs::focus);
    set(ImGuiCol_ResizeGripActive, vs::focus);
    // Tabs: the strip is raised, the selected tab is the editor's own color, others are bare.
    set(ImGuiCol_Tab, clear);
    set(ImGuiCol_TabHovered, vs::pillHover);
    set(ImGuiCol_TabSelected, vs::editorBg);
    set(ImGuiCol_TabSelectedOverline, clear);
    set(ImGuiCol_TabDimmed, clear);
    set(ImGuiCol_TabDimmedSelected, vs::editorBg);
    set(ImGuiCol_TabDimmedSelectedOverline, clear);
    set(ImGuiCol_TextSelectedBg, {0, 120, 212, 110});
    set(ImGuiCol_DragDropTarget, {255, 196, 64, 230});
    set(ImGuiCol_NavCursor, vs::focus);
    set(ImGuiCol_ModalWindowDimBg, {0, 0, 0, 110});
    set(ImGuiCol_TableHeaderBg, vs::chromeBg);
    set(ImGuiCol_TableBorderStrong, vs::border);
    set(ImGuiCol_TableBorderLight, {36, 38, 40, 255});
    set(ImGuiCol_TableRowBg, clear);
    set(ImGuiCol_TableRowBgAlt, {255, 255, 255, 5});
}

std::string headerText(const std::string &text) {
    std::string upper = text;
    for (char &c : upper)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return upper;
}
} // namespace yk::editor::ui
