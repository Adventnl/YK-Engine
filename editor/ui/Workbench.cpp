#include "ui/Panels.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

// The frame of the editor, laid out like VS Code: title bar (menus, Play controls, layout buttons),
// activity bar, side bar, editor groups with tabs, the inspector as the secondary side bar, the
// panel and the status bar. The geometry comes from WorkbenchLayout (tested without a window);
// every part is one borderless ImGui window pinned to its rectangle, so nothing floats or docks.
namespace yk::editor::ui {
namespace {
constexpr ImGuiWindowFlags regionFlags =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav;

float scaleOf() {
    return std::max(1.0F, ImGui::GetStyle().FontScaleDpi);
}
float dp(float value) {
    return value * scaleOf();
}

// Everything that is decided while the frame is drawn and applied when it is over, so a click on a
// tab cannot swap the open document under the panels that are still drawing it.
struct Deferred {
    std::vector<std::function<void()>> actions;
    void later(std::function<void()> action) {
        actions.push_back(std::move(action));
    }
    void run() {
        auto pending = std::move(actions);
        actions.clear();
        for (auto &action : pending)
            action();
    }
} deferred;

bool beginRegion(const char *name, Rect rect, Color background, ImVec2 padding = {0.0F, 0.0F},
                 ImGuiWindowFlags extra = 0) {
    ImGui::SetNextWindowPos(im(rect.position));
    ImGui::SetNextWindowSize(im(rect.size));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, imColor(background));
    const bool open = ImGui::Begin(name, nullptr, regionFlags | extra);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    return open;
}

// A 1 pixel divider along one edge of the current window.
enum class Edge { Left, Right, Top, Bottom };
void divider(Edge edge) {
    ImDrawList &list = *ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetWindowPos();
    const ImVec2 b{a.x + ImGui::GetWindowWidth(), a.y + ImGui::GetWindowHeight()};
    const ImU32 color = packed(vs::border);
    switch (edge) {
    case Edge::Left:
        list.AddLine({a.x + 0.5F, a.y}, {a.x + 0.5F, b.y}, color);
        break;
    case Edge::Right:
        list.AddLine({b.x - 0.5F, a.y}, {b.x - 0.5F, b.y}, color);
        break;
    case Edge::Top:
        list.AddLine({a.x, a.y + 0.5F}, {b.x, a.y + 0.5F}, color);
        break;
    case Edge::Bottom:
        list.AddLine({a.x, b.y - 0.5F}, {b.x, b.y - 0.5F}, color);
        break;
    }
}

// ---------------------------------------------------------------------------------- title bar
void titleBar(EditorState &state, Rect rect) {
    const float bar = rect.size.y;
    const float pad = std::max(4.0F, (bar - ImGui::GetFontSize()) * 0.5F);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {dp(9.0F), pad});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
    const bool open =
        beginRegion("##titlebar", rect, vs::chromeBg, {0.0F, 0.0F}, ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar(2);
    if (open && ImGui::BeginMenuBar()) {
        markWindow("panel/TitleBar");
        // The mark of the application, then the menus.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {dp(9.0F), pad});
        ImGui::Dummy({dp(10.0F), 1.0F});
        ImGui::SameLine(0.0F, 0.0F);
        ImGui::PushFont(fonts().semibold, 13.0F);
        ImGui::TextColored(imColor(vs::focus), "YK");
        ImGui::PopFont();
        ImGui::SameLine(0.0F, dp(6.0F));
        drawMenus(state);
        ImGui::PopStyleVar();

        // Play controls in the middle of the bar.
        const float button = dp(24.0F);
        const float group = 5.0F * button + 4.0F * 2.0F;
        const float left = rect.position.x + (rect.size.x - group) * 0.5F;
        const float top = rect.position.y + (bar - button) * 0.5F;
        ImGui::SetCursorScreenPos({left, top});
        const bool canPlay = state.document != nullptr && state.project != nullptr;
        ImGui::BeginDisabled(!canPlay || state.playing());
        if (iconButton("toolbar/Play", Icon::Play, state.playing(), "Play (F5)",
                       packed(vs::success), button))
            state.startPlay();
        ImGui::EndDisabled();
        ImGui::SameLine(0.0F, 2.0F);
        ImGui::BeginDisabled(!state.playing());
        if (iconButton("toolbar/Pause", Icon::Pause, state.playing() && state.play->paused(),
                       "Pause (F6)", 0, button))
            state.togglePause();
        ImGui::SameLine(0.0F, 2.0F);
        ImGui::BeginDisabled(!state.playing() || !state.play->paused());
        if (iconButton("toolbar/Step", Icon::Step, false, "Step one tick (F10)", 0, button))
            state.stepPlay(InputFrame{});
        ImGui::EndDisabled();
        ImGui::SameLine(0.0F, 2.0F);
        if (iconButton("toolbar/Stop", Icon::Stop, false, "Stop (Shift+F5)", packed(vs::error),
                       button))
            state.stopPlay();
        ImGui::SameLine(0.0F, 2.0F);
        if (iconButton("toolbar/Restart", Icon::Restart, false, "Restart the scene (Ctrl+Shift+F5)",
                       0, button))
            state.restartPlay();
        ImGui::EndDisabled();

        // Layout toggles at the right end.
        WorkbenchLayout &layout = state.layout;
        const float right = rect.position.x + rect.size.x - dp(8.0F);
        ImGui::SetCursorScreenPos({right - 3.0F * (button + 2.0F), top});
        if (iconButton("layout/Sidebar",
                       layout.sideBarVisible ? Icon::SidebarLeft : Icon::SidebarLeftOff, false,
                       "Toggle side bar (Ctrl+B)", 0, button))
            layout.sideBarVisible = !layout.sideBarVisible;
        ImGui::SameLine(0.0F, 2.0F);
        if (iconButton("layout/Panel", layout.panelVisible ? Icon::Panel : Icon::PanelOff, false,
                       "Toggle panel (Ctrl+J)", 0, button))
            layout.panelVisible = !layout.panelVisible;
        ImGui::SameLine(0.0F, 2.0F);
        if (iconButton("layout/Inspector",
                       layout.inspectorVisible ? Icon::SidebarRight : Icon::SidebarRightOff, false,
                       "Toggle inspector (Ctrl+Alt+B)", 0, button))
            layout.inspectorVisible = !layout.inspectorVisible;
        ImGui::EndMenuBar();
    }
    divider(Edge::Bottom);
    ImGui::End();
}

// -------------------------------------------------------------------------------- activity bar
struct ActivityItem {
    SideView view;
    Icon icon;
    const char *label;
    const char *shortcut;
};
constexpr ActivityItem activityItems[] = {
    {SideView::Explorer, Icon::Explorer, "Explorer", "Ctrl+Shift+E"},
    {SideView::Scene, Icon::Hierarchy, "Scene Hierarchy", "Ctrl+Shift+H"},
    {SideView::Prefabs, Icon::Prefabs, "Prefabs", "Ctrl+Shift+K"},
    {SideView::Components, Icon::Components, "Components", "Ctrl+Shift+X"},
    {SideView::Build, Icon::Build, "Build and Run", "Ctrl+Shift+B"}};

void activityBar(EditorState &state, Rect rect) {
    if (!beginRegion("##activity", rect, vs::chromeBg)) {
        ImGui::End();
        return;
    }
    markWindow("panel/ActivityBar");
    WorkbenchLayout &layout = state.layout;
    ImDrawList &list = *ImGui::GetWindowDrawList();
    const float size = rect.size.x;
    ImGui::SetCursorPos({0.0F, 0.0F});
    const auto entry = [&](const char *id, Icon icon, bool active, const std::string &tip) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::PushID(id);
        const bool clicked = ImGui::InvisibleButton("##activity", {size, size});
        const bool hovered = ImGui::IsItemHovered();
        markItem(id);
        ImGui::PopID();
        if (active)
            list.AddRectFilled(at, {at.x + 2.0F, at.y + size}, packed(vs::focus));
        drawIcon(list, icon, {at.x + size * 0.5F, at.y + size * 0.5F}, size * 0.5F,
                 packed(active || hovered ? vs::activityActive : vs::activityInactive));
        if (hovered && hoveredForTooltip())
            ImGui::SetTooltip("%s", tip.c_str());
        return clicked;
    };
    for (const ActivityItem &item : activityItems) {
        const bool active = layout.sideBarVisible && layout.sideView == item.view;
        if (entry((std::string("activity/") +
                   std::string(item.label == std::string("Scene Hierarchy") ? "Scene"
                               : item.label == std::string("Build and Run") ? "Build"
                                                                            : item.label))
                      .c_str(),
                  item.icon, active, std::string(item.label) + " (" + item.shortcut + ")"))
            layout.toggleSideView(item.view);
    }
    // The gear at the bottom.
    ImGui::SetCursorPos({0.0F, rect.size.y - size});
    if (entry("activity/Settings", Icon::Settings, false, "Manage"))
        ImGui::OpenPopup("manage_menu");
    if (ImGui::BeginPopup("manage_menu")) {
        {
            const PopupLook look;
            if (ImGui::MenuItem("Project Settings...", nullptr, false, state.project != nullptr))
                showDialog(state, DialogKind::ProjectSettings);
            markItem("manage/Project Settings");
            if (ImGui::MenuItem("Keyboard Shortcuts"))
                showDialog(state, DialogKind::Shortcuts);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout"))
                layout = {};
            markItem("manage/Reset Layout");
        }
        ImGui::EndPopup();
    }
    divider(Edge::Right);
    ImGui::End();
}

