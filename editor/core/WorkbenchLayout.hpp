#pragma once
#include "yk/core/Json.hpp"
#include "yk/core/Math.hpp"
#include <optional>
#include <string>
#include <string_view>

// Where the editor's parts go, like VS Code's workbench: a title bar on top and a status bar below,
// an activity bar and a side bar on the left, an inspector (the secondary side bar) on the right,
// the editor area in the middle with a panel under it. This is plain data plus geometry: the UI
// draws whatever `regions` says, the sizes survive restarts, and the layout arithmetic is tested
// without a window.
namespace yk::editor {
enum class SideView { Explorer, Scene, Prefabs, Components, Build };
enum class PanelView { Console, Problems, Output, Profiler };
// One editor group, or two next to each other or one above the other.
enum class EditorSplit { None, Right, Down };

const char *name(SideView view);
const char *name(PanelView view);
const char *name(EditorSplit split);
std::optional<SideView> sideViewFromName(std::string_view text);
std::optional<PanelView> panelViewFromName(std::string_view text);
std::optional<EditorSplit> editorSplitFromName(std::string_view text);

struct WorkbenchMetrics {
    float scale{1.0F}; // Display scale; every size below is in points at scale 1.
    float titleBar{30.0F};
    float activityBar{46.0F};
    float statusBar{22.0F};
    float minSideBar{180.0F};
    float minInspector{240.0F};
    float minPanel{80.0F};
    float minEditor{260.0F};
    float minEditorHeight{160.0F};
};

struct WorkbenchRegions {
    Rect title, activity, sideBar, editor, groupA, groupB, inspector, panel, status;
    bool hasSideBar{}, hasInspector{}, hasPanel{}, hasGroupB{};
};

struct WorkbenchLayout {
    bool sideBarVisible{true};
    float sideBarWidth{290.0F};
    SideView sideView{SideView::Scene};
    bool inspectorVisible{true};
    float inspectorWidth{340.0F};
    bool panelVisible{true};
    float panelHeight{210.0F};
    bool panelMaximized{false};
    PanelView panelView{PanelView::Console};
    EditorSplit split{EditorSplit::None};
    float splitRatio{0.5F}; // Share of the editor area the first group gets.

    // Places every region for a window of `size`. Stored sizes are clamped so no part can collapse
    // or push another one out of the window; the regions tile the window exactly.
    WorkbenchRegions regions(Vec2 size, const WorkbenchMetrics &metrics = {}) const;

    // Shows `view` in the side bar; choosing the visible view again hides the side bar (the
    // activity bar behaves like VS Code's).
    void toggleSideView(SideView view);
    // Shows `view` in the panel (opening it when hidden); choosing the visible view hides it.
    void togglePanelView(PanelView view);

    Json toJson() const;
    // Tolerant: anything missing or invalid keeps its default, so an old or damaged file never
    // stops the editor from starting.
    static WorkbenchLayout fromJson(const Json &json);
    friend bool operator==(const WorkbenchLayout &a, const WorkbenchLayout &b) = default;
};
} // namespace yk::editor
