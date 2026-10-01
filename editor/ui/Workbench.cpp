#include "ui/Panels.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

// The frame of the editor, laid out like VS Code's current look: a title bar (menus, the Play
// controls and the project in a capsule, layout buttons) over a canvas with the parts drawn as thin
// outlined cards: the activity bar and side bar together, the editor groups with their tabs, the
// panel, and the inspector, with the status bar under them. The geometry comes from
// WorkbenchLayout (tested without a window); every part is one borderless ImGui window pinned to
// its card, so nothing floats or docks.
namespace yk::editor::ui {
namespace {
constexpr ImGuiWindowFlags regionFlags =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground;

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

Rect shrunk(Rect rect, float by) {
    return {{rect.position.x + by, rect.position.y + by},
            {std::max(0.0F, rect.size.x - 2.0F * by), std::max(0.0F, rect.size.y - 2.0F * by)}};
}

// The window of a part: its card inside the outline, transparent (the card is drawn behind).
bool beginRegion(const char *name, Rect card, ImVec2 padding = {0.0F, 0.0F},
                 ImGuiWindowFlags extra = 0) {
    const Rect inside = shrunk(card, 1.0F);
    ImGui::SetNextWindowPos(im(inside.position));
    ImGui::SetNextWindowSize(im(inside.size));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    const bool open = ImGui::Begin(name, nullptr, regionFlags | extra);
    ImGui::PopStyleVar(2);
    return open;
}

// A part drawn as a card on the canvas: filled, rounded and outlined by a hairline.
void drawCard(ImDrawList &list, Rect card, Color fill) {
    if (card.size.x < 2.0F || card.size.y < 2.0F)
        return;
    const ImVec2 low = im(card.position);
    const ImVec2 high{low.x + card.size.x, low.y + card.size.y};
    const float radius = dp(metrics::cardRadius);
    list.AddRectFilled(low, high, packed(fill), radius);
    list.AddRect({low.x + 0.5F, low.y + 0.5F}, {high.x - 0.5F, high.y - 0.5F}, packed(vs::border),
                 radius, 0, 1.0F);
}

// The canvas and the cards, under every window of the workbench.
void drawCanvas(const WorkbenchRegions &r, ImVec2 window, bool editorVisible) {
    ImDrawList &list = *ImGui::GetBackgroundDrawList();
    list.AddRectFilled({0.0F, 0.0F}, window, packed(vs::chromeBg));
    drawCard(list, r.leftCard, vs::chromeBg);
    if (editorVisible) {
        drawCard(list, r.cardA, vs::editorBg);
        if (r.hasGroupB)
            drawCard(list, r.cardB, vs::editorBg);
    }
    if (r.hasPanel)
        drawCard(list, r.panelCard, vs::chromeBg);
    if (r.hasInspector)
        drawCard(list, r.inspectorCard, vs::chromeBg);
}

// ---------------------------------------------------------------------------------- title bar
std::string fileName(const std::string &path) {
    return std::filesystem::path(path).filename().string();
}

// What the project capsule offers: switching projects, and the recent ones.
void projectMenu(EditorState &state) {
    const PopupLook look;
    if (ImGui::MenuItem("Open Project..."))
        state.guarded([&state] { showDialog(state, DialogKind::OpenProject); });
    markItem("project/open");
    if (ImGui::MenuItem("New Project..."))
        state.guarded([&state] { showDialog(state, DialogKind::NewProject); });
    markItem("project/new");
    const std::vector<std::string> recent = state.recent.paths();
    if (!recent.empty()) {
        ImGui::Separator();
        ImGui::TextColored(imColor(vs::textDim), "Recent");
        for (const std::string &path : recent) {
            const std::string label = std::filesystem::path(path).filename().string();
            if (ImGui::MenuItem(label.c_str()))
                state.guarded([&state, path] { state.openProject(path); });
            markItem("project/recent/" + path);
            tooltip(path);
        }
    }
    if (state.project) {
        ImGui::Separator();
        if (ImGui::MenuItem("Project Settings..."))
            showDialog(state, DialogKind::ProjectSettings);
        markItem("project/settings");
        if (ImGui::MenuItem("Close Project"))
            state.guarded([&state] { state.closeProject(); });
        markItem("project/close");
    }
}

// The capsule in the middle of the title bar: Play, Pause, Step, Stop and Restart on the left, the
// project and its open scene on the right (a menu to switch projects).
void projectCapsule(EditorState &state, Rect bar, float left, float width) {
    const float height = dp(26.0F);
    const float top = bar.position.y + (bar.size.y - height) * 0.5F;
    ImDrawList &list = *ImGui::GetWindowDrawList();
    const ImVec2 low{left, top}, high{left + width, top + height};
    const bool playing = state.playing();
    list.AddRectFilled(low, high, IM_COL32(28, 30, 32, 255), dp(7.0F));
    list.AddRect({low.x + 0.5F, low.y + 0.5F}, {high.x - 0.5F, high.y - 0.5F},
                 playing ? IM_COL32(202, 81, 0, 200) : packed(vs::border), dp(7.0F), 0, 1.0F);

    // Every button is placed by hand: inside a menu bar SameLine takes its line from the menus.
    const float button = dp(22.0F), spacing = dp(2.0F);
    const float buttonTop = top + (height - button) * 0.5F;
    int slot = 0;
    const auto place = [&] {
        ImGui::SetCursorScreenPos(
            {left + dp(4.0F) + static_cast<float>(slot++) * (button + spacing), buttonTop});
    };
    const bool canPlay = state.document != nullptr && state.project != nullptr;
    place();
    ImGui::BeginDisabled(!canPlay || playing);
    if (iconButton("toolbar/Play", Icon::Play, playing, "Play (F5)", packed(vs::success), button))
        state.startPlay();
    ImGui::EndDisabled();
    place();
    ImGui::BeginDisabled(!playing);
    if (iconButton("toolbar/Pause", Icon::Pause, playing && state.play->paused(), "Pause (F6)", 0,
                   button))
        state.togglePause();
    ImGui::EndDisabled();
    place();
    ImGui::BeginDisabled(!playing || !state.play->paused());
    if (iconButton("toolbar/Step", Icon::Step, false, "Step one tick (F10)", 0, button))
        state.stepPlay(InputFrame{});
    ImGui::EndDisabled();
    place();
    ImGui::BeginDisabled(!playing);
    if (iconButton("toolbar/Stop", Icon::Stop, false, "Stop (Shift+F5)", packed(vs::error), button))
        state.stopPlay();
    place();
    if (iconButton("toolbar/Restart", Icon::Restart, false, "Restart the scene (Ctrl+Shift+F5)", 0,
                   button))
        state.restartPlay();
    ImGui::EndDisabled();

    // The project: a button over the rest of the capsule.
    const float dividerX = left + dp(4.0F) + 5.0F * button + 4.0F * spacing + dp(6.0F);
    list.AddLine({dividerX, top + dp(6.0F)}, {dividerX, high.y - dp(6.0F)}, packed(vs::border));
    const float projectWidth = high.x - dividerX - dp(2.0F);
    if (projectWidth < dp(60.0F))
        return;
    ImGui::SetCursorScreenPos({dividerX + dp(1.0F), top});
    if (ImGui::InvisibleButton("##projectcapsule", {projectWidth, height}))
        ImGui::OpenPopup("project_menu");
    const bool hovered = ImGui::IsItemHovered();
    markItem("toolbar/Project");
    if (hovered)
        list.AddRectFilled({dividerX + dp(2.0F), top + dp(2.0F)},
                           {high.x - dp(3.0F), high.y - dp(2.0F)}, packed(vs::pillHover), dp(5.0F));
    const std::string projectName =
        state.project ? state.project->project().name : "No project open";
    std::string sceneName;
    if (state.project && state.document)
        sceneName = fileName(state.document->path()) + (state.document->dirty() ? " *" : "");
    ImGui::PushFont(fonts().semibold, 12.5F);
    const float nameWidth = ImGui::CalcTextSize(projectName.c_str()).x;
    ImGui::PopFont();
    const float textY = top + (height - ImGui::GetFontSize()) * 0.5F;
    float x = dividerX + dp(10.0F);
    ImGui::PushClipRect({dividerX, top}, {high.x - dp(22.0F), high.y}, true);
    ImGui::PushFont(fonts().semibold, 12.5F);
    list.AddText({x, textY}, packed(state.project ? vs::text : vs::textDim), projectName.c_str());
    ImGui::PopFont();
    x += nameWidth + dp(8.0F);
    if (!sceneName.empty()) {
        ImGui::PushFont(fonts().ui, 12.5F);
        list.AddText({x, textY}, packed(vs::textDim), sceneName.c_str());
        ImGui::PopFont();
    }
    ImGui::PopClipRect();
    drawIcon(list, Icon::ChevronDown, {high.x - dp(13.0F), top + height * 0.5F}, dp(13.0F),
             packed(vs::textDim));
    if (hovered && state.project)
        tooltip(state.project->project().root.string());
    if (ImGui::BeginPopup("project_menu")) {
        projectMenu(state);
        ImGui::EndPopup();
    }
}

void titleBar(EditorState &state, Rect rect) {
    const float bar = rect.size.y;
    const float pad = std::max(4.0F, (bar - ImGui::GetFontSize()) * 0.5F);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {dp(9.0F), pad});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
    ImGui::SetNextWindowPos(im(rect.position));
    ImGui::SetNextWindowSize(im(rect.size));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    const bool open = ImGui::Begin("##titlebar", nullptr, regionFlags | ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar(4);
    if (open && ImGui::BeginMenuBar()) {
        markWindow("panel/TitleBar");
        ImDrawList &list = *ImGui::GetWindowDrawList();
        // The mark of the application, then the menus.
        const float mark = dp(22.0F);
        const ImVec2 markLow{rect.position.x + dp(12.0F), rect.position.y + (bar - mark) * 0.5F};
        SDL_Texture *logo = state.renderer ? state.renderer->nativeTexture(state.logo) : nullptr;
        if (logo) {
            // The icon has the macOS grid's padding (100 px of 1024) around the tile; crop it.
            constexpr float tileLow = 100.0F / 1024.0F, tileHigh = 924.0F / 1024.0F;
            list.AddImageRounded(
                ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(logo))),
                markLow, {markLow.x + mark, markLow.y + mark}, {tileLow, tileLow}, {tileHigh, tileHigh},
                IM_COL32_WHITE, dp(5.0F));
        } else { // The logo did not load: a plain mark rather than a hole.
            list.AddRectFilled(markLow, {markLow.x + mark, markLow.y + mark}, packed(vs::focus),
                               dp(6.0F));
            ImGui::PushFont(fonts().semibold, 11.0F);
            const ImVec2 markText = ImGui::CalcTextSize("YK");
            list.AddText(
                {markLow.x + (mark - markText.x) * 0.5F, markLow.y + (mark - markText.y) * 0.5F},
                IM_COL32(255, 255, 255, 255), "YK");
            ImGui::PopFont();
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {dp(9.0F), pad});
        ImGui::Dummy({dp(12.0F) + mark + dp(6.0F), 1.0F});
        ImGui::SameLine(0.0F, 0.0F);
        drawMenus(state);
        const float menusEnd = ImGui::GetItemRectMax().x;
        ImGui::PopStyleVar();

