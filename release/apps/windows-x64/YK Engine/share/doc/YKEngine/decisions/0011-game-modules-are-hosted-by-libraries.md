# ADR 0011: Game modules are hosted through libraries, not plugins

Date: 2026-09-29. Status: accepted.

## Context

There is no scripting language, so a behavior a game needs that the gameplay library does not have
is a C++ `Component`. Components are registered in a `ComponentRegistry` that the editor, the
player and the command line all build at startup. A game with components of its own therefore needs
its own copies of those programs.

## Decision

The programs are thin: `yk::host::runPlayer` and `yk::host::runEditor` take `argc`/`argv` and a
function that registers the game's components after the standard ones, and the stock `main`
functions are one line each. `yk_add_game_hosts()` (cmake/YkGame.cmake) turns a game module library
into `<name>_player` and `<name>_editor` executables. There is no dynamic loading: a game's
editor is built with its components, so the components' reflection, validation and export behave
exactly as in the stock tools, and there is no ABI to keep stable.

## Consequences

A game with C++ ships its own player and edits with its own editor build (the export takes the
player from a path or a template folder, so a game-specific player exports like any other). A
tiny example module (`tests/game_module`) is built and run by the tests so the mechanism stays
honest. Loading modules at run time, and scripting languages, remain future work.
