#include "ui/UiCommon.hpp"
#include "imgui_internal.h"
#include "ui/Codicons.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

namespace yk::editor::ui {
namespace {
ImGuiID editOwner = 0;

} // namespace

const char *glyphOf(Icon icon) {
    switch (icon) {
    case Icon::Move:
        return codicon::move;
    case Icon::Resize:
        return codicon::expandAll;
    case Icon::Rotate:
        return codicon::refresh;
    case Icon::Play:
        return codicon::debugStart;
    case Icon::Pause:
        return codicon::debugPause;
    case Icon::Step:
        return codicon::debugStepOver;
    case Icon::Stop:
        return codicon::debugStop;
    case Icon::Restart:
        return codicon::debugRestart;
    case Icon::Snap:
        return codicon::magnet;
    case Icon::Grid:
        return codicon::editorLayout;
    case Icon::Plus:
        return codicon::add;
    case Icon::Cross:
        return codicon::close;
    case Icon::Search:
        return codicon::search;
    case Icon::Eye:
        return codicon::eye;
    case Icon::EyeOff:
        return codicon::eyeClosed;
    case Icon::Link:
        return codicon::link;
    case Icon::Folder:
        return codicon::folder;
    case Icon::FolderOpen:
        return codicon::folderOpened;
    case Icon::File:
        return codicon::fileText;
    case Icon::Scene:
        return codicon::symbolNamespace;
    case Icon::Prefab:
        return codicon::package;
    case Icon::Image:
        return codicon::fileMedia;
    case Icon::Sound:
        return codicon::unmute;
    case Icon::Entity:
        return codicon::symbolMisc;
    case Icon::Warning:
        return codicon::warning;
    case Icon::Error:
        return codicon::error;
    case Icon::Info:
        return codicon::info;
    case Icon::Dot:
        return codicon::circleSmallFilled;
    case Icon::Save:
        return codicon::save;
    case Icon::SaveAll:
        return codicon::saveAll;
    case Icon::Undo:
        return codicon::discard;
    case Icon::Redo:
        return codicon::redo;
    case Icon::Target:
        return codicon::target;
    case Icon::Explorer:
        return codicon::files;
    case Icon::Hierarchy:
        return codicon::listTree;
    case Icon::Prefabs:
        return codicon::package;
    case Icon::Components:
        return codicon::extensions;
    case Icon::Build:
        return codicon::rocket;
    case Icon::Settings:
        return codicon::settingsGear;
    case Icon::SidebarLeft:
        return codicon::layoutSidebarLeft;
    case Icon::SidebarLeftOff:
        return codicon::layoutSidebarLeftOff;
    case Icon::Panel:
        return codicon::layoutPanel;
    case Icon::PanelOff:
        return codicon::layoutPanelOff;
    case Icon::SidebarRight:
        return codicon::layoutSidebarRight;
    case Icon::SidebarRightOff:
        return codicon::layoutSidebarRightOff;
    case Icon::Lock:
        return codicon::lock;
    case Icon::Unlock:
        return codicon::unlock;
    case Icon::ChevronRight:
        return codicon::chevronRight;
    case Icon::ChevronDown:
        return codicon::chevronDown;
    case Icon::ChevronUp:
        return codicon::chevronUp;
    case Icon::More:
        return codicon::ellipsis;
    case Icon::Trash:
        return codicon::trash;
    case Icon::Copy:
        return codicon::copy;
    case Icon::Paste:
        return codicon::clippy;
    case Icon::Edit:
        return codicon::edit;
    case Icon::Filter:
        return codicon::filter;
    case Icon::Refresh:
        return codicon::refresh;
    case Icon::Output:
        return codicon::output;
    case Icon::Console:
        return codicon::debugConsole;
    case Icon::Profiler:
        return codicon::pulse;
    case Icon::Check:
        return codicon::check;
    case Icon::Tag:
        return codicon::tag;
    case Icon::Layers:
        return codicon::layers;
    case Icon::ZoomIn:
        return codicon::zoomIn;
    case Icon::ZoomOut:
        return codicon::zoomOut;
    case Icon::Split:
        return codicon::splitHorizontal;
    case Icon::Animation:
        return codicon::play;
    case Icon::Code:
        return codicon::fileCode;
    case Icon::Data:
        return codicon::fileBinary;
    }
    return codicon::question;
}

void drawIcon(ImDrawList &list, Icon icon, ImVec2 center, float size, ImU32 color) {
    ImFont *font = fonts().ui;
    if (!font)
        return;
    const char *glyph = glyphOf(icon);
    const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0F, glyph);
    // ImGui sizes a font by its line height and puts the baseline of Inter (the font the Codicons
    // are merged into) at 80% of it, while a Codicons glyph is centered on its own box: centering
    // the line box would draw every icon 0.2 x size too high. Measured from the two fonts' tables.
    constexpr float baselineFraction = 0.8F;
    list.AddText(font, size,
                 {std::floor(center.x - extent.x * 0.5F),
                  std::floor(center.y - (baselineFraction - 0.5F) * size)},
                 color, glyph);
}