        // Layout toggles at the right end.
        const float button = dp(24.0F);
        WorkbenchLayout &layout = state.layout;
        const float right = rect.position.x + rect.size.x - dp(10.0F);
        const float top = rect.position.y + (bar - button) * 0.5F;
        const float togglesLeft = right - 3.0F * button - 2.0F * dp(2.0F);
        const auto toggle = [&](int index, const char *id, Icon icon, const char *tip, bool &flag) {
            ImGui::SetCursorScreenPos(
                {togglesLeft + static_cast<float>(index) * (button + dp(2.0F)), top});
            if (iconButton(id, icon, false, tip, 0, button))
                flag = !flag;
        };
        toggle(0, "layout/Sidebar",
               layout.sideBarVisible ? Icon::SidebarLeft : Icon::SidebarLeftOff,
               "Toggle side bar (Ctrl+B)", layout.sideBarVisible);
        toggle(1, "layout/Panel", layout.panelVisible ? Icon::Panel : Icon::PanelOff,
               "Toggle panel (Ctrl+J)", layout.panelVisible);
        toggle(2, "layout/Inspector",
               layout.inspectorVisible ? Icon::SidebarRight : Icon::SidebarRightOff,
               "Toggle inspector (Ctrl+Alt+B)", layout.inspectorVisible);

