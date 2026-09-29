#include "ui/UiCommon.hpp"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>

namespace yk::editor::ui {
namespace {
ImGuiID editOwner = 0;

ImVec2 offsetBy(ImVec2 origin, float x, float y) {
    return {origin.x + x, origin.y + y};
}
} // namespace

void applyTheme() {
    ImGuiStyle &style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 0.0F;
    style.ChildRounding = 3.0F;
    style.FrameRounding = 3.0F;
    style.PopupRounding = 4.0F;
    style.ScrollbarRounding = 6.0F;
    style.GrabRounding = 3.0F;
    style.TabRounding = 3.0F;
    style.WindowPadding = {8.0F, 8.0F};
    style.FramePadding = {6.0F, 3.5F};
    style.ItemSpacing = {8.0F, 5.0F};
    style.ItemInnerSpacing = {6.0F, 4.0F};
    style.IndentSpacing = 16.0F;
    style.ScrollbarSize = 12.0F;
    style.GrabMinSize = 10.0F;
    style.WindowBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    style.PopupBorderSize = 1.0F;
    style.TabBarBorderSize = 1.0F;
    style.SeparatorTextBorderSize = 1.0F;
    style.WindowMenuButtonPosition = ImGuiDir_None;

    const auto set = [&](ImGuiCol slot, Color color) { style.Colors[slot] = imColor(color); };
    set(ImGuiCol_Text, {216, 220, 230, 255});
    set(ImGuiCol_TextDisabled, {120, 126, 140, 255});
    set(ImGuiCol_WindowBg, {28, 30, 37, 255});
    set(ImGuiCol_ChildBg, {28, 30, 37, 0});
    set(ImGuiCol_PopupBg, {32, 35, 43, 250});
    set(ImGuiCol_Border, {52, 56, 68, 255});
    set(ImGuiCol_BorderShadow, {0, 0, 0, 0});
    set(ImGuiCol_FrameBg, {40, 43, 53, 255});
    set(ImGuiCol_FrameBgHovered, {50, 55, 68, 255});
    set(ImGuiCol_FrameBgActive, {58, 64, 80, 255});
    set(ImGuiCol_TitleBg, {22, 24, 30, 255});
    set(ImGuiCol_TitleBgActive, {26, 28, 35, 255});
    set(ImGuiCol_TitleBgCollapsed, {22, 24, 30, 255});
    set(ImGuiCol_MenuBarBg, {22, 24, 30, 255});
    set(ImGuiCol_ScrollbarBg, {24, 26, 32, 255});
    set(ImGuiCol_ScrollbarGrab, {62, 67, 82, 255});
    set(ImGuiCol_ScrollbarGrabHovered, {80, 86, 104, 255});
    set(ImGuiCol_ScrollbarGrabActive, {98, 106, 128, 255});
    set(ImGuiCol_CheckMark, {110, 180, 240, 255});
    set(ImGuiCol_SliderGrab, {86, 156, 214, 255});
    set(ImGuiCol_SliderGrabActive, {120, 184, 236, 255});
    set(ImGuiCol_Button, {46, 51, 64, 255});
    set(ImGuiCol_ButtonHovered, {62, 70, 90, 255});
    set(ImGuiCol_ButtonActive, {76, 110, 156, 255});
    set(ImGuiCol_Header, {48, 56, 74, 255});
    set(ImGuiCol_HeaderHovered, {58, 70, 94, 255});
    set(ImGuiCol_HeaderActive, {66, 88, 124, 255});
    set(ImGuiCol_Separator, {52, 56, 68, 255});
    set(ImGuiCol_SeparatorHovered, {86, 156, 214, 255});
    set(ImGuiCol_SeparatorActive, {110, 180, 240, 255});
    set(ImGuiCol_ResizeGrip, {52, 56, 68, 160});
    set(ImGuiCol_ResizeGripHovered, {86, 156, 214, 200});
    set(ImGuiCol_ResizeGripActive, {110, 180, 240, 240});
    set(ImGuiCol_Tab, {34, 37, 46, 255});
    set(ImGuiCol_TabHovered, {62, 80, 112, 255});
    set(ImGuiCol_TabSelected, {46, 60, 86, 255});
    set(ImGuiCol_TabSelectedOverline, {86, 156, 214, 255});
    set(ImGuiCol_TabDimmed, {30, 32, 40, 255});
    set(ImGuiCol_TabDimmedSelected, {40, 46, 62, 255});
    set(ImGuiCol_TabDimmedSelectedOverline, {86, 156, 214, 120});
    set(ImGuiCol_DockingPreview, {86, 156, 214, 140});
    set(ImGuiCol_DockingEmptyBg, {22, 24, 30, 255});
    set(ImGuiCol_TextSelectedBg, {86, 156, 214, 100});
    set(ImGuiCol_DragDropTarget, {255, 196, 64, 230});
    set(ImGuiCol_NavCursor, {110, 180, 240, 200});
    set(ImGuiCol_ModalWindowDimBg, {0, 0, 0, 120});
    set(ImGuiCol_TableHeaderBg, {36, 40, 50, 255});
    set(ImGuiCol_TableBorderStrong, {52, 56, 68, 255});
    set(ImGuiCol_TableBorderLight, {42, 46, 56, 255});
    set(ImGuiCol_TableRowBgAlt, {255, 255, 255, 6});
}

void drawIcon(ImDrawList &list, Icon icon, ImVec2 c, float size, ImU32 color) {
    const float h = size * 0.5F;
    const float thick = std::max(1.5F, size * 0.09F);
    const auto line = [&](float x0, float y0, float x1, float y1) {
        list.AddLine(offsetBy(c, x0, y0), offsetBy(c, x1, y1), color, thick);
    };
    const auto triangle = [&](ImVec2 a, ImVec2 b, ImVec2 d) {
        list.AddTriangleFilled(a, b, d, color);
    };
    switch (icon) {
    case Icon::Move: {
        const float arm = h * 0.95F, head = h * 0.32F;
        line(-arm, 0, arm, 0);
        line(0, -arm, 0, arm);
        triangle(offsetBy(c, arm, 0), offsetBy(c, arm - head, -head),
                 offsetBy(c, arm - head, head));
        triangle(offsetBy(c, -arm, 0), offsetBy(c, -arm + head, -head),
                 offsetBy(c, -arm + head, head));
        triangle(offsetBy(c, 0, arm), offsetBy(c, -head, arm - head),
                 offsetBy(c, head, arm - head));
        triangle(offsetBy(c, 0, -arm), offsetBy(c, -head, -arm + head),
                 offsetBy(c, head, -arm + head));
        break;
    }
    case Icon::Resize: {
        const float box = h * 0.6F, knob = h * 0.2F;
        list.AddRect(offsetBy(c, -box, -box), offsetBy(c, box, box), color, 0.0F, 0, thick);
        for (const float sx : {-1.0F, 1.0F})
            for (const float sy : {-1.0F, 1.0F})
                list.AddRectFilled(offsetBy(c, sx * box - knob, sy * box - knob),
                                   offsetBy(c, sx * box + knob, sy * box + knob), color);
        break;
    }
    case Icon::Rotate:
    case Icon::Restart: {
        const float radius = h * 0.68F;
        list.PathArcTo(c, radius, -1.2F, 3.9F, 20);
        list.PathStroke(color, 0, thick);
        const float end = 3.9F;
        const ImVec2 tip = offsetBy(c, std::cos(end) * radius, std::sin(end) * radius);
        const ImVec2 along{-std::sin(end), std::cos(end)};
        const ImVec2 outward{std::cos(end), std::sin(end)};
        const float head = h * 0.42F;
        triangle(offsetBy(tip, along.x * head, along.y * head),
                 offsetBy(tip, -outward.x * head * 0.8F - along.x * head * 0.1F,
                          -outward.y * head * 0.8F - along.y * head * 0.1F),
                 offsetBy(tip, outward.x * head * 0.8F - along.x * head * 0.1F,
                          outward.y * head * 0.8F - along.y * head * 0.1F));
        break;
    }
    case Icon::Play:
        triangle(offsetBy(c, -h * 0.38F, -h * 0.66F), offsetBy(c, -h * 0.38F, h * 0.66F),
                 offsetBy(c, h * 0.7F, 0));
        break;
    case Icon::Pause:
        list.AddRectFilled(offsetBy(c, -h * 0.55F, -h * 0.6F), offsetBy(c, -h * 0.16F, h * 0.6F),
                           color);
        list.AddRectFilled(offsetBy(c, h * 0.16F, -h * 0.6F), offsetBy(c, h * 0.55F, h * 0.6F),
                           color);
        break;
    case Icon::Step:
        triangle(offsetBy(c, -h * 0.6F, -h * 0.6F), offsetBy(c, -h * 0.6F, h * 0.6F),
                 offsetBy(c, h * 0.3F, 0));
        list.AddRectFilled(offsetBy(c, h * 0.42F, -h * 0.6F), offsetBy(c, h * 0.68F, h * 0.6F),
                           color);
        break;
    case Icon::Stop:
        list.AddRectFilled(offsetBy(c, -h * 0.55F, -h * 0.55F), offsetBy(c, h * 0.55F, h * 0.55F),
                           color, 2.0F);
        break;
    case Icon::Grid: {
        const float box = h * 0.75F;
        for (const float t : {-box, 0.0F, box}) {
            line(t, -box, t, box);
            line(-box, t, box, t);
        }
        break;
    }
    case Icon::Plus:
        line(-h * 0.6F, 0, h * 0.6F, 0);
        line(0, -h * 0.6F, 0, h * 0.6F);
        break;
    case Icon::Cross:
        line(-h * 0.5F, -h * 0.5F, h * 0.5F, h * 0.5F);
        line(-h * 0.5F, h * 0.5F, h * 0.5F, -h * 0.5F);
        break;
    case Icon::Search:
        list.AddCircle(offsetBy(c, -h * 0.15F, -h * 0.15F), h * 0.55F, color, 16, thick);
        line(h * 0.25F, h * 0.25F, h * 0.75F, h * 0.75F);
        break;
    case Icon::Eye:
        list.PathArcTo(offsetBy(c, 0, h * 0.55F), h * 0.95F, -2.4F, -0.74F, 16);
        list.PathStroke(color, 0, thick);
        list.PathArcTo(offsetBy(c, 0, -h * 0.55F), h * 0.95F, 0.74F, 2.4F, 16);
        list.PathStroke(color, 0, thick);
        list.AddCircleFilled(c, h * 0.26F, color);
        break;
    case Icon::Link:
        list.AddCircle(offsetBy(c, -h * 0.32F, 0), h * 0.42F, color, 16, thick);
        list.AddCircle(offsetBy(c, h * 0.32F, 0), h * 0.42F, color, 16, thick);
        break;
    case Icon::Folder:
        list.AddRectFilled(offsetBy(c, -h * 0.85F, -h * 0.55F), offsetBy(c, -h * 0.1F, -h * 0.25F),
                           color, 1.5F);
        list.AddRectFilled(offsetBy(c, -h * 0.85F, -h * 0.35F), offsetBy(c, h * 0.85F, h * 0.6F),
                           color, 1.5F);
        break;
    case Icon::File:
    case Icon::Scene: {
        list.AddRect(offsetBy(c, -h * 0.55F, -h * 0.75F), offsetBy(c, h * 0.55F, h * 0.75F), color,
                     1.5F, 0, thick);
        if (icon == Icon::File) {
            line(-h * 0.25F, -h * 0.15F, h * 0.25F, -h * 0.15F);
            line(-h * 0.25F, h * 0.2F, h * 0.25F, h * 0.2F);
        } else {
            triangle(offsetBy(c, -h * 0.3F, h * 0.4F), offsetBy(c, 0, -h * 0.25F),
                     offsetBy(c, h * 0.3F, h * 0.4F));
        }
        break;
    }
    case Icon::Prefab: {
        const ImVec2 top = offsetBy(c, 0, -h * 0.8F), right = offsetBy(c, h * 0.75F, -h * 0.35F);
        const ImVec2 bottomRight = offsetBy(c, h * 0.75F, h * 0.45F),
                     bottom = offsetBy(c, 0, h * 0.85F);
        const ImVec2 bottomLeft = offsetBy(c, -h * 0.75F, h * 0.45F),
                     left = offsetBy(c, -h * 0.75F, -h * 0.35F);
        const ImVec2 points[6] = {top, right, bottomRight, bottom, bottomLeft, left};
        list.AddPolyline(points, 6, color, ImDrawFlags_Closed, thick);
        list.AddLine(c, bottom, color, thick);
        list.AddLine(c, left, color, thick);
        list.AddLine(c, right, color, thick);
        break;
    }
    case Icon::Image:
        list.AddRect(offsetBy(c, -h * 0.8F, -h * 0.6F), offsetBy(c, h * 0.8F, h * 0.6F), color,
                     1.5F, 0, thick);
        triangle(offsetBy(c, -h * 0.55F, h * 0.4F), offsetBy(c, -h * 0.1F, -h * 0.15F),
                 offsetBy(c, h * 0.35F, h * 0.4F));
        list.AddCircleFilled(offsetBy(c, h * 0.35F, -h * 0.25F), h * 0.14F, color);
        break;
    case Icon::Sound:
        list.AddRectFilled(offsetBy(c, -h * 0.75F, -h * 0.25F), offsetBy(c, -h * 0.3F, h * 0.25F),
                           color);
        triangle(offsetBy(c, -h * 0.3F, -h * 0.25F), offsetBy(c, h * 0.15F, -h * 0.7F),
                 offsetBy(c, h * 0.15F, h * 0.7F));
        triangle(offsetBy(c, -h * 0.3F, h * 0.25F), offsetBy(c, h * 0.15F, h * 0.7F),
                 offsetBy(c, -h * 0.3F, -h * 0.25F));
        list.PathArcTo(c, h * 0.55F, -0.7F, 0.7F, 8);
        list.PathStroke(color, 0, thick);
        break;
    case Icon::Entity:
        list.AddRect(offsetBy(c, -h * 0.55F, -h * 0.55F), offsetBy(c, h * 0.55F, h * 0.55F), color,
                     2.0F, 0, thick);
        break;
    case Icon::Dot:
        list.AddCircleFilled(c, h * 0.42F, color);
        break;
    case Icon::Warning:
        triangle(offsetBy(c, 0, -h * 0.8F), offsetBy(c, -h * 0.85F, h * 0.65F),
                 offsetBy(c, h * 0.85F, h * 0.65F));
        list.AddRectFilled(offsetBy(c, -h * 0.07F, -h * 0.25F), offsetBy(c, h * 0.07F, h * 0.25F),
                           IM_COL32(20, 20, 24, 255));
        list.AddCircleFilled(offsetBy(c, 0, h * 0.42F), h * 0.09F, IM_COL32(20, 20, 24, 255));
        break;
    case Icon::Error:
        list.AddCircleFilled(c, h * 0.8F, color);
        list.AddLine(offsetBy(c, -h * 0.3F, -h * 0.3F), offsetBy(c, h * 0.3F, h * 0.3F),
                     IM_COL32(20, 20, 24, 255), thick);
        list.AddLine(offsetBy(c, -h * 0.3F, h * 0.3F), offsetBy(c, h * 0.3F, -h * 0.3F),
                     IM_COL32(20, 20, 24, 255), thick);
        break;
    case Icon::Info:
        list.AddCircleFilled(c, h * 0.8F, color);
        list.AddRectFilled(offsetBy(c, -h * 0.07F, -h * 0.05F), offsetBy(c, h * 0.07F, h * 0.42F),
                           IM_COL32(20, 20, 24, 255));
        list.AddCircleFilled(offsetBy(c, 0, -h * 0.34F), h * 0.09F, IM_COL32(20, 20, 24, 255));
        break;
    case Icon::Save: {
        list.AddRect(offsetBy(c, -h * 0.7F, -h * 0.7F), offsetBy(c, h * 0.7F, h * 0.7F), color,
                     2.0F, 0, thick);
        list.AddRectFilled(offsetBy(c, -h * 0.35F, -h * 0.7F), offsetBy(c, h * 0.35F, -h * 0.15F),
                           color);
        list.AddRect(offsetBy(c, -h * 0.4F, h * 0.15F), offsetBy(c, h * 0.4F, h * 0.7F), color,
                     0.0F, 0, thick);
        break;
    }
    case Icon::Undo:
    case Icon::Redo: {
        const float dir = icon == Icon::Undo ? -1.0F : 1.0F;
        list.PathArcTo(offsetBy(c, 0, h * 0.1F), h * 0.6F, dir < 0 ? 3.4F : -0.26F,
                       dir < 0 ? 5.9F : 2.3F, 16);
        list.PathStroke(color, 0, thick);
        const ImVec2 tip = offsetBy(c, dir * h * 0.6F, -h * 0.1F);
        triangle(offsetBy(tip, dir * h * 0.05F, -h * 0.5F),
                 offsetBy(tip, dir * h * 0.05F, h * 0.3F),
                 offsetBy(tip, -dir * h * 0.45F, -h * 0.1F));
        break;
    }
    case Icon::Target:
        list.AddCircle(c, h * 0.62F, color, 16, thick);
        line(-h * 0.95F, 0, -h * 0.3F, 0);
        line(h * 0.3F, 0, h * 0.95F, 0);
        line(0, -h * 0.95F, 0, -h * 0.3F);
        line(0, h * 0.3F, 0, h * 0.95F);
        break;
    }
}

bool iconButton(const char *id, Icon icon, bool active, const char *tip, ImU32 tint, float size) {
    ImGui::PushID(id);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##icon", {size, size});
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const float alpha = ImGui::GetStyle().Alpha;
    ImDrawList &list = *ImGui::GetWindowDrawList();
    if (active || hovered || held) {
        const Color fill = active ? Color{76, 110, 156, 255} : Color{62, 70, 90, 255};
        list.AddRectFilled(origin, {origin.x + size, origin.y + size},
                           faded(held ? Color{86, 122, 170, 255} : fill, alpha), 4.0F);
    }
    ImU32 glyph = tint != 0 ? tint : ImGui::GetColorU32(ImGuiCol_Text);
    if (!active && !hovered && tint == 0)
        glyph = faded({190, 196, 210, 255}, alpha);
    drawIcon(list, icon, {origin.x + size * 0.5F, origin.y + size * 0.5F}, size * 0.56F, glyph);
    if (hovered && tip && *tip)
        ImGui::SetTooltip("%s", tip);
    markItem(id);
    ImGui::PopID();
    return pressed;
}

void iconLabel(Icon icon, const char *text, ImU32 tint) {
    const float height = ImGui::GetTextLineHeight();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    drawIcon(*ImGui::GetWindowDrawList(), icon,
             {origin.x + height * 0.5F, origin.y + height * 0.5F}, height * 0.95F,
             tint != 0 ? tint : ImGui::GetColorU32(ImGuiCol_TextDisabled));
    ImGui::Dummy({height, height});
    ImGui::SameLine(0.0F, 4.0F);
    ImGui::TextUnformatted(text);
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
        ImGui::SetTooltip("%s", text.c_str());
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
