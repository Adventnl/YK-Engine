#include "ui/EditorApp.hpp"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "imgui.h"
#include "ui/Panels.hpp"
#include "yk/core/Diagnostics.hpp"
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
    // Every part of the editor is pinned by the workbench (WorkbenchLayout), so ImGui saves no
    // window placement of its own. Keyboard navigation stays off: arrow keys and WASD belong to the
    // scene view and to the game being played.
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.IniFilename = nullptr;
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer.nativeRenderer()) ||
        !ImGui_ImplSDLRenderer3_Init(renderer.nativeRenderer())) {
        ImGui::DestroyContext();
        return Error{"Cannot initialize Dear ImGui's SDL backends"};
    }
    imguiReady_ = true;
    ui::loadFonts(io);
    ui::applyTheme();
    ImGuiStyle &style = ImGui::GetStyle();
    style.FontSizeBase = ui::metrics::fontSize;
    const float displayScale =
        state_.options.uiScale > 0.0F ? state_.options.uiScale : SDL_GetWindowDisplayScale(window_);
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
    // Files dropped from the file manager: a project file opens the project, media files are
    // imported into the open one.
    if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr) {
        const std::filesystem::path dropped(event.drop.data);
        if (dropped.filename() == Project::fileName) {
            const std::filesystem::path project = dropped;
            state_.guarded([this, project] { state_.openProject(project); });
        } else if (state_.project) {
            state_.queueImport({dropped});
        }
    }
}

bool EditorApp::onCloseRequested() {
    if (state_.anyDirty() && !state_.playing()) {
        state_.requestQuit(); // Ask about the unsaved changes first.
        return false;
    }
    return true;
}

bool EditorApp::update(const FrameContext &frame) {
    ++frames_;
    if (!state_.options.debugCrash.empty() && frames_ == 8 &&
        !crashOnPurpose(state_.options.debugCrash))
        log(LogLevel::Error, "editor", "Unknown --debug-crash kind");
    ui::widgets().beginFrame();
    if (hook_)
        hook_(*this);

    ImGuiIO &io = ImGui::GetIO();
    const double seconds = static_cast<double>(frame.delta.seconds);
    if (seconds > 0.0) {
        state_.framesPerSecond =
            state_.framesPerSecond * 0.9F + static_cast<float>(1.0 / seconds) * 0.1F;
        auto &times = state_.frameMilliseconds;
        times.push_back(static_cast<float>(seconds * 1000.0));
        if (times.size() > EditorState::frameMillisecondsCapacity)
            times.erase(times.begin());
    }

    // The game hears the keyboard only while its view has the user's attention.
    InputFrame gameInput;
    if (state_.playing() && !io.WantTextInput &&
        (state_.gameView.focused || state_.gameView.hovered) &&
        state_.dialog.kind == DialogKind::None)
        gameInput = frame.input;
    state_.tick(state_.options.fixedStep ? 1.0 / 60.0 : seconds, gameInput);

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    if (state_.options.fixedStep)
        io.DeltaTime =
            1.0F / 60.0F; // The UI's clock (double-click windows, delays) is deterministic too.
    ImGui::NewFrame();

    if (state_.document)
        ui::settleEdits(*state_.document, state_.interaction.dragging());
    ui::drawWorkbench(state_);
    ui::handleShortcuts(state_);
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
