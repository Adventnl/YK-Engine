#include "core/EditorGeometry.hpp"
#include "ui/Panels.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>
#include <cmath>

namespace yk::editor::ui {
namespace {
constexpr float toolbarHeight = 30.0F;
constexpr float crumbHeight = 22.0F;

void dashedLine(ImDrawList &list, ImVec2 from, ImVec2 to, ImU32 color, float thickness = 1.0F,
                float dash = 6.0F, float gap = 4.0F) {
    const float dx = to.x - from.x, dy = to.y - from.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0F)
        return;
    const ImVec2 direction{dx / length, dy / length};
    for (float at = 0.0F; at < length; at += dash + gap) {
        const float end = std::min(at + dash, length);
        list.AddLine({from.x + direction.x * at, from.y + direction.y * at},
                     {from.x + direction.x * end, from.y + direction.y * end}, color, thickness);
    }
}

// Draws an arrow head at `tip` pointing along `direction` (unit).
void arrowHead(ImDrawList &list, ImVec2 tip, ImVec2 direction, ImU32 color, float size = 9.0F) {
    const ImVec2 side{-direction.y, direction.x};
    list.AddTriangleFilled(tip,
                           {tip.x - direction.x * size + side.x * size * 0.5F,
                            tip.y - direction.y * size + side.y * size * 0.5F},
                           {tip.x - direction.x * size - side.x * size * 0.5F,
                            tip.y - direction.y * size - side.y * size * 0.5F},
                           color);
}

// Everything the overlay needs to place things: the panel's screen origin and the view transform.
struct Canvas {
    ImDrawList &list;
    const SceneInteraction &ui;
    ImVec2 origin;
    ImVec2 at(Vec2 world) const {
        const Vec2 local = ui.toScreen(world);
        return {origin.x + local.x, origin.y + local.y};
    }
    float markerHalf() const {
        return std::max(0.3F, 10.0F * ui.metersPerPixel());
    }
};

void outline(const Canvas &canvas, const OrientedBox &box, ImU32 color, float thickness,
             bool dashed = false) {
    const auto corners = box.corners();
    ImVec2 points[4];
    for (std::size_t i = 0; i < 4; ++i)
        points[i] = canvas.at(corners[i]);
    if (!dashed) {
        canvas.list.AddPolyline(points, 4, color, ImDrawFlags_Closed, thickness);
        return;
    }
    for (std::size_t i = 0; i < 4; ++i)
        dashedLine(canvas.list, points[i], points[(i + 1) % 4], color, thickness);
}

// A diamond for entities that have no visible extent.
void marker(const Canvas &canvas, Vec2 world, ImU32 color) {
    const ImVec2 c = canvas.at(world);
    const float r = 7.0F;
    const ImVec2 points[4] = {{c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r}, {c.x - r, c.y}};
    canvas.list.AddPolyline(points, 4, color, ImDrawFlags_Closed, 1.5F);
}

void nameTag(const Canvas &canvas, const Scene &scene, const Entity &entity, ImU32 color) {
    const OrientedBox box = displayBox(entity, canvas.markerHalf());
    const Rect bounds = box.bounds();
    ImVec2 anchor = canvas.at(bounds.position);
    anchor.y -= ImGui::GetTextLineHeight() + 3.0F;
    const std::string text = entityLabel(scene, entity.id());
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    canvas.list.AddRectFilled({anchor.x - 3.0F, anchor.y - 1.0F},
                              {anchor.x + size.x + 3.0F, anchor.y + size.y + 1.0F},
                              IM_COL32(16, 18, 24, 190), 3.0F);
    canvas.list.AddText(anchor, color, text.c_str());
}

// Point where the segment from `from` (outside) to the middle of `box` crosses the box edge.
Vec2 entryPoint(Vec2 from, const OrientedBox &box) {
    if (box.contains(from))
        return box.center;
    float low = 0.0F, high = 1.0F;
    for (int i = 0; i < 18; ++i) {
        const float middle = (low + high) * 0.5F;
        (box.contains(lerp(from, box.center, middle)) ? high : low) = middle;
    }
    return lerp(from, box.center, high);
}

void link(const Canvas &canvas, const Scene &scene, const Link &connection, Color color,
          float alpha, float thickness) {
    const Entity *from = scene.find(connection.from);
    const Entity *to = scene.find(connection.to);
    if (!from || !to)
        return;
    const float half = canvas.markerHalf();
    const OrientedBox source = displayBox(*from, half), target = displayBox(*to, half);
    const Vec2 start = entryPoint(target.center, source);
    const Vec2 end = entryPoint(source.center, target);
    const ImVec2 a = canvas.at(start), b = canvas.at(end);
    const ImU32 packedColor = faded(color, alpha);
    // A slight bow keeps parallel links from overlapping.
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float length = std::max(1.0F, std::sqrt(dx * dx + dy * dy));
    const ImVec2 control{(a.x + b.x) * 0.5F - dy / length * length * 0.12F,
                         (a.y + b.y) * 0.5F + dx / length * length * 0.12F};
    canvas.list.AddBezierQuadratic(a, control, b, packedColor, thickness);
    ImVec2 direction{b.x - control.x, b.y - control.y};
    const float directionLength =
        std::max(1.0F, std::sqrt(direction.x * direction.x + direction.y * direction.y));
    direction = {direction.x / directionLength, direction.y / directionLength};
    arrowHead(canvas.list, b, direction, packedColor);
    canvas.list.AddCircleFilled(a, 3.0F, packedColor);
}

std::string handleName(const Handle &handle) {
    switch (handle.kind) {
    case HandleKind::Left:
        return "Left";
    case HandleKind::Right:
        return "Right";
    case HandleKind::Top:
        return "Top";
    case HandleKind::Bottom:
        return "Bottom";
    case HandleKind::TopLeft:
        return "TopLeft";
    case HandleKind::TopRight:
        return "TopRight";
    case HandleKind::BottomLeft:
        return "BottomLeft";
    case HandleKind::BottomRight:
        return "BottomRight";
    case HandleKind::Rotate:
        return "Rotate";
    case HandleKind::Ghost:
        return "Ghost" + std::to_string(handle.index);
    default:
        return "Body";
    }
}

void drawGizmoHandles(const Canvas &canvas, const EditorState &state) {
    const SceneInteraction &ui = state.interaction;
    const Handle hovered = ui.hovered();
    const Entity *primary = state.document->scene().find(state.document->primary());
    if (!primary)
        return;
    const float half = canvas.markerHalf();
    // The primary object's pivot.
    const ImVec2 pivot = canvas.at(primary->worldPosition());
    canvas.list.AddLine({pivot.x - 5, pivot.y}, {pivot.x + 5, pivot.y},
                        IM_COL32(255, 255, 255, 200));
    canvas.list.AddLine({pivot.x, pivot.y - 5}, {pivot.x, pivot.y + 5},
                        IM_COL32(255, 255, 255, 200));

    const auto ghosts = displacementGhosts(*primary, half);
    for (const Handle &handle : ui.handles()) {
        const bool hot = hovered.kind == handle.kind && hovered.index == handle.index;
        const ImVec2 c{canvas.origin.x + handle.screen.x, canvas.origin.y + handle.screen.y};
        widgets().mark("scene/handle/" + handleName(handle),
                       {vec(c) - Vec2{6.0F, 6.0F}, {12.0F, 12.0F}});
        const ImU32 fill = hot ? packed(palette::selection) : IM_COL32(245, 247, 252, 255);
        const ImU32 edge = IM_COL32(20, 22, 28, 255);
        switch (handle.kind) {
        case HandleKind::Rotate: {
            const OrientedBox box = displayBox(*primary, half);
            const float radians = degreesToRadians(box.rotationDegrees);
            const Vec2 top = box.center + rotated({0.0F, -box.half.y}, radians);
            const ImVec2 stem = canvas.at(top);
            canvas.list.AddLine(stem, c, IM_COL32(245, 247, 252, 200), 1.5F);
            canvas.list.AddCircleFilled(c, 7.0F, fill);
            canvas.list.AddCircle(c, 7.0F, edge, 16, 1.5F);
            drawIcon(canvas.list, Icon::Rotate, c, 10.0F, edge);
            break;
        }
        case HandleKind::Ghost: {
            if (handle.index < ghosts.size()) {
                const Ghost &ghost = ghosts[handle.index];
                const OrientedBox origin = displayBox(*primary, half);
                dashedLine(canvas.list, canvas.at(origin.center), c, faded(palette::ghost, 0.8F),
                           1.5F);
                outline(canvas, ghost.box, faded(palette::ghost, 0.9F), 1.5F, true);
                canvas.list.AddRectFilled(
                    canvas.at(ghost.box.bounds().position),
                    canvas.at(ghost.box.bounds().position + ghost.box.bounds().size),
                    faded(palette::ghost, 0.12F));
            }
            const ImVec2 diamond[4] = {
                {c.x, c.y - 8}, {c.x + 8, c.y}, {c.x, c.y + 8}, {c.x - 8, c.y}};
            canvas.list.AddConvexPolyFilled(
                diamond, 4, hot ? packed(palette::selection) : packed(palette::ghost));
            canvas.list.AddPolyline(diamond, 4, edge, ImDrawFlags_Closed, 1.5F);
            break;
        }
        default:
            canvas.list.AddRectFilled({c.x - 4.5F, c.y - 4.5F}, {c.x + 4.5F, c.y + 4.5F}, fill,
                                      1.5F);
            canvas.list.AddRect({c.x - 4.5F, c.y - 4.5F}, {c.x + 4.5F, c.y + 4.5F}, edge, 1.5F, 0,
                                1.5F);
            break;
        }
    }
}

void drawCameraFrame(const Canvas &canvas, const EditorState &state, const Scene &scene) {
    const Camera *camera = SceneRenderer::primaryCamera(scene);
    if (!camera)
        return;
    const CameraView view = camera->view();
    float aspect = 16.0F / 9.0F;
    if (state.project && state.project->project().window.height > 0)
        aspect = static_cast<float>(state.project->project().window.width) /
                 static_cast<float>(state.project->project().window.height);
    const Vec2 half{view.visibleHeight * aspect * 0.5F, view.visibleHeight * 0.5F};
    const OrientedBox frame{view.position, half, 0.0F};
    outline(canvas, frame, faded(palette::camera, 0.7F), 1.5F, true);
    const ImVec2 corner = canvas.at(view.position - half);
    canvas.list.AddText({corner.x + 4.0F, corner.y + 3.0F}, faded(palette::camera, 0.9F),
                        "Camera view");
    if (camera->clampToBounds) {
        const Vec2 low = camera->boundsMin, high = camera->boundsMax;
        outline(canvas, {(low + high) * 0.5F, (high - low) * 0.5F, 0.0F},
                faded(palette::camera, 0.35F), 1.0F, true);
    }
}

void drawOverlays(EditorState &state, const Canvas &canvas) {
    const Scene *scene = state.visibleScene();
    if (!scene)
        return;
    const float half = canvas.markerHalf();
    const ViewOptions &options = state.view;
    if (options.cameraFrame)
        drawCameraFrame(canvas, state, *scene);

    // Debug overlays: the box of every sprite, and where each entity's origin is.
    if (!state.playing() && (options.spriteBounds || options.pivots) && scene->size() < 4000) {
        for (const EntityId id : scene->hierarchyOrder()) {
            const Entity *entity = scene->find(id);
            if (!entity->activeInHierarchy() || entity->hiddenInHierarchy())
                continue;
            if (options.spriteBounds && entity->has<SpriteRenderer>())
                outline(canvas, displayBox(*entity, half), IM_COL32(120, 200, 255, 110), 1.0F);
            if (options.pivots) {
                const ImVec2 at = canvas.at(entity->worldPosition());
                const ImU32 tone = IM_COL32(255, 120, 120, 200);
                canvas.list.AddLine({at.x - 4.0F, at.y}, {at.x + 4.0F, at.y}, tone, 1.5F);
                canvas.list.AddLine({at.x, at.y - 4.0F}, {at.x, at.y + 4.0F}, tone, 1.5F);
            }
        }
    }

    // Deactivated entities keep a faint dashed outline so they can still be found and selected.
    if (!state.playing() && scene->size() < 3000) {
        for (const EntityId id : scene->hierarchyOrder()) {
            const Entity *entity = scene->find(id);
            if (!entity->activeInHierarchy())
                outline(canvas, displayBox(*entity, half), IM_COL32(160, 166, 180, 90), 1.0F, true);
        }
    }

    const auto selected = [&](EntityId id) {
        return state.playing() ? state.playSelection == id : state.document->isSelected(id);
    };
    std::vector<EntityId> selection;
    if (state.playing()) {
        if (state.playSelection)
            selection.push_back(state.playSelection);
    } else {
        selection = state.document->selection();
    }
    const EntityId primaryId = state.inspected();

    // Links: everything the selection drives, and what drives it.
    if (options.allLinks)
        for (const Link &connection : allLinks(*scene))
            link(canvas, *scene, connection, palette::linkOut, 0.35F, 1.25F);
    if (options.links)
        for (const EntityId id : selection) {
            for (const Link &connection : linksFrom(*scene, id))
                link(canvas, *scene, connection, palette::linkOut, 0.95F, 2.0F);
            for (const Link &connection : linksTo(*scene, id))
                link(canvas, *scene, connection, palette::linkIn, 0.95F, 2.0F);
        }

    // Hovered entity.
    const EntityId hoveredId = state.playing() ? EntityId{} : state.interaction.hoveredEntity();
    if (hoveredId && !selected(hoveredId) && !state.interaction.dragging())
        if (const Entity *entity = scene->find(hoveredId)) {
            outline(canvas, displayBox(*entity, half), packed(palette::hover), 1.5F);
            nameTag(canvas, *scene, *entity, IM_COL32(230, 234, 244, 255));
        }

    // Selection outlines.
    for (const EntityId id : selection) {
        const Entity *entity = scene->find(id);
        if (!entity)
            continue;
        const Color color = id == primaryId ? palette::selection : palette::selectionSecondary;
        const OrientedBox box = displayBox(*entity, half);
        outline(canvas, box, packed(color), id == primaryId ? 2.0F : 1.5F);
        if (!gizmoBox(*entity))
            marker(canvas, entity->worldPosition(), packed(color));
        nameTag(canvas, *scene, *entity, packed(color));
    }
    if (options.labels)
        for (const EntityId id : scene->hierarchyOrder())
            if (!selected(id) && id != hoveredId)
                nameTag(canvas, *scene, *scene->find(id), IM_COL32(190, 196, 210, 200));

    if (state.document && !state.playing())
        drawGizmoHandles(canvas, state);

    if (const auto area = state.interaction.marquee()) {
        const ImVec2 a = canvas.at(area->position), b = canvas.at(area->position + area->size);
        canvas.list.AddRectFilled(a, b, faded(palette::accent, 0.16F));
        canvas.list.AddRect(a, b, faded(palette::accent, 0.9F));
    }
}

void banner(const ViewportPanel &panel, const std::string &text, Color color) {
    ImDrawList &list = *ImGui::GetWindowDrawList();
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    const ImVec2 origin{panel.origin.x + (panel.size.x - size.x) * 0.5F, panel.origin.y + 10.0F};
    list.AddRectFilled({origin.x - 12.0F, origin.y - 5.0F},
                       {origin.x + size.x + 12.0F, origin.y + size.y + 5.0F}, faded(color, 0.92F),
                       6.0F);
    list.AddText(origin, IM_COL32(16, 18, 24, 255), text.c_str());
}

void separator() {
    ImGui::SameLine(0.0F, 6.0F);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine({origin.x, origin.y + 5.0F}, {origin.x, origin.y + 21.0F},
                                        packed(vs::border));
    ImGui::Dummy({1.0F, 26.0F});
    ImGui::SameLine(0.0F, 6.0F);
}

// One row above the view: the tools (move, resize, rotate), snapping, the overlays menu and the
// pointer position. Widget ids are stable ("toolbar/Move") so scripts and tests can find them.
void toolbar(EditorState &state, bool hoveredLastFrame, ImVec2 start) {
    ImDrawList &list = *ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    list.AddRectFilled({origin.x, origin.y + start.y},
                       {origin.x + ImGui::GetWindowWidth(), origin.y + start.y + toolbarHeight},
                       packed(vs::editorBg));
    ImGui::SetCursorPos({start.x + 6.0F, start.y + 2.0F});
    const bool editing = state.document != nullptr && !state.playing();
    SceneInteraction &interaction = state.interaction;
    ImGui::BeginDisabled(!editing);
    if (iconButton("toolbar/Move", Icon::Move, interaction.tool == Tool::Move, "Move (W)"))
        interaction.tool = Tool::Move;
    ImGui::SameLine(0.0F, 2.0F);
    if (iconButton("toolbar/Resize", Icon::Resize, interaction.tool == Tool::Resize, "Resize (R)"))
        interaction.tool = Tool::Resize;
    ImGui::SameLine(0.0F, 2.0F);
    if (iconButton("toolbar/Rotate", Icon::Rotate, interaction.tool == Tool::Rotate, "Rotate (E)"))
        interaction.tool = Tool::Rotate;
    separator();
    if (iconButton("toolbar/Snap", Icon::Snap, interaction.snap.enabled,
                   "Snap to grid (hold Ctrl while dragging to toggle)"))
        interaction.snap.enabled = !interaction.snap.enabled;
    ImGui::SameLine(0.0F, 2.0F);
    ImGui::SetNextItemWidth(70.0F);
    static constexpr float steps[] = {0.1F, 0.25F, 0.5F, 1.0F, 2.0F};
    char current[16];
    std::snprintf(current, sizeof current, "%g m", static_cast<double>(interaction.snap.grid));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6.0F, 2.0F});
    const bool comboOpen = ImGui::BeginCombo("##grid", current);
    markItem("toolbar/GridSize");
    if (comboOpen) {
        for (const float step : steps) {
            char item[16];
            std::snprintf(item, sizeof item, "%g m", static_cast<double>(step));
            if (ImGui::Selectable(item, std::abs(step - interaction.snap.grid) < 1e-4F))
                interaction.snap.grid = step;
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
    ImGui::EndDisabled();
    separator();
    if (iconButton("toolbar/Overlays", Icon::Layers, false, "Overlays: what the scene view draws"))
        ImGui::OpenPopup("overlays_menu");
    if (ImGui::BeginPopup("overlays_menu")) {
        ViewOptions &view = state.view;
        const auto item = [&](const char *id, const char *text, bool &value) {
            const bool clicked = ImGui::MenuItem(text, nullptr, value);
            markItem(std::string("overlay/") + id);
            if (clicked)
                value = !value;
        };
        item("Grid", "Grid", view.grid);
        item("Colliders", "Collider outlines", view.colliders);
        item("Sprites", "Sprite bounds", view.spriteBounds);
        item("Pivots", "Pivot points", view.pivots);
        item("Links", "Links of the selection", view.links);
        item("AllLinks", "Every link in the scene", view.allLinks);
        item("Names", "Entity names", view.labels);
        item("Camera", "Game camera frame", view.cameraFrame);
        ImGui::EndPopup();
    }
    ImGui::SameLine(0.0F, 2.0F);
    if (iconButton("scene/FrameAll", Icon::Target, false, "Frame the whole scene (Home)"))
        state.interaction.frameAll();

    const ImGuiIO &io = ImGui::GetIO();
    char text[96];
    if (hoveredLastFrame && state.interaction.bound()) {
        const Vec2 world = state.interaction.toWorld(vec(io.MousePos) - state.sceneView.origin);
        std::snprintf(text, sizeof text, "x %.2f   y %.2f   %d%%", static_cast<double>(world.x),
                      static_cast<double>(world.y),
                      static_cast<int>(state.interaction.camera.zoom / 48.0F * 100.0F));
    } else {
        std::snprintf(text, sizeof text, "%d%%",
                      static_cast<int>(state.interaction.camera.zoom / 48.0F * 100.0F));
    }
    const float width = ImGui::CalcTextSize(text).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - width - 12.0F);
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(fonts().mono, 12.0F);
    ImGui::TextColored(imColor(palette::dim), "%s", text);
    ImGui::PopFont();
}

