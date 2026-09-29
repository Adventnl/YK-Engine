#include "Modules.hpp"
#include "PrototypeGame.hpp"

namespace yk {
void registerAllModules(ComponentRegistry &registry) {
    registerEngineComponents(registry);
    registerGameplayComponents(registry);
    prototype::registerPrototypeGame(registry);
}
} // namespace yk
