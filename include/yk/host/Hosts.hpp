#pragma once
#include "yk/scene/Registry.hpp"
#include <functional>

// The stock programs as functions, so a game with components of its own can build its own copies of
// them: the same player, editor and command line, with the game's components registered after the
// engine's and the gameplay library's.
//
//     // my_game_player.cpp
//     #include "yk/host/Hosts.hpp"
//     #include "MyGame.hpp"                      // void registerMyGame(yk::ComponentRegistry &)
//     int main(int argc, char **argv) { return yk::host::runPlayer(argc, argv, registerMyGame); }
//
// cmake/YkGame.cmake's yk_add_game_hosts() writes those three files for you (docs/BUILDING.md).
// Each function is the whole program: it parses argv, prints usage and returns the exit code.
namespace yk::host {
// Adds a game's components to a registry that already holds the standard ones.
using RegisterComponents = std::function<void(ComponentRegistry &)>;

// yk_player: runs a project as a game.
int runPlayer(int argc, char **argv, const RegisterComponents &registerGame = {});
// yk_editor: the editor (only in builds with YK_RUNTIME).
int runEditor(int argc, char **argv, const RegisterComponents &registerGame = {});
// yk: validate, format, info, components, export, targets.
int runTool(int argc, char **argv, const RegisterComponents &registerGame = {});
} // namespace yk::host
