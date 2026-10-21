#include "support/check.hpp"
#include "yk/assets/Project.hpp"
#include "yk/components/Components.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/host/Hosts.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <string>
#include <vector>

using namespace yk;

namespace {
unsigned ticks{};
bool sawEscape{};

// Exercise the real player with SDL keyboard events, rather than --keys (which replaces input
// after the player's host shortcuts have run).
class EscapeProbe final : public Component {
  public:
    static void describe(TypeBuilder<EscapeProbe> &type) {
        type.description("Checks that Escape reaches gameplay without quitting the player.");
    }

    void onStart(GameContext &) override {
        int count = 0;
        SDL_Window **windows = SDL_GetWindows(&count);
        CHECK(windows && count == 1);
        if (windows && count == 1)
            window_ = SDL_GetWindowID(windows[0]);
        SDL_free(windows);
    }

    void onFixedUpdate(GameContext &game, float) override {
        ++ticks;
        if (ticks == 1) {
            SDL_Event focus{};
            focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
            focus.window.windowID = window_;
            CHECK(SDL_PushEvent(&focus));
            SDL_Event key{};
            key.type = SDL_EVENT_KEY_DOWN;
            key.key.windowID = window_;
            key.key.scancode = SDL_SCANCODE_ESCAPE;
            CHECK(SDL_PushEvent(&key));
        } else if (ticks == 2) {
            sawEscape = game.keyboard().state(Key::Escape).pressed;
            SDL_Event key{};
            key.type = SDL_EVENT_KEY_UP;
            key.key.windowID = window_;
            key.key.scancode = SDL_SCANCODE_ESCAPE;
            CHECK(SDL_PushEvent(&key));
        } else if (ticks == 3) {
            CHECK(game.keyboard().state(Key::Escape).released);
            SDL_Event close{};
            close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
            close.window.windowID = window_;
            CHECK(SDL_PushEvent(&close));
        }
    }

  private:
    SDL_WindowID window_{};
};

void registerProbe(ComponentRegistry &registry) {
    registry.add<EscapeProbe>("EscapeProbe");
}
} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "yk-player-input-test";
    std::filesystem::create_directories(root);
    ComponentRegistry registry;
    registerStandardComponents(registry);
    registerProbe(registry);
    auto project = Project::create(root, "Player Input Test");
    project.startScene = "main.ykscene";
    CHECK(project.save());
    Scene scene(registry);
    scene.createEntity("Camera").add<Camera>();
    scene.createEntity("Input probe").add<EscapeProbe>();
    CHECK(saveScene(scene, root / project.startScene));
    std::vector<std::string> words = {"yk_player",  "--project", root.string(), "--fixed",
                                      "--no-audio", "--frames",  "10"};
    std::vector<char *> arguments;
    for (auto &word : words)
        arguments.push_back(word.data());
    CHECK(host::runPlayer(static_cast<int>(arguments.size()), arguments.data(), registerProbe) ==
          0);
    CHECK(sawEscape);
    CHECK(ticks == 3); // Escape keeps running; the window close event still quits.
    std::filesystem::remove_all(root);
    return test::finish("player_input");
}
