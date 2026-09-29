#pragma once
#include "core/EditorDocument.hpp"
#include "imgui.h"
#include "yk/core/Color.hpp"
#include "yk/core/Math.hpp"
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Small building blocks shared by every editor panel: colors, vector icons, the look of the UI and
// the glue that turns a widget interaction into one undoable document change.
namespace yk::editor::ui {
inline ImVec2 im(Vec2 value) {
    return {value.x, value.y};
}
inline Vec2 vec(ImVec2 value) {
    return {value.x, value.y};
}
inline ImU32 packed(Color color) {
    return IM_COL32(color.r, color.g, color.b, color.a);
}
inline ImVec4 imColor(Color color) {
    return {static_cast<float>(color.r) / 255.0F, static_cast<float>(color.g) / 255.0F,
            static_cast<float>(color.b) / 255.0F, static_cast<float>(color.a) / 255.0F};
}
inline Color fromImColor(const ImVec4 &color) {
    const auto channel = [](float value) {
        return static_cast<std::uint8_t>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F);
    };
    return {channel(color.x), channel(color.y), channel(color.z), channel(color.w)};
}
// `color` with its alpha scaled (so translucent overlays keep their hue).
inline ImU32 faded(Color color, float alpha) {
    color.a =
        static_cast<std::uint8_t>(std::clamp(static_cast<float>(color.a) * alpha, 0.0F, 255.0F));
    return packed(color);
}

namespace palette {
inline constexpr Color accent{86, 156, 214, 255};
inline constexpr Color selection{255, 196, 64, 255};
inline constexpr Color selectionSecondary{255, 226, 140, 255};
inline constexpr Color hover{255, 255, 255, 110};
inline constexpr Color gridMinor{255, 255, 255, 12};
inline constexpr Color gridMajor{255, 255, 255, 30};
inline constexpr Color axisX{230, 90, 90, 150};
inline constexpr Color axisY{90, 200, 110, 150};
inline constexpr Color linkOut{90, 210, 255, 255};
inline constexpr Color linkIn{255, 150, 90, 255};
inline constexpr Color ghost{120, 230, 170, 255};
inline constexpr Color camera{200, 200, 255, 200};
inline constexpr Color info{170, 180, 200, 255};
inline constexpr Color warning{240, 190, 80, 255};
inline constexpr Color error{240, 100, 90, 255};
inline constexpr Color good{110, 210, 130, 255};
inline constexpr Color dim{140, 146, 160, 255};
} // namespace palette

// Dark theme, compact spacing.
void applyTheme();

enum class Icon {
    Move,
    Resize,
    Rotate,
    Play,
    Pause,
    Step,
    Stop,
    Restart,
    Grid,
    Plus,
    Cross,
    Search,
    Eye,
    Link,
    Folder,
    File,
    Scene,
    Prefab,
    Image,
    Sound,
    Entity,
    Warning,
    Error,
    Info,
    Dot,
    Save,
    Undo,
    Redo,
    Target
};
// Draws a vector icon inside a `size` square centered at `center`.
void drawIcon(ImDrawList &list, Icon icon, ImVec2 center, float size, ImU32 color);
// Square button showing an icon. `active` draws it as toggled on. Returns true when clicked.
bool iconButton(const char *id, Icon icon, bool active, const char *tooltip, ImU32 tint = 0,
                float size = 28.0F);
// Text with a small icon in front, for list rows.
void iconLabel(Icon icon, const char *text, ImU32 tint = 0);

// Records where interesting widgets ended up on screen, by stable name, so a scripted driver can
// click "toolbar/Play" instead of guessing pixels. Recording costs nothing while disabled.
class WidgetRegistry {
  public:
    void setEnabled(bool enabled) {
        enabled_ = enabled;
    }
    bool enabled() const {
        return enabled_;
    }
    // Widgets seen during the last completed frame become the lookup set.
    void beginFrame();
    void mark(const std::string &id, Rect screenRect, bool visible = true);
    std::optional<Rect> find(const std::string &id) const;
    // False for widgets scrolled or clipped out of sight (a script must scroll them into view).
    bool visible(const std::string &id) const;
    std::vector<std::string> ids() const;
    // The next time `id` is submitted, its window scrolls to show it.
    void requestReveal(std::string id) {
        reveal_ = std::move(id);
    }
    const std::string &revealRequest() const {
        return reveal_;
    }
    void clearReveal() {
        reveal_.clear();
    }

  private:
    struct Entry {
        Rect rect;
        bool visible{true};
    };
    bool enabled_{};
    std::string reveal_;
    std::unordered_map<std::string, Entry> current_, previous_;
};
WidgetRegistry &widgets();
// Registers the last submitted ImGui item under `id`.
void markItem(const std::string &id);
// Registers the current window's rectangle.
void markWindow(const std::string &id);
// Registers a two-field widget (DragFloat2) as `id`, `id`/x and `id`/y.
void markPair(const std::string &id);

// Turns the widget just submitted into document edits. `changed` is what the widget returned and
// `apply` writes the widget's new value into the scene. The first change opens one document change
// that stays open while the widget is held (a drag, a focused text box), so the whole interaction
// is a single Undo step.
void commitEdit(EditorDocument &document, const std::string &label, bool changed,
                const std::function<void()> &apply);
// Closes an interaction whose widget disappeared (the selection changed under a focused text box).
// Call once per frame before drawing panels, except while a scene-view gizmo drag owns the change.
void settleEdits(EditorDocument &document, bool gizmoDragging);

// ImGui text inputs over std::string (the buffer grows as the user types).
bool inputText(const char *label, std::string &value, ImGuiInputTextFlags flags = 0,
               const char *hint = nullptr);
// True on the frame Enter finished editing the text field just submitted (Enter deactivates it).
bool enterPressedInField();
bool inputTextMultiline(const char *label, std::string &value, ImVec2 size,
                        ImGuiInputTextFlags flags = 0);

// "moveSpeed" -> "Move Speed" (see prettifyName), used by labels.
std::string label(const std::string &identifier);
// Shows `text` as a tooltip when the last item is hovered.
void tooltip(const std::string &text);
// True when the last item is hovered for a moment; lets tooltips avoid flickering on drags.
bool hoveredForTooltip();
// A property's name in a table cell: shortened with "..." when it does not fit, in which case the
// tooltip spells the whole name out before `description`.
void nameCell(const std::string &text, const std::string &description);
} // namespace yk::editor::ui