bool iconButton(const char *id, Icon icon, bool active, const char *tip, ImU32 tint, float size) {
    ImGui::PushID(id);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##icon", {size, size});
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const float alpha = ImGui::GetStyle().Alpha;
    ImDrawList &list = *ImGui::GetWindowDrawList();
    if (held || hovered || active) {
        const Color fill = held     ? Color{90, 93, 94, 120}
                           : active ? Color{99, 102, 103, 90}
                                    : Color{90, 93, 94, 80};
        list.AddRectFilled(origin, {origin.x + size, origin.y + size}, faded(fill, alpha), 5.0F);
    }
    ImU32 glyph = tint != 0 ? tint : packed(active || hovered ? vs::textBright : vs::text);
    if (tint == 0 && !active && !hovered)
        glyph = packed(Color{190, 190, 190, 255});
    drawIcon(list, icon, {origin.x + size * 0.5F, origin.y + size * 0.5F}, size * 0.62F, glyph);
    if (hovered && tip && *tip)
        ImGui::SetTooltip("%s", shortcutText(tip));
    markItem(id);
    ImGui::PopID();
    return pressed;
}

void iconLabel(Icon icon, const char *text, ImU32 tint) {
    const float height = ImGui::GetTextLineHeight();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    // A label that continues a row (a tree node with frame padding) sits lower than the line's top:
    // the icon follows the text, not the top of the row.
    origin.y += ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;
    drawIcon(*ImGui::GetWindowDrawList(), icon,
             {origin.x + height * 0.5F, origin.y + height * 0.5F}, height * 0.95F,
             tint != 0 ? tint : ImGui::GetColorU32(ImGuiCol_TextDisabled));
    ImGui::Dummy({height, height});
    ImGui::SameLine(0.0F, 4.0F);
    ImGui::TextUnformatted(text);
}

const char *shortcutText(const char *text) {
#if defined(__APPLE__)
    // A ring of buffers so several labels can be alive in one frame.
    static std::string ring[32];
    static std::size_t next = 0;
    std::string &out = ring[next++ % 32];
    out = text;
    const auto replaceAll = [&out](const std::string &from, const std::string &to) {
        for (std::size_t at = out.find(from); at != std::string::npos;
             at = out.find(from, at + to.size()))
            out.replace(at, from.size(), to);
    };
    replaceAll("Ctrl+", "Cmd+");
    replaceAll("Alt+", "Option+");
    replaceAll("Cmd+Tab", "Control+Tab"); // Command+Tab is the system's; the editor uses Control.
    replaceAll("Cmd+Shift+Tab", "Control+Shift+Tab");
    return out.c_str();
#else
    return text;
#endif
}

float displayScale() {
    return std::max(1.0F, ImGui::GetStyle().FontScaleDpi);
}