        // The capsule sits in the middle of the window, or after the menus when they need the room.
        float width = std::clamp(rect.size.x * 0.36F, dp(330.0F), dp(540.0F));
        float left = rect.position.x + (rect.size.x - width) * 0.5F;
        left = std::max(left, menusEnd + dp(16.0F));
        width = std::min(width, togglesLeft - dp(16.0F) - left);
        if (width > dp(190.0F))
            projectCapsule(state, rect, left, width);
        ImGui::EndMenuBar();
    }
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

// The activity bar is the left strip of the left card: one icon per side bar view, the active one
// on a rounded highlight, and the settings gear at the bottom.
void activityBar(EditorState &state, Rect strip) {
    if (!beginRegion("##activity", strip)) {
        ImGui::End();
        return;
    }
    markWindow("panel/ActivityBar");
    WorkbenchLayout &layout = state.layout;
    ImDrawList &list = *ImGui::GetWindowDrawList();
    const float cell = ImGui::GetWindowWidth();
    const float slot = std::min(cell, dp(metrics::activityBar));
    ImGui::SetCursorPos({0.0F, dp(6.0F)});
    const auto entry = [&](const char *id, Icon icon, bool active, const std::string &tip) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::PushID(id);
        const bool clicked = ImGui::InvisibleButton("##activity", {cell, slot});
        const bool hovered = ImGui::IsItemHovered();
        markItem(id);
        ImGui::PopID();
        if (active || hovered) {
            const float inset = dp(5.0F);
            list.AddRectFilled(
                {at.x + inset, at.y + dp(2.0F)}, {at.x + cell - inset, at.y + slot - dp(2.0F)},
                packed(active ? vs::pill : vs::pillHover), dp(metrics::controlRadius + 1.0F));
        }
        drawIcon(list, icon, {at.x + cell * 0.5F, at.y + slot * 0.5F}, dp(22.0F),
                 packed(active || hovered ? vs::activityActive : vs::activityInactive));
        if (hovered && hoveredForTooltip())
            ImGui::SetTooltip("%s", shortcutText(tip.c_str()));
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
    ImGui::SetCursorPos({0.0F, ImGui::GetWindowHeight() - slot - dp(6.0F)});
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

// The "..." menu of a side bar view: only what the view really can do.
bool sideMoreMenu(EditorState &state) {
    const PopupLook look;
    bool any = false;
    switch (state.layout.sideView) {
    case SideView::Explorer: {
        any = true;
        const bool project = state.project != nullptr;
        if (ImGui::MenuItem("Import Assets...", nullptr, false, project))
            state.chooseAssetsToImport(nullptr);
        markItem("sidebar/more/import");
        if (ImGui::MenuItem("Refresh", nullptr, false, project))
            state.project->refresh();
        markItem("sidebar/more/refresh");
        if (ImGui::MenuItem("Show Project in File Manager", nullptr, false, project)) {
            const std::string url = toFileUrl(state.project->project().root);
            SDL_OpenURL(url.c_str());
        }
        markItem("sidebar/more/reveal");
        break;
    }
    case SideView::Scene: {
        any = true;
        const bool editing = state.document != nullptr && !state.playing();
        if (ImGui::MenuItem("Select All", shortcutText("Ctrl+A"), false, editing))
            state.document->selectAll();
        markItem("sidebar/more/selectall");
        if (ImGui::MenuItem("Deselect", nullptr, false,
                            editing && !state.document->selection().empty()))
            state.document->clearSelection();
        markItem("sidebar/more/deselect");
        if (ImGui::MenuItem("Frame All in Scene View", "Home", false, state.document != nullptr))
            state.interaction.frameAll();
        markItem("sidebar/more/frameall");
        break;
    }
    default:
        break;
    }
    return any;
}

bool sideHasMore(const EditorState &state) {
    return state.layout.sideView == SideView::Explorer || state.layout.sideView == SideView::Scene;
}

// The title row of a side bar view: its name, and the "..." button when the view has actions.
float sideBarHeader(EditorState &state, float width) {
    const float height = dp(metrics::sideBarHeader);
    ImDrawList &list = *ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetWindowPos();
    ImGui::PushFont(fonts().semibold, 13.0F);
    const ImVec2 textSize = ImGui::CalcTextSize(sideTitle(state.layout.sideView));
    list.AddText({at.x + dp(14.0F), at.y + (height - textSize.y) * 0.5F}, packed(vs::text),
                 sideTitle(state.layout.sideView));
    ImGui::PopFont();
    if (sideHasMore(state)) {
        const float button = dp(22.0F);
        ImGui::SetCursorPos({width - button - dp(8.0F), (height - button) * 0.5F});
        if (iconButton("sidebar/more", Icon::More, false, "More actions", 0, button))
            ImGui::OpenPopup("sidebar_more");
        if (ImGui::BeginPopup("sidebar_more")) {
            sideMoreMenu(state);
            ImGui::EndPopup();
        }
    }
    return height;
}

void sideBar(EditorState &state, Rect card) {
    if (!beginRegion("##sidebar", card)) {
        ImGui::End();
        return;
    }
    markWindow("panel/SideBar");
    const float header = sideBarHeader(state, ImGui::GetWindowWidth());
    ImGui::SetCursorPos({0.0F, header});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(8.0F), dp(6.0F)});
    const ImVec2 size{ImGui::GetWindowWidth(), ImGui::GetWindowHeight() - header};
    if (ImGui::BeginChild("##sidecontent", size, ImGuiChildFlags_AlwaysUseWindowPadding,
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
    ImGui::End();
}

// ------------------------------------------------------------------------------ editor groups
constexpr const char *gameKey = "Game";

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
        if (ImGui::MenuItem("Close", shortcutText("Ctrl+W")))
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

void editorGroup(EditorState &state, int group, Rect card) {
    const std::string name = group == 0 ? "##group0" : "##group1";
    if (!beginRegion(name.c_str(), card)) {
        ImGui::End();
        return;
    }
    markWindow(group == 0 ? "panel/Editor" : "panel/Editor2");
    if (!state.project) {
        if (group == 0)
            welcomePage(state);
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

    const float width = ImGui::GetWindowWidth();
    const float barHeight = dp(metrics::tabBar);
    const float tabTop = dp(4.0F);
    const ImVec2 origin = ImGui::GetWindowPos();
    ImDrawList &list = *ImGui::GetWindowDrawList();
    // The strip is a raised band with the card's rounded top corners; the selected tab is the color
    // of the editor below it, so it reads as part of it.
    list.AddRectFilled(origin, {origin.x + width, origin.y + barHeight}, packed(vs::raisedBg),
                       dp(metrics::cardRadius - 1.0F), ImDrawFlags_RoundCornersTop);
    const float toolbarWidth = 2.0F * dp(26.0F) + dp(8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        {dp(12.0F), (barHeight - tabTop - ImGui::GetFontSize()) * 0.5F});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, {dp(6.0F), 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
    ImGui::SetCursorPos({dp(6.0F), tabTop});
    const bool hasTabs = (showSceneTabs && !state.sceneTabs.empty()) || showGameTab;
    if (hasTabs &&
        ImGui::BeginChild("##tabs", {width - toolbarWidth - dp(6.0F), barHeight - tabTop},
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_NoBackground)) {
        if (ImGui::BeginTabBar((name + "_tabs").c_str(),
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
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          imColor(desired == path ? vs::text : vs::textDim));
                    const std::string label = std::string(glyphOf(Icon::Scene)) + "  " +
                                              fileName(path) + "###tab:" + path;
                    const bool selected = ImGui::BeginTabItem(label.c_str(), &open, flags);
                    ImGui::PopStyleColor();
                    markItem("tab/" + fileName(path));
                    tooltip(path);
                    tabContextMenu(state, path);
                    if (selected) {
                        shown = path;
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
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      imColor(desired == gameKey ? vs::text : vs::textDim));
                const std::string label = std::string(glyphOf(Icon::Play)) + "  Game###tab:game";
                const bool selected =
                    ImGui::BeginTabItem(label.c_str(), group == 1 ? &open : nullptr, flags);
                ImGui::PopStyleColor();
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
    }
    if (hasTabs)
        ImGui::EndChild();
    ImGui::PopStyleVar(3);

    // Editor actions at the right end of the strip.
    if (hasTabs) {
        const float button = dp(24.0F);
        ImGui::SetCursorPos({width - 2.0F * button - dp(10.0F), (barHeight - button) * 0.5F});
        const bool split = state.layout.split != EditorSplit::None;
        if (iconButton((group == 0 ? "editor/split" : "editor/split2"), Icon::Split, split,
                       split ? "Unsplit the editor (Ctrl+\\)" : "Split the editor right (Ctrl+\\)",
                       0, button))
            state.layout.split = split ? EditorSplit::None : EditorSplit::Right;
        ImGui::SameLine(0.0F, dp(2.0F));
        if (iconButton((group == 0 ? "editor/more" : "editor/more2"), Icon::More, false,
                       "More actions", 0, button))
            ImGui::OpenPopup("editor_more");
        if (ImGui::BeginPopup("editor_more")) {
            const PopupLook look;
            if (ImGui::MenuItem("Close All Scenes", nullptr, false, !state.sceneTabs.empty()))
                deferred.later([&state] {
                    for (const std::string &other : std::vector<std::string>(state.sceneTabs))
                        state.closeScene(other);
                });
            markItem("editor/more/closeall");
            if (ImGui::MenuItem("Save All", shortcutText("Ctrl+Alt+S"), false,
                                !state.playing() && state.anyDirty()))
                deferred.later([&state] { state.saveAll(); });
            markItem("editor/more/saveall");
            ImGui::Separator();
            if (ImGui::MenuItem(state.layout.split == EditorSplit::Down ? "Split Right"
                                                                        : "Split Down",
                                nullptr, false, true))
                state.layout.split = state.layout.split == EditorSplit::Down ? EditorSplit::Right
                                                                             : EditorSplit::Down;
            markItem("editor/more/splitdirection");
            ImGui::EndPopup();
        }
    }
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
    const ImVec2 contentSize{width, ImGui::GetWindowHeight() - (hasTabs ? barHeight : 0.0F)};
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
    ImGui::End();
}

// --------------------------------------------------------------------------------- inspector
// The right-hand card: an Inspector tab (the entity, file or scene) and a Debug tab (the running
// game), with the way to hide the card at the right end of the header.
void inspector(EditorState &state, Rect card) {
    if (!beginRegion("##inspector", card)) {
        ImGui::End();
        return;
    }
    markWindow("panel/Inspector");
    WorkbenchLayout &layout = state.layout;
    const float header = dp(metrics::panelTabBar);
    const float tabHeight = dp(26.0F);
    ImGui::SetCursorPos({dp(8.0F), (header - tabHeight) * 0.5F});
    if (pillTab("inspector-tab/Inspector", "Inspector",
                layout.inspectorView == InspectorView::Inspector, tabHeight))
        layout.inspectorView = InspectorView::Inspector;
    if (pillTab("inspector-tab/Debug", "Debug", layout.inspectorView == InspectorView::Debug,
                tabHeight, state.playing() ? std::string(" ") : std::string(), vs::success))
        layout.inspectorView = InspectorView::Debug;
    const float button = dp(24.0F);
    ImGui::SetCursorPos({ImGui::GetWindowWidth() - button - dp(8.0F), (header - button) * 0.5F});
    if (iconButton("inspector/close", Icon::Cross, false, "Hide the inspector (Ctrl+Alt+B)", 0,
                   button))
        layout.inspectorVisible = false;
    ImGui::SetCursorPos({0.0F, header});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(10.0F), dp(6.0F)});
    if (ImGui::BeginChild("##inspectorcontent",
                          {ImGui::GetWindowWidth(), ImGui::GetWindowHeight() - header},
                          ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None)) {
        if (layout.inspectorView == InspectorView::Debug)
            debugPanel(state);
        else
            inspectorPanel(state);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
}

// ------------------------------------------------------------------------------------- panel
void panel(EditorState &state, Rect card) {
    if (!beginRegion("##panel", card)) {
        ImGui::End();
        return;
    }
    markWindow("panel/Panel");
    WorkbenchLayout &layout = state.layout;
    const float bar = dp(metrics::panelTabBar);
    const float tabHeight = dp(26.0F);
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
    const std::vector<Tab> tabs = {{PanelView::Console, "Console",
                                    consoleErrors ? std::to_string(consoleErrors) : "", vs::error},
                                   {PanelView::Problems, "Problems",
                                    errors + warnings ? std::to_string(errors + warnings) : "",
                                    errors ? vs::error : vs::warning},
                                   {PanelView::Output, "Build Output", "", vs::info},
                                   {PanelView::Profiler, "Profiler", "", vs::info}};
    // Tabs that do not fit are cut off before the buttons at the right end, not drawn over them.
    const float buttonsLeft = ImGui::GetWindowWidth() - 2.0F * (dp(24.0F) + dp(2.0F)) - dp(10.0F);
    ImGui::PushClipRect(
        {ImGui::GetWindowPos().x, ImGui::GetWindowPos().y},
        {ImGui::GetWindowPos().x + buttonsLeft, ImGui::GetWindowPos().y + ImGui::GetWindowHeight()},
        true);
    ImGui::SetCursorPos({dp(8.0F), (bar - tabHeight) * 0.5F});
    for (const Tab &tab : tabs)
        if (pillTab((std::string("panel-tab/") + tab.label).c_str(), tab.label,
                    layout.panelView == tab.view, tabHeight, tab.badge, tab.badgeColor))
            layout.panelView = tab.view;
    ImGui::PopClipRect();
    // Actions at the right end: maximize and close.
    const float button = dp(24.0F);
    ImGui::SetCursorPos(
        {ImGui::GetWindowWidth() - 2.0F * (button + dp(2.0F)) - dp(6.0F), (bar - button) * 0.5F});
    if (iconButton("panel/Maximize", layout.panelMaximized ? Icon::ChevronDown : Icon::ChevronUp,
                   false, layout.panelMaximized ? "Restore panel size" : "Maximize panel", 0,
                   button))
        layout.panelMaximized = !layout.panelMaximized;
    ImGui::SameLine(0.0F, dp(2.0F));
    if (iconButton("panel/Close", Icon::Cross, false, "Close panel (Ctrl+J)", 0, button)) {
        layout.panelVisible = false;
        layout.panelMaximized = false;
    }
    ImGui::SetCursorPos({0.0F, bar});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(10.0F), dp(2.0F)});
    if (ImGui::BeginChild("##panelcontent",
                          {ImGui::GetWindowWidth(), std::max(0.0F, ImGui::GetWindowHeight() - bar)},
                          ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar)) {
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
    ImGui::End();
}

// ---------------------------------------------------------------------------------- status bar
struct StatusItem {
    std::string id;
    std::string text;
    Icon icon{Icon::Dot};
    bool hasIcon{};
    Color color{}; // Tint of the icon and text; alpha 0: the bar's own color.
    std::string tooltip;
    std::function<void()> onClick;
};

float drawStatusItems(const std::vector<StatusItem> &items, float x, float y, float height,
                      bool rightAligned, Color foreground) {
    ImDrawList &list = *ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    ImGui::PushFont(fonts().ui, 12.0F);
    std::vector<float> widths;
    float total = 0.0F;
    for (const StatusItem &item : items) {
        float w = ImGui::CalcTextSize(item.text.c_str()).x + dp(16.0F);
        if (item.hasIcon)
            w += dp(17.0F);
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
        if (hovered && item.onClick)
            list.AddRectFilled({tl.x, tl.y + dp(3.0F)},
                               {tl.x + widths[i], tl.y + height - dp(3.0F)},
                               IM_COL32(255, 255, 255, 26), dp(4.0F));
        const Color tint = item.color.a != 0 ? item.color : foreground;
        float textX = tl.x + dp(8.0F);
        if (item.hasIcon) {
            drawIcon(list, item.icon, {textX + dp(6.0F), tl.y + height * 0.5F}, dp(13.0F),
                     packed(tint));
            textX += dp(17.0F);
        }
        const ImVec2 textSize = ImGui::CalcTextSize(item.text.c_str());
        list.AddText({textX, tl.y + (height - textSize.y) * 0.5F}, packed(tint), item.text.c_str());
        if (hovered && !item.tooltip.empty() && hoveredForTooltip())
            ImGui::SetTooltip("%s", item.tooltip.c_str());
        if (clicked && item.onClick)
            item.onClick();
        at += widths[i];
    }
    ImGui::PopFont();
    return total;
}

// The status bar has no card of its own: quiet dim text on the canvas, like the reference, and the
// whole bar turns orange (paused: yellow) while the game runs, as VS Code's does while debugging.
void statusBar(EditorState &state, Rect rect) {
    Color foreground{168, 170, 172, 255}; // Quiet, but readable at 12 points.
    const bool playing = state.playing();
    ImGui::SetNextWindowPos(im(rect.position));
    ImGui::SetNextWindowSize(im(rect.size));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::Begin("##status", nullptr, regionFlags);
    ImGui::PopStyleVar(2);
    markWindow("panel/StatusBar");
    if (playing) {
        const bool paused = state.play->paused();
        foreground = paused ? Color{30, 30, 30, 255} : vs::textBright;
        ImGui::GetWindowDrawList()->AddRectFilled(
            im(rect.position), {rect.position.x + rect.size.x, rect.position.y + rect.size.y},
            packed(paused ? vs::statusPaused : vs::statusPlaying));
    }
    WorkbenchLayout &layout = state.layout;
    std::vector<StatusItem> left, right;
    if (state.project) {
        StatusItem project;
        project.id = "project";
        project.text = state.project->project().name;
        project.icon = Icon::FolderOpen;
        project.hasIcon = true;
        project.tooltip = state.project->project().root.string();
        project.onClick = [&state] {
            state.layout.sideView = SideView::Explorer;
            state.layout.sideBarVisible = true;
        };
        left.push_back(project);
    } else {
        StatusItem none;
        none.id = "project";
        none.text = "No project";
        none.icon = Icon::FolderOpen;
        none.hasIcon = true;
        left.push_back(none);
    }
    if (state.document && !playing) {
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
    if (playing) {
        StatusItem mode;
        mode.id = "mode";
        mode.text = state.play->paused() ? "PAUSED" : "PLAYING";
        mode.icon = state.play->paused() ? Icon::Pause : Icon::Play;
        mode.hasIcon = true;
        left.push_back(mode);
    }
    if (state.playerProcessRunning()) {
        StatusItem player;
        player.id = "player";
        player.text = "Player running";
        player.icon = Icon::Play;
        player.hasIcon = true;
        player.tooltip = "The game runs in the standalone player. Click to stop it.";
        player.onClick = [&state] { state.stopPlayerProcesses(); };
        left.push_back(player);
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
    if (state.document && !playing) {
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
        std::snprintf(text, sizeof text, "%ld%%",
                      std::lround(static_cast<double>(state.interaction.camera.percent())));
        zoom.text = text;
        zoom.tooltip = "Scene view zoom (click to fit the scene in the view)";
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
    drawStatusItems(left, dp(6.0F), 0.0F, height, false, foreground);
    drawStatusItems(right, rect.size.x - dp(6.0F), 0.0F, height, true, foreground);
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
    state.explorerFocused = false; // The Explorer sets it while it is drawn.
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    WorkbenchMetrics scaled;
    scaled.scale = displayScale();
    scaled.titleBar = metrics::titleBar;
    scaled.activityBar = metrics::activityBar;
    scaled.statusBar = metrics::statusBar;
    scaled.gap = metrics::gap;
    const ImVec2 window = viewport->Size;
    const WorkbenchRegions r = state.layout.regions({window.x, window.y}, scaled);
    // A panel that the layout maximizes hides the editor groups.
    const bool editorVisible = r.editor.size.y > 40.0F;
    drawCanvas(r, window, editorVisible);

    // The left card holds the activity bar and, beside it, the side bar.
    const float cell = std::min(dp(metrics::activityBar), r.leftCard.size.x);
    const Rect activityStrip{r.leftCard.position, {cell, r.leftCard.size.y}};
    titleBar(state, r.title);
    activityBar(state, activityStrip);
    if (r.hasSideBar) {
        const Rect sideCard{{r.leftCard.position.x + cell, r.leftCard.position.y},
                            {std::max(0.0F, r.leftCard.size.x - cell), r.leftCard.size.y}};
        ImDrawList &list = *ImGui::GetBackgroundDrawList();
        list.AddLine({sideCard.position.x + 0.5F, sideCard.position.y + 1.0F},
                     {sideCard.position.x + 0.5F, sideCard.position.y + sideCard.size.y - 1.0F},
                     packed(vs::border));
        sideBar(state, sideCard);
    }
    if (editorVisible) {
        editorGroup(state, 0, r.cardA);
        if (r.hasGroupB)
            editorGroup(state, 1, r.cardB);
    }
    if (r.hasInspector)
        inspector(state, r.inspectorCard);
    if (r.hasPanel)
        panel(state, r.panelCard);
    statusBar(state, r.status);
    sashes(state, r, window);
    deferred.run();
    saveLayoutIfChanged(state);
}
} // namespace yk::editor::ui
