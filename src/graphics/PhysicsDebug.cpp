#include "yk/graphics/PhysicsDebug.hpp"

namespace yk {
Status drawPhysicsDebug(Renderer &renderer, const physics::World &world, float unitsPerMeter,
                        int layer) {
    if (!std::isfinite(unitsPerMeter) || unitsPerMeter <= 0)
        return Error{"Physics debug scale must be finite and positive"};
    const auto lines = world.debugLines();
    if (!lines)
        return Error{lines.error()};
    for (const auto &line : lines.value()) {
        Color color{90, 220, 110, 255};
        if (line.sensor)
            color = {255, 200, 70, 255};
        else if (line.bodyType == physics::BodyType::Static)
            color = {130, 150, 180, 255};
        else if (line.bodyType == physics::BodyType::Kinematic)
            color = {80, 170, 255, 255};
        else if (!line.awake)
            color = {100, 130, 100, 255};
        auto status = renderer.debugLine(line.first * unitsPerMeter, line.second * unitsPerMeter,
                                         color, layer);
        if (!status)
            return status;
    }
    return success();
}
} // namespace yk
