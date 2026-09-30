#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Project.hpp"
#include "yk/gameplay/Exploration.hpp"
#include "yk/runtime/GameSession.hpp"
#include "yk/scene/SceneSerializer.hpp"

using namespace yk;
int main() {
    ComponentRegistry registry;
    registerStandardComponents(registry);
    auto loadedProject = Project::load(YK_EXPLORATION_PROJECT_DIR);
    CHECK(loadedProject);
    if (!loadedProject)
        return test::finish("exploration");
    Project project = loadedProject.value();
    ProjectAssets assets(project);
    auto load = [&](const std::string &path) -> Result<std::unique_ptr<Scene>> {
        auto resolved = project.resolve(path);
        if (!resolved)
            return Error{resolved.error()};
        return loadScene(resolved.value(), registry);
    };
    auto first = load(project.startScene);
    CHECK(first);
    if (!first)
        return test::finish("exploration");
    RuntimeOptions options;
    options.layers = project.layers;
    options.inputMap = project.input;
    options.assets = &assets;
    auto created = GameSession::create(std::move(first.value()), project.startScene, options, load);
    CHECK(created);
    if (!created)
        return test::finish("exploration");
    auto session = std::move(created.value());
    Keyboard keyboard;
    auto tick = [&](int count = 1) {
        bool changed = false;
        for (int i = 0; i < count; ++i) {
            InputFrame frame;
            frame.keyboard = keyboard;
            changed = session->update(1.0 / 60.0, frame) || changed;
            keyboard.beginFrame();
        }
        return changed;
    };
    auto press = [&](Key key) {
        keyboard.set(key, true);
        tick();
        keyboard.set(key, false);
        tick();
    };
    auto actor = [&]() { return session->runtime().scene().findByName("Explorer"); };
    auto object = [&](const char *name) { return session->runtime().scene().findByName(name); };
    tick();
    CHECK(actor() && actor()->get<TopDownController>());
    CHECK(object("Mara") && object("Mara")->get<NpcPath>());
    // Four-way movement is normalized on a diagonal and blocked by solid walls.
    const Vec2 start = actor()->worldPosition();
    keyboard.set(Key::D, true);
    keyboard.set(Key::S, true);
    tick(30);
    keyboard.set(Key::D, false);
    keyboard.set(Key::S, false);
    tick(3);
    const Vec2 moved = actor()->worldPosition() - start;
    CHECK(moved.x > 0.3F && moved.y > 0.3F);
    CHECK(std::fabs(moved.x - moved.y) < 0.5F);
    session->runtime().teleport(*actor(), {18.0F, 11.0F});
    keyboard.set(Key::D, true);
    tick(90);
    keyboard.set(Key::D, false);
    tick(3);
    CHECK(actor()->worldPosition().x < 18.7F);
    // NPC conversation keeps the scene running and exposes two authored portrait expressions.
    session->runtime().teleport(*actor(), {8.2F, 9.0F});
    tick(2);
    press(Key::E);
    auto *dialogue = object("Mara")->get<Dialogue>();
    CHECK(dialogue && dialogue->active());
    CHECK(dialogue->currentPage().portrait.path == "assets/portraits/keeper.png");
    CHECK(session->runtime().inputLocked());
    tick(180);
    press(Key::E);
    CHECK(dialogue->active());
    CHECK(dialogue->currentPage().portrait.path == "assets/portraits/keeper_smile.png");
    tick(180);
    press(Key::E);
    CHECK(!dialogue->active());
    CHECK(!session->runtime().inputLocked());
    CHECK(session->runtime().blackboard().number("met_mara") == 1.0);
    // A one-time interaction unlocks a gate; opening removes its solid collider.
    const Vec2 maraBeforeSwitch = object("Mara")->worldPosition();
    session->runtime().teleport(*actor(), {12.2F, 15.0F});
    tick(2);
    press(Key::E);
    CHECK(session->runtime().blackboard().number("courtyard_switch") == 1.0);
    session->runtime().teleport(*actor(), {17.9F, 11.0F});
    tick(2);
    press(Key::E);
    tick(35);
    CHECK(distance(object("Mara")->worldPosition(), maraBeforeSwitch) > 0.15F);
    auto *gate = object("Castle gate")->get<StateGate>();
    CHECK(gate && gate->open());
    CHECK(!object("Castle gate")->get<Collider>()->enabled);
    CHECK(session->runtime().blackboard().number("courtyard_gate") == 1.0);
    // Both maps use the same runtime and select named arrival spawns. Kept flags survive the trip.
    session->runtime().teleport(*actor(), {24.0F, 11.0F});
    CHECK(tick(5));
    CHECK(session->scenePath() == "scenes/hall.ykscene");
    CHECK(session->runtime().blackboard().number("courtyard_gate") == 1.0);
    tick(2);
    CHECK(distance(actor()->worldPosition(), Vec2{4.0F, 11.0F}) < 0.6F);
    session->runtime().teleport(*actor(), {2.0F, 11.0F});
    CHECK(tick(5));
    CHECK(session->scenePath() == "scenes/courtyard.ykscene");
    tick(2);
    CHECK(distance(actor()->worldPosition(), Vec2{22.0F, 11.0F}) < 0.6F);
    CHECK(object("Castle gate")->get<StateGate>()->open());
    CHECK(!object("Brass switch")->get<Interactable>()->available(session->runtime()));
    // The same gate can also be driven by a signal from a plate, lever or event action.
    auto *returnedGate = object("Castle gate")->get<StateGate>();
    returnedGate->toggle(session->runtime());
    CHECK(!returnedGate->open());
    returnedGate->setSignal(EntityId{9999}, true);
    tick(2);
    CHECK(returnedGate->open());
    return test::finish("exploration");
}