// -------------------------------------------------------------------------------- side bar
const char *sideTitle(SideView view) {
    switch (view) {
    case SideView::Explorer:
        return "Explorer";
    case SideView::Scene:
        return "Scene";
    case SideView::Prefabs:
        return "Prefabs";
    case SideView::Components:
        return "Components";
    case SideView::Build:
        return "Build and Run";
    }
    return "";
}

// The small title row at the top of a side bar or panel region ("EXPLORER"), and where content
// starts below it.
float regionHeader(const std::string &title, float height) {
    ImDrawList &list = *ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetWindowPos();
    const std::string text = headerText(title);
    list.AddText(fonts().semibold, 11.5F, {at.x + dp(14.0F), at.y + (height - 11.5F) * 0.5F - 1.0F},
                 packed(vs::textDim), text.c_str());
    return height;
}

void sideBar(EditorState &state, Rect rect) {
    if (!beginRegion("##sidebar", rect, vs::chromeBg)) {
        ImGui::End();
        return;
    }
    markWindow("panel/SideBar");
    const float header = regionHeader(sideTitle(state.layout.sideView), dp(metrics::sideBarHeader));
    ImGui::SetCursorPos({0.0F, header});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(8.0F), dp(4.0F)});
    const ImVec2 size{rect.size.x, rect.size.y - header};
    if (ImGui::BeginChild("##sidecontent", size, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar)) {
        switch (state.layout.sideView) {
        case SideView::Explorer:
            markWindow("panel/Assets");
            assetsPanel(state);
            break;
        case SideView::Scene:
            markWindow("panel/Hierarchy");
            hierarchyPanel(state);
            break;
        case SideView::Prefabs:
            markWindow("panel/Prefabs");
            prefabsPanel(state);
            break;
        case SideView::Components:
            markWindow("panel/Components");
            componentsPanel(state);
            break;
        case SideView::Build:
            markWindow("panel/Build");
            buildPanel(state);
            break;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    divider(Edge::Right);
    ImGui::End();
}

// ------------------------------------------------------------------------------ editor groups
constexpr const char *gameKey = "Game";

std::string fileName(const std::string &path) {
    return std::filesystem::path(path).filename().string();
}

// The key of the tab a group should show: a scene path, or "Game".
std::string desiredTab(const EditorState &state, int group) {
    if (group == 1 || state.layout.split != EditorSplit::None) {
        // In a split, the second group is the game; the first keeps the scene tabs.
        return group == 1 ? std::string(gameKey)
                          : (state.document ? state.document->path() : std::string{});
    }
    if (state.editorFocus == EditorFocus::Game || !state.document)
        return state.sceneTabs.empty() || state.editorFocus == EditorFocus::Game
                   ? std::string(gameKey)
                   : std::string{};
    return state.document->path();
}

void tabContextMenu(EditorState &state, const std::string &path) {
    if (!ImGui::BeginPopupContextItem("tab_menu"))
        return;
    {
        const PopupLook look;
        if (ImGui::MenuItem("Close", "Ctrl+W"))
            deferred.later([&state, path] { state.closeScene(path); });
        if (ImGui::MenuItem("Close Others", nullptr, false, state.sceneTabs.size() > 1))
            deferred.later([&state, path] {
                for (const std::string &other : std::vector<std::string>(state.sceneTabs))
                    if (other != path)
                        state.closeScene(other);
            });
        if (ImGui::MenuItem("Close All"))
            deferred.later([&state] {
                for (const std::string &other : std::vector<std::string>(state.sceneTabs))
                    state.closeScene(other);
            });
        ImGui::Separator();
        if (ImGui::MenuItem("Copy Path"))
            ImGui::SetClipboardText(path.c_str());
        if (ImGui::MenuItem(state.layout.split == EditorSplit::None ? "Split Right" : "Unsplit",
                            "Ctrl+\\"))
            state.layout.split =
                state.layout.split == EditorSplit::None ? EditorSplit::Right : EditorSplit::None;
    }
    ImGui::EndPopup();
}

void editorGroup(EditorState &state, int group, Rect rect) {
    const std::string name = group == 0 ? "##group0" : "##group1";
    if (!beginRegion(name.c_str(), rect, vs::editorBg)) {
        ImGui::End();
        return;
    }
    markWindow(group == 0 ? "panel/Editor" : "panel/Editor2");
    if (!state.project) {
        if (group == 0)
            welcomePage(state);
        divider(Edge::Bottom);
        ImGui::End();
        return;
    }
    static std::string lastDesired[2];
    const std::string desired = desiredTab(state, group);
    const bool programmatic = desired != lastDesired[group];
    lastDesired[group] = desired;
    const bool showGameTab = group == 1 || state.layout.split == EditorSplit::None;
    const bool showSceneTabs = group == 0;
    std::string shown;

    const float barHeight = dp(metrics::tabBar);
    const ImVec2 origin = ImGui::GetWindowPos();
    ImDrawList &list = *ImGui::GetWindowDrawList();
    list.AddRectFilled(origin, {origin.x + rect.size.x, origin.y + barHeight},
                       packed(vs::chromeBg));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        {dp(12.0F), (barHeight - ImGui::GetFontSize() - 1.0F) * 0.5F});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, {6.0F, 0.0F});
    ImGui::PushStyleColor(ImGuiCol_TabSelected, imColor(vs::editorBg));
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, imColor(vs::editorBg));
    ImGui::PushStyleColor(ImGuiCol_Text, imColor(vs::textDim));
    ImGui::SetCursorPos({0.0F, 0.0F});
    const bool hasTabs = (showSceneTabs && !state.sceneTabs.empty()) || showGameTab;
    if (hasTabs &&
        ImGui::BeginTabBar((name + "_tabs").c_str(),
                           ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll |
                               ImGuiTabBarFlags_NoTabListScrollingButtons)) {
        if (showSceneTabs) {
            for (const std::string &path : std::vector<std::string>(state.sceneTabs)) {
                const EditorDocument *doc = nullptr;
                if (state.document && state.document->path() == path)
                    doc = state.document.get();
                else if (const auto found = state.background.find(path);
                         found != state.background.end())
                    doc = found->second.document.get();
                bool open = true;
                ImGuiTabItemFlags flags =
                    doc && doc->dirty() ? ImGuiTabItemFlags_UnsavedDocument : 0;
                if (programmatic && desired == path)
                    flags |= ImGuiTabItemFlags_SetSelected;
                const bool selected =
                    ImGui::BeginTabItem((fileName(path) + "###tab:" + path).c_str(), &open, flags);
                markItem("tab/" + fileName(path));
                tooltip(path);
                tabContextMenu(state, path);
                if (selected) {
                    shown = path;
                    ImGui::PushStyleColor(ImGuiCol_Text, imColor(vs::textBright));
                    ImGui::PopStyleColor();
                    ImGui::EndTabItem();
                }
                if (!open)
                    deferred.later([&state, path] { state.closeScene(path); });
            }
        }
        if (showGameTab) {
            bool open = true;
            ImGuiTabItemFlags flags = 0;
            if (programmatic && desired == gameKey)
                flags |= ImGuiTabItemFlags_SetSelected;
            const bool selected =
                ImGui::BeginTabItem("Game###tab:game", group == 1 ? &open : nullptr, flags);
            markItem("tab/Game");
            if (selected) {
                shown = gameKey;
                ImGui::EndTabItem();
            }
            if (!open)
                deferred.later([&state] { state.layout.split = EditorSplit::None; });
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    list.AddLine({origin.x, origin.y + barHeight - 0.5F},
                 {origin.x + rect.size.x, origin.y + barHeight - 0.5F}, packed(vs::border));
    if (shown.empty())
        shown = desired;

    // A click on a tab makes it current; the swap happens after the frame.
    if (!programmatic && shown != desired) {
        if (shown == gameKey)
            deferred.later([&state] { state.editorFocus = EditorFocus::Game; });
        else
            deferred.later([&state, shown] { state.activateScene(shown); });
    }

    // The content under the tabs.
    ImGui::SetCursorPos({0.0F, hasTabs ? barHeight : 0.0F});
    const ImVec2 contentSize{rect.size.x, rect.size.y - (hasTabs ? barHeight : 0.0F)};
    if (ImGui::BeginChild("##editorcontent", contentSize, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (shown == gameKey) {
            if (state.focusGame && state.playing()) {
                // Pressing Play hands the keyboard to the game, like clicking into its view would.
                ImGui::SetWindowFocus(name.c_str());
                state.focusGame = false;
            }
            gameViewPanel(state);
        } else if (shown.empty() || (state.document && state.document->path() == shown)) {
            sceneViewPanel(state);
        }
    }
    ImGui::EndChild();
    if (group == 0 && state.layout.split == EditorSplit::Right)
        divider(Edge::Right);
    if (group == 0 && state.layout.split == EditorSplit::Down)
        divider(Edge::Bottom);
    ImGui::End();
}

// --------------------------------------------------------------------------------- inspector
void inspector(EditorState &state, Rect rect) {
    if (!beginRegion("##inspector", rect, vs::chromeBg)) {
        ImGui::End();
        return;
    }
    markWindow("panel/Inspector");
    const float header = regionHeader("Inspector", dp(metrics::sideBarHeader));
    ImGui::SetCursorPos({0.0F, header});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(8.0F), dp(4.0F)});
    if (ImGui::BeginChild("##inspectorcontent", {rect.size.x, rect.size.y - header},
                          ImGuiChildFlags_None, ImGuiWindowFlags_None))
        inspectorPanel(state);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    divider(Edge::Left);
    ImGui::End();
}

