#include "ui/EditorApp.hpp"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "imgui.h"
#include "ui/Panels.hpp"
#include "yk/core/Log.hpp"
#include <SDL3/SDL.h>
#include <algorithm>

namespace yk::editor {
EditorApp::EditorApp(const ComponentRegistry &registry, EditorOptions options, SDL_Window *window)
    : state_(registry, std::move(options)), window_(window) {}

EditorApp::~EditorApp() {
    if (imguiReady_) {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
}

Status EditorApp::initialize(Renderer &renderer) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    // Docking is the whole layout. Keyboard navigation stays off: arrow keys and WASD belong to the
    // scene view and to the game being played.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigDockingWithShift = false;
    iniPath_ = (state_.settingsDirectory / "layout.ini").string();
    io.IniFilename = state_.options.persistLayout ? iniPath_.c_str() : nullptr;
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer.nativeRenderer()) ||
        !ImGui_ImplSDLRenderer3_Init(renderer.nativeRenderer())) {
        ImGui::DestroyContext();
        return Error{"Cannot initialize Dear ImGui's SDL backends"};
    }
    imguiReady_ = true;
    ui::applyTheme();
    ImGuiStyle &style = ImGui::GetStyle();
    style.FontSizeBase = 15.0F;
    const float displayScale = SDL_GetWindowDisplayScale(window_);
    if (displayScale > 1.01F) {
        style.ScaleAllSizes(displayScale);
        style.FontScaleDpi = displayScale;
    }
    ui::widgets().setEnabled(state_.options.testHooks);
    return state_.initialize(renderer);
}

void EditorApp::onNativeEvent(const SDL_Event &event) {
    if (imguiReady_)
        ImGui_ImplSDL3_ProcessEvent(&event);
}

bool EditorApp::onCloseRequested() {
    if (state_.document && state_.document->dirty() && !state_.playing()) {
        state_.requestQuit(); // Ask about the unsaved changes first.
        return false;
    }
    return true;
}

bool EditorApp::update(const FrameContext &frame) {
    ++frames_;
    ui::widgets().beginFrame();
    if (hook_)
        hook_(*this);

    ImGuiIO &io = ImGui::GetIO();
    const double seconds = static_cast<double>(frame.delta.seconds);
    if (seconds > 0.0)
        state_.framesPerSecond =
            state_.framesPerSecond * 0.9F + static_cast<float>(1.0 / seconds) * 0.1F;

    // The game hears the keyboard only while its view has the user's attention.
    Keyboard gameInput;
    if (state_.playing() && !io.WantTextInput &&
        (state_.gameView.focused || state_.gameView.hovered) &&
        state_.dialog.kind == DialogKind::None)
        gameInput = frame.keyboard;
    state_.tick(state_.options.fixedStep ? 1.0 / 60.0 : seconds, gameInput);

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    if (state_.options.fixedStep)
        io.DeltaTime =
            1.0F / 60.0F; // The UI's clock (double-click windows, delays) is deterministic too.
    ImGui::NewFrame();

    if (state_.document)
        ui::settleEdits(*state_.document, state_.interaction.dragging());
    ui::drawShell(state_);
    ui::handleShortcuts(state_);
    ui::hierarchyPanel(state_);
    ui::inspectorPanel(state_);
    ui::sceneViewPanel(state_);
    ui::gameViewPanel(state_);
    ui::assetsPanel(state_);
    ui::consolePanel(state_);
    ui::drawWelcome(state_);
    ui::drawDialogs(state_);
    ImGui::Render();

    const std::string title = state_.windowTitle();
    if (title != title_) {
        title_ = title;
        SDL_SetWindowTitle(window_, title_.c_str());
    }
    return !state_.quit;
}

Status EditorApp::render(Renderer &renderer) {
    if (auto viewports = ui::renderViewports(state_); !viewports)
        return viewports;
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer.nativeRenderer());
    if (capture_) {
        if (auto saved = renderer.capture(*capture_); !saved) {
            captureError_ = saved.error();
            log(LogLevel::Error, "editor", "Screenshot failed: " + saved.error());
        }
        capture_.reset();
    }
    return success();
}
} // namespace yk::editor
