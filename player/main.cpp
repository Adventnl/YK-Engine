// yk_player: the stock player. The program itself is yk::host::runPlayer, so a game with components
// of its own can build its own copy (yk_add_game_hosts, docs/BUILDING.md).
#include "yk/host/Hosts.hpp"

int main(int argc, char **argv) {
    return yk::host::runPlayer(argc, argv);
}
