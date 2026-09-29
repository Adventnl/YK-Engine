#include "core/WorkbenchLayout.hpp"
#include <algorithm>
#include <cmath>

namespace yk::editor {
namespace {
struct SideName {
    SideView view;
    const char *text;
};
constexpr SideName sideNames[] = {{SideView::Explorer, "explorer"},
                                  {SideView::Scene, "scene"},
                                  {SideView::Prefabs, "prefabs"},
                                  {SideView::Components, "components"},
                                  {SideView::Build, "build"}};
struct PanelName {
    PanelView view;
    const char *text;
};
constexpr PanelName panelNames[] = {{PanelView::Console, "console"},
                                    {PanelView::Problems, "problems"},
                                    {PanelView::Output, "output"},
                                    {PanelView::Profiler, "profiler"}};

float number(const Json &json, const char *key, float fallback, float low, float high) {
    const Json &value = json.get(key);
    if (!value.isNumber() || !std::isfinite(value.asNumber()))
        return fallback;
    return std::clamp(static_cast<float>(value.asNumber()), low, high);
}
bool flag(const Json &json, const char *key, bool fallback) {
    const Json &value = json.get(key);
    return value.isBool() ? value.asBool() : fallback;
}
} // namespace

const char *name(SideView view) {
    for (const SideName &entry : sideNames)
        if (entry.view == view)
            return entry.text;
    return "scene";
}
const char *name(PanelView view) {
    for (const PanelName &entry : panelNames)
        if (entry.view == view)
            return entry.text;
    return "console";
}
const char *name(EditorSplit split) {
    switch (split) {
    case EditorSplit::Right:
        return "right";
    case EditorSplit::Down:
        return "down";
    case EditorSplit::None:
        break;
    }
    return "none";
}
std::optional<SideView> sideViewFromName(std::string_view text) {
    for (const SideName &entry : sideNames)
        if (text == entry.text)
            return entry.view;
    return std::nullopt;
}
std::optional<PanelView> panelViewFromName(std::string_view text) {
    for (const PanelName &entry : panelNames)
        if (text == entry.text)
            return entry.view;
    return std::nullopt;
}
std::optional<EditorSplit> editorSplitFromName(std::string_view text) {
    if (text == "none")
        return EditorSplit::None;
    if (text == "right")
        return EditorSplit::Right;
    if (text == "down")
        return EditorSplit::Down;
    return std::nullopt;
}

WorkbenchRegions WorkbenchLayout::regions(Vec2 size, const WorkbenchMetrics &metrics) const {
    const float s = std::max(0.25F, metrics.scale);
    WorkbenchRegions out;
    const float width = std::max(size.x, 1.0F);
    const float titleHeight = std::min(metrics.titleBar * s, size.y * 0.25F);
    const float statusHeight = std::min(metrics.statusBar * s, size.y * 0.25F);
    const float bodyTop = titleHeight;
    const float bodyHeight = std::max(0.0F, size.y - titleHeight - statusHeight);
    out.title = {{0.0F, 0.0F}, {width, titleHeight}};
    out.status = {{0.0F, size.y - statusHeight}, {width, statusHeight}};
    const float activityWidth = std::min(metrics.activityBar * s, width * 0.2F);
    out.activity = {{0.0F, bodyTop}, {activityWidth, bodyHeight}};

    // The side bar and the inspector give way before the editor gets narrower than its minimum.
    float sideWidth = sideBarVisible ? std::max(sideBarWidth, metrics.minSideBar * s) : 0.0F;
    float inspectorW = inspectorVisible ? std::max(inspectorWidth, metrics.minInspector * s) : 0.0F;
    const float room = std::max(0.0F, width - activityWidth - metrics.minEditor * s);
    if (sideWidth + inspectorW > room) {
        const float scale = room / std::max(sideWidth + inspectorW, 1.0F);
        sideWidth *= scale;
        inspectorW *= scale;
    }
    out.hasSideBar = sideWidth > 1.0F;
    out.hasInspector = inspectorW > 1.0F;
    sideWidth = out.hasSideBar ? sideWidth : 0.0F;
    inspectorW = out.hasInspector ? inspectorW : 0.0F;
    out.sideBar = {{activityWidth, bodyTop}, {sideWidth, bodyHeight}};
    out.inspector = {{width - inspectorW, bodyTop}, {inspectorW, bodyHeight}};
    const float columnLeft = activityWidth + sideWidth;
    const float columnWidth = std::max(0.0F, width - columnLeft - inspectorW);

    // The panel sits under the editor area only; maximized, it replaces the area.
    float panelH = 0.0F;
    if (panelVisible) {
        const float most = std::max(metrics.minPanel * s, bodyHeight - metrics.minEditorHeight * s);
        panelH = panelMaximized ? bodyHeight : std::clamp(panelHeight, metrics.minPanel * s, most);
        panelH = std::min(panelH, bodyHeight);
    }
    out.hasPanel = panelH > 1.0F;
    const float editorH = bodyHeight - panelH;
    out.editor = {{columnLeft, bodyTop}, {columnWidth, editorH}};
    out.panel = {{columnLeft, bodyTop + editorH}, {columnWidth, panelH}};

    out.groupA = out.editor;
    if (split != EditorSplit::None && out.editor.size.x > 1.0F && out.editor.size.y > 1.0F) {
        out.hasGroupB = true;
        const float ratio = std::clamp(splitRatio, 0.15F, 0.85F);
        if (split == EditorSplit::Right) {
            const float first = std::round(out.editor.size.x * ratio);
            out.groupA = {out.editor.position, {first, out.editor.size.y}};
            out.groupB = {{out.editor.position.x + first, out.editor.position.y},
                          {out.editor.size.x - first, out.editor.size.y}};
        } else {
            const float first = std::round(out.editor.size.y * ratio);
            out.groupA = {out.editor.position, {out.editor.size.x, first}};
            out.groupB = {{out.editor.position.x, out.editor.position.y + first},
                          {out.editor.size.x, out.editor.size.y - first}};
        }
    }
    return out;
}

void WorkbenchLayout::toggleSideView(SideView view) {
    if (sideBarVisible && sideView == view) {
        sideBarVisible = false;
        return;
    }
    sideView = view;
    sideBarVisible = true;
}

void WorkbenchLayout::togglePanelView(PanelView view) {
    if (panelVisible && panelView == view) {
        panelVisible = false;
        panelMaximized = false;
        return;
    }
    panelView = view;
    panelVisible = true;
}

Json WorkbenchLayout::toJson() const {
    Json json = Json::object();
    json.set("format", "yk.workbench");
    json.set("version", 1);
    json.set("sideBarVisible", sideBarVisible);
    json.set("sideBarWidth", sideBarWidth);
    json.set("sideView", name(sideView));
    json.set("inspectorVisible", inspectorVisible);
    json.set("inspectorWidth", inspectorWidth);
    json.set("panelVisible", panelVisible);
    json.set("panelHeight", panelHeight);
    json.set("panelMaximized", panelMaximized);
    json.set("panelView", name(panelView));
    json.set("split", name(split));
    json.set("splitRatio", splitRatio);
    return json;
}

WorkbenchLayout WorkbenchLayout::fromJson(const Json &json) {
    WorkbenchLayout layout;
    if (!json.isObject())
        return layout;
    layout.sideBarVisible = flag(json, "sideBarVisible", layout.sideBarVisible);
    layout.sideBarWidth = number(json, "sideBarWidth", layout.sideBarWidth, 120.0F, 2000.0F);
    if (const auto view = sideViewFromName(json.get("sideView").asString()))
        layout.sideView = *view;
    layout.inspectorVisible = flag(json, "inspectorVisible", layout.inspectorVisible);
    layout.inspectorWidth = number(json, "inspectorWidth", layout.inspectorWidth, 120.0F, 2000.0F);
    layout.panelVisible = flag(json, "panelVisible", layout.panelVisible);
    layout.panelHeight = number(json, "panelHeight", layout.panelHeight, 40.0F, 4000.0F);
    layout.panelMaximized = flag(json, "panelMaximized", layout.panelMaximized);
    if (const auto view = panelViewFromName(json.get("panelView").asString()))
        layout.panelView = *view;
    if (const auto split = editorSplitFromName(json.get("split").asString()))
        layout.split = *split;
    layout.splitRatio = number(json, "splitRatio", layout.splitRatio, 0.15F, 0.85F);
    return layout;
}
} // namespace yk::editor
