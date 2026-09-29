#pragma once
#include "yk/scene/Registry.hpp"

namespace yk {
// The one place that decides which components a tool or player knows: the engine's standard
// components, the reusable gameplay library and the linked game modules. To add another game
// module, link its library and register it here; the editor and player then both pick it up.
void registerAllModules(ComponentRegistry &registry);
} // namespace yk
