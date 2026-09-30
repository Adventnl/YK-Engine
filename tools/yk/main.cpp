// yk: command-line tools for YK projects (validate, format, info, components, export, targets).
// The program itself is yk::host::runTool, so a game with components of its own can build its
// own copy (yk_add_game_hosts, docs/BUILDING.md).
#include "yk/host/Hosts.hpp"

int main(int argc, char **argv) {
    return yk::host::runTool(argc, argv);
}