// The path to the selected entity, like VS Code's breadcrumbs: scene > parent > entity. Every part
// selects its entity.
void breadcrumbs(EditorState &state, ImVec2 start) {
    ImDrawList &list = *ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float top = origin.y + start.y + toolbarHeight;
    list.AddRectFilled({origin.x, top}, {origin.x + ImGui::GetWindowWidth(), top + crumbHeight},
                       packed(vs::editorBg));
    ImGui::SetCursorPos({start.x + 10.0F, start.y + toolbarHeight + 2.0F});
    ImGui::PushStyleColor(ImGuiCol_Text, imColor(vs::textDim));
    const Scene *scene = state.visibleScene();
    const std::string sceneName =
        state.document ? std::filesystem::path(state.document->path()).filename().string() : "";
    ImGui::TextUnformatted(sceneName.empty() ? "Scene" : sceneName.c_str());
    if (scene && state.inspected() && scene->find(state.inspected())) {
        std::vector<EntityId> chain;
        for (const Entity *entity = scene->find(state.inspected()); entity;
             entity = entity->parent())
            chain.push_back(entity->id());
        std::reverse(chain.begin(), chain.end());
        for (const EntityId id : chain) {
            ImGui::SameLine(0.0F, 4.0F);
            drawIcon(*ImGui::GetWindowDrawList(), Icon::ChevronRight,
                     {ImGui::GetCursorScreenPos().x + 5.0F, ImGui::GetCursorScreenPos().y + 8.0F},
                     12.0F, packed(vs::textFaint));
            ImGui::Dummy({10.0F, 1.0F});
            ImGui::SameLine(0.0F, 2.0F);
            ImGui::PushID(static_cast<int>(id.value & 0x7fffffff));
            ImGui::TextUnformatted(entityLabel(*scene, id).c_str());
            const bool hovered = ImGui::IsItemHovered();
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && state.document &&
                !state.playing())
                state.document->select(id);
            markItem("crumb/" + entityLabel(*scene, id));
            if (hovered)
                ImGui::GetWindowDrawList()->AddLine(
                    {ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y}, ImGui::GetItemRectMax(),
                    packed(vs::text));
            ImGui::PopID();
        }
    }
    ImGui::PopStyleColor();
}

