#include "SpinnerModule.hpp"

namespace spinner {
void Spinner::describe(yk::TypeBuilder<Spinner> &type) {
    type.category("Example").description(
        "Turns its entity at a steady rate (the example game module).");
    type.field("degreesPerSecond", &Spinner::degreesPerSecond).range(-720, 720, 1.0);
    type.field("clockwise", &Spinner::clockwise);
}

void Spinner::onFixedUpdate(yk::GameContext &, float seconds) {
    entity().transform().rotationDegrees += degreesPerSecond * seconds * (clockwise ? 1.0F : -1.0F);
}

void registerComponents(yk::ComponentRegistry &registry) {
    registry.add<Spinner>("Spinner");
}
} // namespace spinner