// ------------------------------------------------------------------------------------- panel
void panel(EditorState &state, Rect rect) {
    if (!beginRegion("##panel", rect, vs::chromeBg)) {
        ImGui::End();
        return;
    }
    markWindow("panel/Panel");
    WorkbenchLayout &layout = state.layout;
    ImDrawList &list = *ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float bar = dp(metrics::panelTabBar);
    struct Tab {
        PanelView view;
        const char *label;
        std::string badge;
        Color badgeColor;
    };
    std::size_t errors = 0, warnings = 0;
    for (const ProjectIssue &issue : state.problems)
        (issue.severity == ProjectIssue::Severity::Error ? errors : warnings)++;
    const std::size_t consoleErrors = state.console.count(LogLevel::Error);
    std::vector<Tab> tabs = {{PanelView::Console, "Console",
                              consoleErrors ? std::to_string(consoleErrors) : "", vs::error},
                             {PanelView::Problems, "Problems",
                              errors + warnings ? std::to_string(errors + warnings) : "",
                              errors ? vs::error : vs::warning},
                             {PanelView::Output, "Build Output", "", vs::info},
                             {PanelView::Profiler, "Profiler", "", vs::info}};
    float x = dp(14.0F);
    ImGui::SetCursorPos({0.0F, 0.0F});
    for (const Tab &tab : tabs) {
        const std::string text = headerText(tab.label);
        ImGui::PushFont(fonts().semibold, 11.5F);
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        ImGui::PopFont();
        const float badgeWidth = tab.badge.empty() ? 0.0F : dp(22.0F);
        const float width = textSize.x + badgeWidth + dp(4.0F);
        ImGui::SetCursorPos({x, 0.0F});
        ImGui::PushID(tab.label);
        const bool clicked = ImGui::InvisibleButton("##paneltab", {width, bar});
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        markItem(std::string("panel-tab/") + tab.label);
        const bool active = layout.panelView == tab.view;
        const ImVec2 at{origin.x + x, origin.y};
        list.AddText(fonts().semibold, 11.5F, {at.x, at.y + (bar - 11.5F) * 0.5F - 1.0F},
                     packed(active || hovered ? vs::textBright : vs::textDim), text.c_str());
        if (!tab.badge.empty()) {
            const ImVec2 pill{at.x + textSize.x + 6.0F, at.y + bar * 0.5F - 7.0F};
            list.AddRectFilled(pill, {pill.x + 16.0F, pill.y + 14.0F}, packed(tab.badgeColor),
                               7.0F);
            ImGui::PushFont(fonts().ui, 10.5F);
            const ImVec2 badgeSize = ImGui::CalcTextSize(tab.badge.c_str());
            list.AddText(
                {pill.x + (16.0F - badgeSize.x) * 0.5F, pill.y + (14.0F - badgeSize.y) * 0.5F},
                IM_COL32(20, 20, 20, 255), tab.badge.c_str());
            ImGui::PopFont();
        }
        if (active)
            list.AddRectFilled({at.x, at.y + bar - 2.0F},
                               {at.x + textSize.x + badgeWidth, at.y + bar - 1.0F},
                               packed(vs::focus));
        if (clicked)
            layout.panelView = tab.view;
        x += width + dp(16.0F);
    }
    // Actions at the right end: maximize and close.
    const float button = dp(22.0F);
    ImGui::SetCursorPos({rect.size.x - 2.0F * (button + 2.0F) - dp(8.0F), (bar - button) * 0.5F});
    if (iconButton("panel/Maximize", layout.panelMaximized ? Icon::ChevronDown : Icon::ChevronUp,
                   false, layout.panelMaximized ? "Restore panel size" : "Maximize panel", 0,
                   button))
        layout.panelMaximized = !layout.panelMaximized;
    ImGui::SameLine(0.0F, 2.0F);
    if (iconButton("panel/Close", Icon::Cross, false, "Close panel (Ctrl+J)", 0, button)) {
        layout.panelVisible = false;
        layout.panelMaximized = false;
    }
    ImGui::SetCursorPos({0.0F, bar});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(8.0F), dp(2.0F)});
    if (ImGui::BeginChild("##panelcontent", {rect.size.x, std::max(0.0F, rect.size.y - bar)},
                          ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar)) {
        switch (layout.panelView) {
        case PanelView::Console:
            markWindow("panel/Console");
            consolePanel(state);
            break;
        case PanelView::Problems:
            markWindow("panel/Problems");
            problemsPanel(state);
            break;
        case PanelView::Output:
            markWindow("panel/Output");
            outputPanel(state);
            break;
        case PanelView::Profiler:
            markWindow("panel/Profiler");
            profilerPanel(state);
            break;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    divider(Edge::Top);
    ImGui::End();
}

// ---------------------------------------------------------------------------------- status bar
struct StatusItem {
    std::string id;
    std::string text;
    Icon icon{Icon::Dot};
    bool hasIcon{};
    bool highlight{}; // The blue chip at the far left.
    std::string tooltip;
    std::function<void()> onClick;
};

float drawStatusItems(const std::vector<StatusItem> &items, float x, float y, float height,
                      bool rightAligned, Color foreground, Color chipColor) {
    ImDrawList &list = *ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    ImGui::PushFont(fonts().ui, 12.5F);
    std::vector<float> widths;
    float total = 0.0F;
    for (const StatusItem &item : items) {
        float w = ImGui::CalcTextSize(item.text.c_str()).x + dp(16.0F);
        if (item.hasIcon)
            w += dp(16.0F);
        widths.push_back(w);
        total += w;
    }
    float at = rightAligned ? x - total : x;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const StatusItem &item = items[i];
        ImGui::SetCursorPos({at, y});
        ImGui::PushID(static_cast<int>(i) + (rightAligned ? 1000 : 0));
        const bool clicked = ImGui::InvisibleButton("##status", {widths[i], height});
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        markItem("status/" + item.id);
        const ImVec2 tl{origin.x + at, origin.y + y};
        if (item.highlight)
            list.AddRectFilled(tl, {tl.x + widths[i], tl.y + height}, packed(chipColor));
        else if (hovered && item.onClick)
            list.AddRectFilled(tl, {tl.x + widths[i], tl.y + height}, IM_COL32(255, 255, 255, 30));
        float textX = tl.x + dp(8.0F);
        if (item.hasIcon) {
            drawIcon(list, item.icon, {textX + dp(6.0F), tl.y + height * 0.5F}, 13.0F,
                     packed(foreground));
            textX += dp(16.0F);
        }
        const ImVec2 textSize = ImGui::CalcTextSize(item.text.c_str());
        list.AddText({textX, tl.y + (height - textSize.y) * 0.5F}, packed(foreground),
                     item.text.c_str());
        if (hovered && !item.tooltip.empty() && hoveredForTooltip())
            ImGui::SetTooltip("%s", item.tooltip.c_str());
        if (clicked && item.onClick)
            item.onClick();
        at += widths[i];
    }
    ImGui::PopFont();
    return total;
}