void handleInput(EditorState &state, ViewportPanel &panel) {
    const ImGuiIO &io = ImGui::GetIO();
    SceneInteraction &ui = state.interaction;
    const Vec2 mouse = vec(io.MousePos) - panel.origin;
    const Modifiers modifiers{io.KeyShift, io.KeyCtrl, io.KeyAlt};
    const bool hovered = panel.hovered;
    const bool spaceHeld =
        ImGui::IsKeyDown(ImGuiKey_Space) && !io.WantTextInput && !state.playing();

    if (hovered && io.MouseWheel != 0.0F)
        ui.zoomAt(mouse, std::pow(1.12F, io.MouseWheel));

    // Panning: middle or right drag, or Space + left drag.
    if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
                    ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                    (spaceHeld && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))) {
        panel.panning = true;
        panel.rightPress = vec(io.MousePos);
    }
    if (panel.panning) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
            ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Left))
            ui.panBy(vec(io.MouseDelta));
        else
            panel.panning = false;
    }
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        distance(vec(io.MousePos), panel.rightPress) < 4.0F && !state.playing() && state.document) {
        panel.contextWorld = snapTo(ui.toWorld(mouse), ui.snap.enabled ? ui.snap.grid : 0.0F);
        ImGui::OpenPopup("scene_context");
    }
    if (panel.panning)
        return;

    if (state.playing()) {
        // While the game runs the scene view only inspects.
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const auto stack =
                pickAll(*state.visibleScene(), ui.toWorld(mouse), ui.metersPerPixel());
            state.playSelection = stack.empty() ? EntityId{} : stack.front();
        }
        return;
    }
    if (!state.document)
        return;

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !spaceHeld) {
        if (state.pick) {
            const EntityId hit = ui.pickAt(mouse);
            const auto callback = state.pick->onPick;
            state.pick.reset();
            if (hit)
                callback(hit);
        } else {
            ui.pointerPressed(mouse, modifiers);
            panel.capturedLeft = true;
        }
    }
    if (panel.capturedLeft) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (io.MouseDelta.x != 0.0F || io.MouseDelta.y != 0.0F)
                ui.pointerMoved(mouse, modifiers);
        } else {
            ui.pointerReleased(mouse, modifiers);
            panel.capturedLeft = false;
        }
    } else if (hovered) {
        ui.pointerMoved(mouse, modifiers);
    }
}

