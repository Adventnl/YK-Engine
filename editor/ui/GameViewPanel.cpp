#include "ui/Panels.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>

namespace yk::editor::ui {
namespace {
constexpr float stripHeight = 30.0F;

void strip(EditorState &state, ImVec2 start) {
    ImGui::SetCursorPos({start.x + 8.0F, start.y + 4.0F});
    ImGui::AlignTextToFramePadding();
    if (state.playing()) {
        ImGui::TextColored(imColor(state.play->paused() ? palette::warning : palette::good), "%s",
                           state.play->paused() ? "PAUSED" : "PLAYING");
        ImGui::SameLine(0.0F, 14.0F);
        ImGui::TextColored(imColor(palette::dim), "tick %llu",
                           static_cast<unsigned long long>(state.play->runtime().tick()));
        ImGui::SameLine(0.0F, 14.0F);
        if (iconButton("game/Restart", Icon::Restart, false, "Restart the scene", 0, 22.0F))
            state.restartPlay();
        ImGui::SameLine();
        if (iconButton("game/PhysicsDebug", Icon::Entity, state.view.gamePhysicsDebug,
                       "Show physics shapes", 0, 22.0F))
            state.view.gamePhysicsDebug = !state.view.gamePhysicsDebug;
        ImGui::SameLine();
        if (iconButton("game/Colliders", Icon::Grid, state.view.gameColliders,
                       "Show collider outlines", 0, 22.0F))
            state.view.gameColliders = !state.view.gameColliders;
    } else {
        ImGui::TextColored(imColor(palette::dim),
                           "Preview of the game camera. Press Play (F5) to run.");
    }
}

void emptyFill(const char *text) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(origin, {origin.x + avail.x, origin.y + avail.y},
                                              IM_COL32(20, 22, 28, 255));
    ImGui::SetCursorScreenPos({origin.x + 24.0F, origin.y + avail.y * 0.4F});
    ImGui::TextDisabled("%s", text);
}
} // namespace

void gameViewPanel(EditorState &state) {
    ViewportPanel &panel = state.gameView;
    panel.visible = false;
    panel.hovered = false;
    panel.focused = false;
    if (!state.showGame)
        return;
    if (state.focusRequest == "Game") { // Also selects the tab when another one is showing.
        ImGui::SetNextWindowFocus();
        state.focusRequest.clear();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
    const bool open = ImGui::Begin(
        "Game", &state.showGame, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::End();
        return;
    }
    markWindow("panel/Game");
    panel.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const ImVec2 contentStart = ImGui::GetCursorPos(); // Below the tab bar when docked.

    const Scene *scene = state.visibleScene();
    if (!scene) {
        emptyFill(state.project ? "No scene is open." : "No project is open.");
        ImGui::End();
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
                ImGui::Image(image->texture, avail, {0.0F, 0.0F}, image->uv1);
                trackViewport(panel, origin, avail);
                panel.hovered = ImGui::IsItemHovered();
                widgets().mark("game/view", {panel.origin, panel.size});
                if (state.playing())
                    state.play->setViewportSize(pixels);
            }
    }
    ImGui::End();
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
    if (const Scene *scene = state.visibleScene())
        return drawScenePreview(*state.renderer, *state.sceneRenderer, *scene,
                                Rect{{0.0F, 0.0F}, pixels}, options);
    return success();
}
} // namespace yk::editor::ui