bool pillTab(const char *id, const char *label, bool active, float height, const std::string &badge,
             Color badgeColor) {
    ImFont *font = active ? fonts().semibold : fonts().ui;
    ImGui::PushFont(font, 0.0F);
    const float padding = dp(10.0F);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const float badgeWidth = badge.empty() ? 0.0F : dp(24.0F);
    const ImVec2 size{textSize.x + 2.0F * padding + badgeWidth, height};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##pilltab", size);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    markItem(id);
    ImDrawList &list = *ImGui::GetWindowDrawList();
    if (active || hovered)
        list.AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                           packed(active ? vs::pill : vs::pillHover), dp(metrics::controlRadius));
    const ImU32 color = packed(active || hovered ? vs::textBright : vs::textDim);
    list.AddText({origin.x + padding, origin.y + (size.y - textSize.y) * 0.5F}, color, label);
    if (!badge.empty()) {
        const float w = dp(16.0F), h = dp(14.0F);
        const ImVec2 pill{origin.x + padding + textSize.x + dp(6.0F),
                          origin.y + (size.y - h) * 0.5F};
        list.AddRectFilled(pill, {pill.x + w, pill.y + h}, packed(badgeColor), h * 0.5F);
        ImGui::PushFont(fonts().ui, 10.5F);
        const ImVec2 badgeSize = ImGui::CalcTextSize(badge.c_str());
        list.AddText({pill.x + (w - badgeSize.x) * 0.5F, pill.y + (h - badgeSize.y) * 0.5F},
                     IM_COL32(20, 20, 20, 255), badge.c_str());
        ImGui::PopFont();
    }
    ImGui::PopFont();
    ImGui::SameLine(0.0F, dp(2.0F));
    return clicked;
}

bool sectionHeader(const char *id, const char *title, bool &open, bool separator) {
    const float height = dp(28.0F);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##section", {width, height});
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    markItem(id);
    if (clicked)
        open = !open;
    ImDrawList &list = *ImGui::GetWindowDrawList();
    if (separator)
        list.AddLine({origin.x, origin.y + 0.5F}, {origin.x + width, origin.y + 0.5F},
                     packed(vs::border));
    if (hovered)
        list.AddRectFilled({origin.x, origin.y + 1.0F}, {origin.x + width, origin.y + height},
                           packed(vs::listHover));
    drawIcon(list, open ? Icon::ChevronDown : Icon::ChevronRight,
             {origin.x + dp(11.0F), origin.y + height * 0.5F}, dp(14.0F), packed(vs::textDim));
    ImGui::PushFont(fonts().semibold, 0.0F);
    const ImVec2 textSize = ImGui::CalcTextSize(title);
    list.AddText({origin.x + dp(22.0F), origin.y + (height - textSize.y) * 0.5F}, packed(vs::text),
                 title);
    ImGui::PopFont();
    return open;
}

void imageInCard(const ImTextureRef &texture, ImVec2 size, ImVec2 uv1) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImGui::GetWindowDrawList()->AddImageRounded(
        texture, origin, {origin.x + size.x, origin.y + size.y}, {0.0F, 0.0F}, uv1, IM_COL32_WHITE,
        dp(metrics::cardRadius - 1.0F), ImDrawFlags_RoundCornersBottom);
}