void statusBar(EditorState &state, Rect rect) {
    Color background = vs::chromeBg;
    Color foreground = vs::text;
    if (state.playing()) {
        background = state.play->paused() ? vs::statusPaused : vs::statusPlaying;
        foreground = state.play->paused() ? Color{30, 30, 30, 255} : vs::textBright;
    }
    if (!beginRegion("##status", rect, background)) {
        ImGui::End();
        return;
    }
    markWindow("panel/StatusBar");
    WorkbenchLayout &layout = state.layout;
    std::vector<StatusItem> left, right;
    StatusItem chip;
    chip.id = "project";
    chip.text = state.project ? state.project->project().name : "No project";
    chip.icon = Icon::FolderOpen;
    chip.hasIcon = true;
    chip.highlight = !state.playing();
    chip.tooltip = state.project ? state.project->project().root.string() : "Open a project";
    chip.onClick = [&state] {
        state.layout.sideView = SideView::Explorer;
        state.layout.sideBarVisible = true;
    };
    left.push_back(chip);
    if (state.document && !state.playing()) {
        StatusItem scene;
        scene.id = "scene";
        scene.text = fileName(state.document->path()) + (state.document->dirty() ? " *" : "");
        scene.icon = Icon::Scene;
        scene.hasIcon = true;
        scene.tooltip = state.document->path();
        scene.onClick = [&layout] { layout.toggleSideView(SideView::Scene); };
        left.push_back(scene);
    }
    std::size_t errors = 0, warnings = 0;
    for (const ProjectIssue &issue : state.problems)
        (issue.severity == ProjectIssue::Severity::Error ? errors : warnings)++;
    if (state.project) {
        StatusItem problems;
        problems.id = "problems";
        problems.text = std::to_string(errors) + "  " + std::to_string(warnings);
        problems.icon = errors ? Icon::Error : Icon::Warning;
        problems.hasIcon = true;
        problems.tooltip = "Problems: " + std::to_string(errors) + " error(s), " +
                           std::to_string(warnings) + " warning(s). Click to open.";
        problems.onClick = [&state] {
            state.layout.panelView = PanelView::Problems;
            state.layout.panelVisible = true;
        };
        left.push_back(problems);
    }
    if (state.playing()) {
        StatusItem mode;
        mode.id = "mode";
        mode.text = state.play->paused() ? "PAUSED" : "PLAYING";
        mode.icon = state.play->paused() ? Icon::Pause : Icon::Play;
        mode.hasIcon = true;
        left.push_back(mode);
    }
    const ConsoleLog::Entry latest = state.console.latest();
    if (latest.serial != 0 && latest.level != LogLevel::Info) {
        StatusItem last;
        last.id = "message";
        last.text = latest.message.substr(0, 90);
        last.icon = latest.level == LogLevel::Error ? Icon::Error : Icon::Warning;
        last.hasIcon = true;
        last.onClick = [&state] {
            state.layout.panelView = PanelView::Console;
            state.layout.panelVisible = true;
        };
        left.push_back(last);
    }

    char text[96];
    if (state.document && !state.playing()) {
        StatusItem selection;
        selection.id = "selection";
        std::snprintf(text, sizeof text, "%zu selected", state.document->selection().size());
        selection.text = text;
        right.push_back(selection);
        StatusItem entities;
        entities.id = "entities";
        std::snprintf(text, sizeof text, "%zu entities", state.document->scene().size());
        entities.text = text;
        right.push_back(entities);
        StatusItem snap;
        snap.id = "snap";
        std::snprintf(
            text, sizeof text, "Snap %s",
            state.interaction.snap.enabled
                ? (std::to_string(state.interaction.snap.grid).substr(0, 4) + " m").c_str()
                : "off");
        snap.text = text;
        snap.icon = Icon::Snap;
        snap.hasIcon = true;
        snap.tooltip = "Toggle snapping to the grid";
        snap.onClick = [&state] {
            state.interaction.snap.enabled = !state.interaction.snap.enabled;
        };
        right.push_back(snap);
    }
    if (state.document) {
        StatusItem zoom;
        zoom.id = "zoom";
        std::snprintf(text, sizeof text, "%d%%",
                      static_cast<int>(state.interaction.camera.zoom / 48.0F * 100.0F));
        zoom.text = text;
        zoom.tooltip = "Scene view zoom (click to frame the scene)";
        zoom.onClick = [&state] { state.interaction.frameAll(); };
        right.push_back(zoom);
    }
    StatusItem fps;
    fps.id = "fps";
    std::snprintf(text, sizeof text, "%.0f fps", static_cast<double>(state.framesPerSecond));
    fps.text = text;
    fps.tooltip = "Editor frame rate";
    fps.onClick = [&layout] {
        layout.panelView = PanelView::Profiler;
        layout.panelVisible = true;
    };
    right.push_back(fps);

    const float height = rect.size.y;
    drawStatusItems(left, 0.0F, 0.0F, height, false, foreground, vs::focus);
    drawStatusItems(right, rect.size.x - dp(4.0F), 0.0F, height, true, foreground, vs::focus);
    divider(Edge::Top);
    ImGui::End();
}