void contextMenu(EditorState &state, ViewportPanel &panel) {
    if (!ImGui::BeginPopup("scene_context"))
        return;
    if (state.document) {
        if (ImGui::BeginMenu("Create Here")) {
            createEntityMenu(state, {}, panel.contextWorld);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Paste Here", "Ctrl+V")) {
            if (const char *text = ImGui::GetClipboardText(); text && *text)
                if (auto parsed = Json::parse(text))
                    if (auto pasted = state.document->paste(parsed.value(), {}, panel.contextWorld);
                        !pasted)
                        log(LogLevel::Warning, "editor", pasted.error());
        }
        const EntityId selected = state.document->primary();
        if (selected) {
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                state.document->duplicateSelection();
            if (ImGui::MenuItem("Delete", "Del"))
                state.document->deleteSelection();
        }
    }
    ImGui::EndPopup();
}

void emptyState(EditorState &state) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(origin, {origin.x + avail.x, origin.y + avail.y},
                                              packed(vs::editorBg));
    const char *headline = state.project ? "No scene is open" : "No project is open";
    const char *hint = state.project ? "Open a scene from the Assets panel or File > Open Scene."
                                     : "Create or open a project to start.";
    ImGui::SetCursorScreenPos({origin.x + 24.0F, origin.y + avail.y * 0.4F});
    ImGui::TextColored(imColor(palette::dim), "%s", headline);
    ImGui::SetCursorScreenPos({origin.x + 24.0F, origin.y + avail.y * 0.4F + 22.0F});
    ImGui::TextDisabled("%s", hint);
    if (state.project && !state.scenePaths().empty()) {
        ImGui::SetCursorScreenPos({origin.x + 24.0F, origin.y + avail.y * 0.4F + 52.0F});
        if (ImGui::Button("Open Start Scene")) {
            const std::string &start = state.project->project().startScene;
            state.openScene(start.empty() ? state.scenePaths().front() : start);
        }
        markItem("scene/OpenStart");
    }
}
} // namespace