PopupLook::PopupLook() {
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, imColor(Color{58, 60, 63, 255}));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, imColor(Color{70, 72, 75, 255}));
    ImGui::PushStyleColor(ImGuiCol_Text, imColor(vs::text));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8.0F, 5.0F});
}
PopupLook::~PopupLook() {
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

void WidgetRegistry::beginFrame() {
    previous_.swap(current_);
    current_.clear();
}
void WidgetRegistry::mark(const std::string &id, Rect screenRect, bool visible) {
    if (enabled_)
        current_[id] = {screenRect, visible};
}
std::optional<Rect> WidgetRegistry::find(const std::string &id) const {
    const auto found = previous_.find(id);
    if (found == previous_.end())
        return std::nullopt;
    return found->second.rect;
}
bool WidgetRegistry::visible(const std::string &id) const {
    const auto found = previous_.find(id);
    return found != previous_.end() && found->second.visible;
}
std::vector<std::string> WidgetRegistry::ids() const {
    std::vector<std::string> names;
    names.reserve(previous_.size());
    for (const auto &[name, rect] : previous_)
        names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}
WidgetRegistry &widgets() {
    static WidgetRegistry registry;
    return registry;
}
void markItem(const std::string &id) {
    if (!widgets().enabled())
        return;
    const ImVec2 low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
    widgets().mark(id, {vec(low), vec(high) - vec(low)}, ImGui::IsItemVisible());
    if (widgets().revealRequest() == id) {
        ImGui::SetScrollHereY(0.5F);
        widgets().clearReveal();
    }
}
void markPair(const std::string &id) {
    if (!widgets().enabled())
        return;
    const ImVec2 low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    const float half = (high.x - low.x - gap) * 0.5F;
    const bool visible = ImGui::IsItemVisible();
    widgets().mark(id, {vec(low), vec(high) - vec(low)}, visible);
    widgets().mark(id + "/x", {vec(low), {half, high.y - low.y}}, visible);
    widgets().mark(id + "/y", {{low.x + half + gap, low.y}, {half, high.y - low.y}}, visible);
    if (widgets().revealRequest() == id || widgets().revealRequest() == id + "/x" ||
        widgets().revealRequest() == id + "/y") {
        ImGui::SetScrollHereY(0.5F);
        widgets().clearReveal();
    }
}
void markWindow(const std::string &id) {
    if (!widgets().enabled())
        return;
    widgets().mark(id, {vec(ImGui::GetWindowPos()), vec(ImGui::GetWindowSize())});
}

void commitEdit(EditorDocument &document, const std::string &changeLabel, bool changed,
                const std::function<void()> &apply) {
    const ImGuiID id = ImGui::GetItemID();
    if (id == 0)
        return; // The widget was skipped (clipped away), so there is nothing to track.
    if (ImGui::IsItemActivated() && !document.inChange()) {
        document.beginChange(changeLabel);
        editOwner = id;
    }
    if (changed) {
        if (!document.inChange()) {
            document.beginChange(changeLabel);
            editOwner = id;
        }
        apply();
    }
    if (document.inChange() && editOwner == id && !ImGui::IsItemActive()) {
        document.endChange();
        editOwner = 0;
    }
}

void settleEdits(EditorDocument &document, bool gizmoDragging) {
    if (!document.inChange() || gizmoDragging) {
        if (!document.inChange())
            editOwner = 0;
        return;
    }
    if (editOwner != 0 && ImGui::GetActiveID() != editOwner) {
        document.endChange();
        editOwner = 0;
    }
}

namespace {
int growString(ImGuiInputTextCallbackData *data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto *value = static_cast<std::string *>(data->UserData);
        value->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = value->data();
    }
    return 0;
}
} // namespace

bool inputText(const char *labelText, std::string &value, ImGuiInputTextFlags flags,
               const char *hint) {
    flags |= ImGuiInputTextFlags_CallbackResize;
    if (hint)
        return ImGui::InputTextWithHint(labelText, hint, value.data(), value.capacity() + 1, flags,
                                        growString, &value);
    return ImGui::InputText(labelText, value.data(), value.capacity() + 1, flags, growString,
                            &value);
}

bool enterPressedInField() {
    return ImGui::IsItemDeactivated() && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                                          ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
}

bool inputTextMultiline(const char *labelText, std::string &value, ImVec2 size,
                        ImGuiInputTextFlags flags) {
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputTextMultiline(labelText, value.data(), value.capacity() + 1, size, flags,
                                     growString, &value);
}

std::string label(const std::string &identifier) {
    return prettifyName(identifier);
}

bool hoveredForTooltip() {
    return ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                                ImGuiHoveredFlags_AllowWhenDisabled);
}
void tooltip(const std::string &text) {
    if (!text.empty() && hoveredForTooltip())
        ImGui::SetTooltip("%s", shortcutText(text.c_str()));
}

void nameCell(const std::string &text, const std::string &description) {
    const float available = ImGui::GetContentRegionAvail().x;
    std::string shown = text;
    const bool cut = ImGui::CalcTextSize(text.c_str()).x > available;
    while (cut && shown.size() > 1 && ImGui::CalcTextSize((shown + "...").c_str()).x > available)
        shown.pop_back();
    ImGui::TextUnformatted((cut ? shown + "..." : shown).c_str());
    tooltip(cut ? (description.empty() ? text : text + "\n" + description) : description);
}
} // namespace yk::editor::ui
