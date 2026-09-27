#pragma once
#include "yk/graphics/Renderer.hpp"
#include "yk/physics/World.hpp"

namespace yk {
// Call between beginFrame and present. Conversion is explicit: physics meters -> renderer units.
Status drawPhysicsDebug(Renderer &renderer, const physics::World &world,
                        float unitsPerMeter = 100.0F, int layer = 1000);
} // namespace yk