bool ensureTarget(EditorState &state, ViewportPanel &panel, Vec2 pixels) {
    if (!state.renderer)
        return false;
    const auto roundUp = [](float value) { return std::ceil(value / 64.0F) * 64.0F; };
    const Vec2 wanted{std::clamp(roundUp(pixels.x), 64.0F, 8192.0F),
                      std::clamp(roundUp(pixels.y), 64.0F, 8192.0F)};
    const bool valid = state.renderer->valid(panel.texture);
    if (valid && panel.allocated.x >= pixels.x && panel.allocated.y >= pixels.y &&
        panel.allocated.x <= wanted.x + 256.0F && panel.allocated.y <= wanted.y + 256.0F)
        return true;
    if (valid)
        state.renderer->release(panel.texture);
    auto created =
        state.renderer->createRenderTarget(static_cast<int>(wanted.x), static_cast<int>(wanted.y));
    if (!created) {
        log(LogLevel::Error, "editor", "Cannot create a view: " + created.error());
        panel.texture = {};
        return false;
    }
    panel.texture = created.value();
    panel.allocated = wanted;
    return true;
}

std::optional<TargetImage> targetImage(EditorState &state, const ViewportPanel &panel,
                                       Vec2 pixels) {
    SDL_Texture *native = state.renderer ? state.renderer->nativeTexture(panel.texture) : nullptr;
    if (!native || panel.allocated.x <= 0.0F || panel.allocated.y <= 0.0F)
        return std::nullopt;
    return TargetImage{
        ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(native))),
        {pixels.x / panel.allocated.x, pixels.y / panel.allocated.y}};
}