// -------------------------------------------------------------------------------------- sashes
// A draggable strip between two regions. `zone` straddles the border; dragging reports the pointer
// position along the moving axis.
bool sash(const char *id, Rect zone, bool vertical, float &pointer) {
    ImGui::SetNextWindowPos(im(zone.position));
    ImGui::SetNextWindowSize(im(zone.size));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    // The strip must be exactly as big as `zone` (ImGui would round a 5 point window up to 32 and
    // that window would swallow clicks meant for the panels under it) and must sit above the
    // regions it separates; windows flagged NoBringToFrontOnFocus are created behind the others.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {1.0F, 1.0F});
    ImGui::Begin(id, nullptr,
                 (regionFlags & ~ImGuiWindowFlags_NoBringToFrontOnFocus) |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PopStyleVar(3);
    ImGui::SetCursorPos({0.0F, 0.0F});
    ImGui::InvisibleButton("##sash", im(zone.size));
    const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
    const bool active = ImGui::IsItemActive();
    markItem(std::string("sash/") + id);
    if (hovered)
        ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    if (hovered) {
        ImDrawList &list = *ImGui::GetWindowDrawList();
        const ImVec2 a = ImGui::GetWindowPos();
        if (vertical)
            list.AddRectFilled({a.x + zone.size.x * 0.5F - 1.0F, a.y},
                               {a.x + zone.size.x * 0.5F + 1.0F, a.y + zone.size.y},
                               packed(active ? vs::focus : Color{0, 120, 212, 160}));
        else
            list.AddRectFilled({a.x, a.y + zone.size.y * 0.5F - 1.0F},
                               {a.x + zone.size.x, a.y + zone.size.y * 0.5F + 1.0F},
                               packed(active ? vs::focus : Color{0, 120, 212, 160}));
    }
    if (active) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        pointer = vertical ? mouse.x : mouse.y;
    }
    ImGui::End();
    return active;
}

