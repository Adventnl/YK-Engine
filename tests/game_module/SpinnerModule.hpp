#pragma once
#include "yk/gameplay/Gameplay.hpp"

// A tiny game module: one component that is not in the engine. It exists to prove that a game with
// C++ of its own can build its own player, editor and command line (yk_add_game_hosts) and that its
// component behaves like any other in every tool: it shows up in the editor's catalog and
// Inspector, saves and loads, validates, and runs.
namespace spinner {
class Spinner final : public yk::Component {
  public:
    float degreesPerSecond{90.0F};
    bool clockwise{true};
    static void describe(yk::TypeBuilder<Spinner> &type);

    void onFixedUpdate(yk::GameContext &context, float seconds) override;
};

// Adds the module's components to a registry that already holds the standard ones.
void registerComponents(yk::ComponentRegistry &registry);
} // namespace spinner