void trackViewport(ViewportPanel &panel, ImVec2 origin, ImVec2 size) {
    panel.origin = vec(origin);
    panel.size = vec(size);
    panel.visible = true;
}

void sceneViewPanel(EditorState &state) {
    ViewportPanel &panel = state.sceneView;
    const bool hoveredLastFrame = panel.hovered;
    panel.visible = false;
    panel.hovered = false;
    panel.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const ImVec2 contentStart = ImGui::GetCursorPos(); // Below the tab bar.

    const Scene *scene = state.visibleScene();
    if (!scene) {
        emptyState(state);
        return;
    }
    toolbar(state, hoveredLastFrame, contentStart);
    breadcrumbs(state, contentStart);
    ImGui::SetCursorPos({contentStart.x, contentStart.y + toolbarHeight + crumbHeight});
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 8.0F || avail.y < 8.0F) {
        ImGui::Dummy({0.0F, 0.0F}); // The first frames of a group are tiny.
        return;
    }
    const float dpi = std::max(1.0F, ImGui::GetIO().DisplayFramebufferScale.x);
    panel.scale = dpi;
    const Vec2 pixels{avail.x * dpi, avail.y * dpi};
    if (ensureTarget(state, panel, pixels)) {
        if (const auto image = targetImage(state, panel, pixels)) {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Image(image->texture, avail, {0.0F, 0.0F}, image->uv1);
            trackViewport(panel, origin, avail);
            ImGui::SetCursorScreenPos(origin);
            ImGui::InvisibleButton("##sceneview", avail,
                                   ImGuiButtonFlags_MouseButtonLeft |
                                       ImGuiButtonFlags_MouseButtonMiddle |
                                       ImGuiButtonFlags_MouseButtonRight);
            panel.hovered = ImGui::IsItemHovered();
            widgets().mark("scene/view", {panel.origin, panel.size});

            state.interaction.viewport = panel.size;
            if (state.frameRequested) {
                state.interaction.frameAll();
                state.frameRequested = false;
            }
            handleInput(state, panel);

            if (state.document && !state.playing() && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("YK_PREFAB")) {
                    const std::string path(static_cast<const char *>(payload->Data),
                                           static_cast<std::size_t>(payload->DataSize) - 1);
                    const Vec2 mouse = vec(ImGui::GetIO().MousePos) - panel.origin;
                    state.instantiatePrefab(path, snapTo(state.interaction.toWorld(mouse),
                                                         state.interaction.snap.enabled
                                                             ? state.interaction.snap.grid
                                                             : 0.0F));
                }
                ImGui::EndDragDropTarget();
            }

            ImDrawList &list = *ImGui::GetWindowDrawList();
            list.PushClipRect(origin, {origin.x + avail.x, origin.y + avail.y}, true);
            drawOverlays(state, Canvas{list, state.interaction, origin});
            list.PopClipRect();
            if (state.pick)
                banner(panel, state.pick->prompt, palette::selection);
            else if (state.playing())
                banner(panel,
                       state.play->paused() ? "Paused - editing is disabled while playing"
                                            : "Playing - editing is disabled while playing",
                       palette::good);
            contextMenu(state, panel);
        }
    }
}

