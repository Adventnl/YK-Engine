#pragma once
#include "ui/EditorState.hpp"
#include "yk/core/Application.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

struct SDL_Window;

namespace yk::editor {
// The editor as an application: owns Dear ImGui, builds every panel each frame and draws the scene
// and game views into their render targets.
class EditorApp final : public ApplicationLayer {
  public:
    EditorApp(const ComponentRegistry &registry, EditorOptions options, SDL_Window *window);
    ~EditorApp() override;

    Status initialize(Renderer &renderer) override;
    bool update(const FrameContext &frame) override;
    void onNativeEvent(const SDL_Event &event) override;
    bool onCloseRequested() override;
    Status render(Renderer &renderer) override;

    EditorState &state() {
        return state_;
    }
    SDL_Window *window() const {
        return window_;
    }
    // Runs once per frame after the widget registry has rolled over, before the UI is built: the
    // scripted driver injects input here.
    void setFrameHook(std::function<void(EditorApp &)> hook) {
        hook_ = std::move(hook);
    }
    // Saves the frame that is drawn next as a BMP (after the UI is on it).
    void requestCapture(std::filesystem::path path) {
        capture_ = std::move(path);
    }
    // Set when a requested capture failed.
    const std::string &captureError() const {
        return captureError_;
    }
    unsigned frames() const {
        return frames_;
    }
    // Asks the application loop to end after this frame.
    void quit() {
        state_.quit = true;
    }

  private:
    EditorState state_;
    SDL_Window *window_;
    std::string iniPath_;
    std::string title_;
    std::function<void(EditorApp &)> hook_;
    std::optional<std::filesystem::path> capture_;
    std::string captureError_;
    unsigned frames_{};
    bool imguiReady_{};
};
} // namespace yk::editor