void sashes(EditorState &state, const WorkbenchRegions &r, ImVec2 window) {
    WorkbenchLayout &layout = state.layout;
    const float thick = dp(metrics::sash);
    float pointer = 0.0F;
    if (r.hasSideBar &&
        sash("side",
             {{r.sideBar.position.x + r.sideBar.size.x - thick * 0.5F, r.sideBar.position.y},
              {thick, r.sideBar.size.y}},
             true, pointer))
        layout.sideBarWidth =
            std::clamp(pointer - r.sideBar.position.x, dp(150.0F), window.x * 0.5F);
    if (r.hasInspector && sash("inspector",
                               {{r.inspector.position.x - thick * 0.5F, r.inspector.position.y},
                                {thick, r.inspector.size.y}},
                               true, pointer))
        layout.inspectorWidth = std::clamp(r.inspector.position.x + r.inspector.size.x - pointer,
                                           dp(220.0F), window.x * 0.5F);
    if (r.hasPanel && !layout.panelMaximized &&
        sash("panel",
             {{r.panel.position.x, r.panel.position.y - thick * 0.5F}, {r.panel.size.x, thick}},
             false, pointer))
        layout.panelHeight =
            std::clamp(r.panel.position.y + r.panel.size.y - pointer, dp(60.0F), window.y);
    if (r.hasGroupB) {
        if (layout.split == EditorSplit::Right) {
            if (sash("split",
                     {{r.groupB.position.x - thick * 0.5F, r.groupB.position.y},
                      {thick, r.groupB.size.y}},
                     true, pointer))
                layout.splitRatio =
                    std::clamp((pointer - r.editor.position.x) / std::max(1.0F, r.editor.size.x),
                               0.15F, 0.85F);
        } else if (sash("split",
                        {{r.groupB.position.x, r.groupB.position.y - thick * 0.5F},
                         {r.groupB.size.x, thick}},
                        false, pointer)) {
            layout.splitRatio = std::clamp(
                (pointer - r.editor.position.y) / std::max(1.0F, r.editor.size.y), 0.15F, 0.85F);
        }
    }
}