Status renderViewports(EditorState &state) {
    ViewportPanel &panel = state.sceneView;
    const Scene *scene = state.visibleScene();
    if (panel.visible && scene && state.renderer->valid(panel.texture)) {
        const Vec2 pixels = panel.size * panel.scale;
        Camera2D camera;
        camera.position = state.interaction.camera.center;
        camera.setZoom(state.interaction.camera.zoom * panel.scale);
        RenderPass pass{camera, Rect{{0.0F, 0.0F}, pixels}, scene->settings.background,
                        panel.texture};
        if (auto begun = state.renderer->beginPass(pass); !begun)
            return begun;
        Status drawn = success();
        if (state.view.grid && !state.playing()) {
            const ViewCamera scaled{state.interaction.camera.center,
                                    state.interaction.camera.zoom * panel.scale};
            const Vec2 topLeft = scaled.toWorld({0.0F, 0.0F}, pixels);
            const Vec2 bottomRight = scaled.toWorld(pixels, pixels);
            float step = state.interaction.snap.grid > 0.0F ? state.interaction.snap.grid : 1.0F;
            while (step * scaled.zoom < 10.0F * panel.scale)
                step *= 2.0F;
            const auto firstX = static_cast<long>(std::floor(topLeft.x / step));
            const auto lastX = static_cast<long>(std::ceil(bottomRight.x / step));
            const auto firstY = static_cast<long>(std::floor(topLeft.y / step));
            const auto lastY = static_cast<long>(std::ceil(bottomRight.y / step));
            const auto colorFor = [](long index, Color axis) {
                if (index == 0)
                    return axis;
                return index % 5 == 0 ? palette::gridMajor : palette::gridMinor;
            };
            if (lastX - firstX < 600 && lastY - firstY < 600) {
                for (long i = firstX; i <= lastX && drawn; ++i) {
                    const float x = static_cast<float>(i) * step;
                    drawn = state.renderer->debugLine({x, topLeft.y}, {x, bottomRight.y},
                                                      colorFor(i, palette::axisY), -5000);
                }
                for (long i = firstY; i <= lastY && drawn; ++i) {
                    const float y = static_cast<float>(i) * step;
                    drawn = state.renderer->debugLine({topLeft.x, y}, {bottomRight.x, y},
                                                      colorFor(i, palette::axisX), -5000);
                }
            }
        }
        if (drawn)
            drawn = state.sceneRenderer->drawWorld(*state.renderer, *scene,
                                                   {camera, pixels, false, true});
        if (drawn && state.view.colliders)
            drawn = state.sceneRenderer->drawColliders(*state.renderer, *scene);
        const Status ended = state.renderer->endPass();
        if (!drawn)
            return drawn;
        if (!ended)
            return ended;
    }
    return renderGameViewport(state);
}
} // namespace yk::editor::ui
