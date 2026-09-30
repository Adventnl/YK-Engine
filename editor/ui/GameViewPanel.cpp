#include "ui/Panels.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>

namespace yk::editor::ui {
namespace {
constexpr float stripHeight = 30.0F;

void strip(EditorState &state, ImVec2 start) {
    ImDrawList &list = *ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    list.AddRectFilled({origin.x, origin.y + start.y},
                       {origin.x + ImGui::GetWindowWidth(), origin.y + start.y + stripHeight},
                       packed(vs::editorBg));
    ImGui::SetCursorPos({start.x + 10.0F, start.y + 6.0F});
    ImGui::AlignTextToFramePadding();
    if (state.playing()) {
        const Color tone = state.play->paused() ? vs::warning : vs::success;
        ImGui::TextColored(imColor(tone), "%s", state.play->paused() ? "PAUSED" : "PLAYING");
        ImGui::SameLine(0.0F, 12.0F);
        ImGui::PushFont(fonts().mono, 12.0F);
        ImGui::TextColored(imColor(palette::dim), "tick %llu",
                           static_cast<unsigned long long>(state.play->runtime().tick()));
        ImGui::PopFont();
    } else {
        // The hint gives way to the buttons when the group is narrow (a split editor).
        const float room = ImGui::GetWindowWidth() - 4.0F * 26.0F - 30.0F;
        const char *hint = "Preview of the game camera. Press Play (F5) to run.";
        if (ImGui::CalcTextSize(hint).x > room)
            hint = "Game preview";
        if (ImGui::CalcTextSize(hint).x <= room)
            ImGui::TextColored(imColor(palette::dim), "%s", hint);
    }
    // Right-aligned toggles.
    const float button = 24.0F;
    ImGui::SameLine(ImGui::GetWindowWidth() - 4.0F * (button + 2.0F) - 8.0F);
    ImGui::SetCursorPosY(start.y + 3.0F);
    ImGui::BeginDisabled(!state.playing()); // There is nothing to restart until the game runs.
    if (iconButton("game/Restart", Icon::Restart, false, "Restart the scene", 0, button))
        state.restartPlay();
    ImGui::EndDisabled();
    ImGui::SameLine(0.0F, 2.0F);
    if (iconButton("game/PhysicsDebug", Icon::Entity, state.view.gamePhysicsDebug,
                   "Show physics shapes", 0, button))
        state.view.gamePhysicsDebug = !state.view.gamePhysicsDebug;
    ImGui::SameLine(0.0F, 2.0F);
    if (iconButton("game/Colliders", Icon::Grid, state.view.gameColliders, "Show collider outlines",
                   0, button))
        state.view.gameColliders = !state.view.gameColliders;
    ImGui::SameLine(0.0F, 2.0F);
    if (iconButton("game/Stats", Icon::Profiler, state.view.gameStats,
                   "Show rendering and physics statistics", 0, button))
        state.view.gameStats = !state.view.gameStats;
}

void emptyFill(const char *text) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(origin, {origin.x + avail.x, origin.y + avail.y},
                                              packed(vs::editorBg));
    ImGui::SetCursorScreenPos({origin.x + 24.0F, origin.y + avail.y * 0.4F});
    ImGui::TextDisabled("%s", text);
}

// A few lines of numbers in the corner of the game image, for spotting what costs time.
void statsOverlay(EditorState &state, const ViewportPanel &panel) {
    const SceneRenderStats &draw = state.sceneRenderer->stats();
    char text[256];
    if (state.play) {
        const auto physics = state.play->runtime().physics().stats();
        std::snprintf(text, sizeof text,
                      "sprites %zu (culled %zu)   quads %zu   particles %zu\nbodies %zu   entities "
                      "%zu   tick %llu",
                      draw.sprites, draw.culled, draw.quads, draw.particles,
                      static_cast<std::size_t>(physics.bodies),
                      state.play->runtime().scene().size(),
                      static_cast<unsigned long long>(state.play->runtime().tick()));
    } else {
        std::snprintf(text, sizeof text, "sprites %zu (culled %zu)   quads %zu   particles %zu",
                      draw.sprites, draw.culled, draw.quads, draw.particles);
    }
    ImDrawList &list = *ImGui::GetWindowDrawList();
    ImGui::PushFont(fonts().mono, 12.0F);
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 at{panel.origin.x + 8.0F, panel.origin.y + 8.0F};
    list.AddRectFilled({at.x - 6.0F, at.y - 4.0F}, {at.x + size.x + 6.0F, at.y + size.y + 4.0F},
                       IM_COL32(0, 0, 0, 160), 3.0F);
    list.AddText(at, packed(vs::text), text);
    ImGui::PopFont();
}
} // namespace

void gameViewPanel(EditorState &state) {
    ViewportPanel &panel = state.gameView;
    panel.visible = false;
    panel.hovered = false;
    panel.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const ImVec2 contentStart = ImGui::GetCursorPos(); // Below the tab bar.

    const Scene *scene = state.visibleScene();
    if (!scene) {
        emptyFill(state.project ? "No scene is open." : "No project is open.");
        return;
    }
    strip(state, contentStart);
    ImGui::SetCursorPos({contentStart.x, contentStart.y + stripHeight});
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::Dummy({0.0F, 0.0F}); // Keeps ImGui happy when no image fits yet.
    if (avail.x >= 8.0F && avail.y >= 8.0F) {
        const float dpi = std::max(1.0F, ImGui::GetIO().DisplayFramebufferScale.x);
        panel.scale = dpi;
        const Vec2 pixels{avail.x * dpi, avail.y * dpi};
        if (ensureTarget(state, panel, pixels))
            if (const auto image = targetImage(state, panel, pixels)) {
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                imageInCard(image->texture, avail, image->uv1);
                trackViewport(panel, origin, avail);
                ImGui::SetCursorScreenPos(origin);
                ImGui::InvisibleButton("##gameview", avail);
                panel.hovered = ImGui::IsItemHovered();
                widgets().mark("game/view", {panel.origin, panel.size});
                if (state.playing())
                    state.play->setViewportSize(pixels);
                if (state.view.gameStats)
                    statsOverlay(state, panel);
            }
    }
}

Status renderGameViewport(EditorState &state) {
    ViewportPanel &panel = state.gameView;
    if (!panel.visible || !state.renderer->valid(panel.texture))
        return success();
    const Vec2 pixels = panel.size * panel.scale;
    GameViewOptions options;
    options.target = panel.texture;
    options.physicsDebug = state.view.gamePhysicsDebug;
    options.colliders = state.view.gameColliders;
    if (state.play)
        return drawGameView(*state.renderer, *state.sceneRenderer, state.play->runtime(),
                            Rect{{0.0F, 0.0F}, pixels}, options);
    if (const Scene *scene = state.visibleScene()) {
        // No game is running, so no variables exist: {level_time} and friends show their
        // fallbacks (or nothing) instead of the raw placeholder.
        static const Blackboard noVariables;
        return drawScenePreview(*state.renderer, *state.sceneRenderer, *scene,
                                Rect{{0.0F, 0.0F}, pixels}, options, &noVariables);
    }
    return success();
}
} // namespace yk::editor::ui