void saveLayoutIfChanged(EditorState &state) {
    if (!state.options.persistLayout || state.layout == state.savedLayout ||
        ImGui::IsMouseDown(ImGuiMouseButton_Left))
        return;
    const auto written = writeTextFileAtomic(state.settingsDirectory / "workbench.json",
                                             state.layout.toJson().dump(2) + "\n");
    if (!written)
        log(LogLevel::Warning, "editor", "Could not save the window layout: " + written.error());
    state.savedLayout = state.layout;
}
} // namespace

void drawWorkbench(EditorState &state) {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    WorkbenchMetrics scaled;
    scaled.scale = scaleOf();
    const WorkbenchRegions r = state.layout.regions({viewport->Size.x, viewport->Size.y}, scaled);
    titleBar(state, r.title);
    activityBar(state, r.activity);
    if (r.hasSideBar)
        sideBar(state, r.sideBar);
    // A panel that the layout maximizes hides the editor groups.
    const bool editorVisible = r.editor.size.y > 40.0F;
    if (editorVisible) {
        editorGroup(state, 0, r.groupA);
        if (r.hasGroupB)
            editorGroup(state, 1, r.groupB);
    }
    if (r.hasInspector)
        inspector(state, r.inspector);
    if (r.hasPanel)
        panel(state, r.panel);
    statusBar(state, r.status);
    sashes(state, r, {viewport->Size.x, viewport->Size.y});
    deferred.run();
    saveLayoutIfChanged(state);
}
} // namespace yk::editor::ui
