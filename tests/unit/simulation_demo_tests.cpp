#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Project.hpp"
#include "yk/assets/Validation.hpp"
#include "yk/gameplay/Exploration.hpp"
#include "yk/runtime/GameSession.hpp"
#include "yk/scene/SceneSerializer.hpp"

using namespace yk;

int main() {
    ComponentRegistry registry;
    registerStandardComponents(registry);
    auto projectResult = Project::load(YK_SIMULATION_PROJECT_DIR);
    CHECK(projectResult);
    if (!projectResult)
        return test::finish("simulation_demo");
    Project project = projectResult.value();
    CHECK(!hasErrors(validateProject(project, registry)));
    ProjectAssets assets(project);
    auto load = [&](const std::string &file) -> Result<std::unique_ptr<Scene>> {
        const auto path = project.resolve(file);
        if (!path)
            return Error{path.error()};
        return loadScene(path.value(), registry);
    };
    const std::string scenePath = "scenes/night_shift.ykscene";
    auto scene = load(scenePath);
    CHECK(scene);
    if (!scene)
        return test::finish("simulation_demo");
    RuntimeOptions options;
    options.layers = project.layers;
    options.inputMap = project.input;
    options.assets = &assets;
    auto created = GameSession::create(std::move(scene.value()), scenePath, options, load);
    CHECK(created);
    if (!created)
        return test::finish("simulation_demo");
    auto session = std::move(created.value());
    Keyboard keyboard;
    auto tick = [&](int count = 1) {
        for (int i = 0; i < count; ++i) {
            InputFrame frame;
            frame.keyboard = keyboard;
            session->update(1.0 / 60.0, frame);
            keyboard.beginFrame();
        }
    };
    auto press = [&](Key key) {
        keyboard.set(key, true);
        tick();
        keyboard.set(key, false);
        tick();
    };
    auto object = [&](const char *name) { return session->runtime().scene().findByName(name); };
    auto player = [&]() { return object("Runner"); };
    auto teleport = [&](Vec2 at) {
        session->runtime().teleport(*player(), at);
        tick(3);
    };
    auto &blackboard = session->runtime().blackboard();
    tick(92); // Finish the short title card.

    CHECK(player() && player()->get<TopDownController>() && player()->get<Interactor>());
    CHECK(blackboard.text("level_state") == "playing");
    CHECK(object("Escape rules") && !object("Escape rules")->get<LevelFlow>()->completed());

    // Neither exit can be crossed before its corresponding action is finished.
    teleport({8.05F, 9.0F});
    press(Key::E);
    CHECK(!object("Cell gate")->get<StateGate>()->open());
    keyboard.set(Key::D, true);
    tick(55);
    keyboard.set(Key::D, false);
    tick(3);
    CHECK(player()->worldPosition().x < 8.6F);
    CHECK(blackboard.number("keycard") == 0.0);

    teleport({6.25F, 10.95F});
    press(Key::E);
    CHECK(blackboard.number("keycard") == 1.0);
    CHECK(!object("Keycard")->active());
    teleport({8.05F, 9.0F});
    press(Key::E);
    tick(25);
    CHECK(object("Cell gate")->get<StateGate>()->open());
    CHECK(!object("Cell gate")->get<Collider>()->enabled);
    keyboard.set(Key::D, true);
    tick(55);
    keyboard.set(Key::D, false);
    tick(3);
    CHECK(player()->worldPosition().x > 9.5F);

    // The panel needs the wire from the cell desk.
    teleport({14.3F, 6.6F});
    CHECK(!object("Control panel")->get<Interactable>()->available(session->runtime()));
    press(Key::E);
    CHECK(blackboard.number("power") == 0.0);
    teleport({4.65F, 6.9F});
    press(Key::E);
    CHECK(blackboard.number("wire") == 1.0);
    teleport({14.3F, 6.6F});
    press(Key::E);
    CHECK(blackboard.number("power") == 1.0);
    CHECK(!object("Escape rules")->get<LevelFlow>()->completed());

    teleport({20.05F, 9.0F});
    press(Key::E);
    tick(25);
    CHECK(object("Yard gate")->get<StateGate>()->open());
    keyboard.set(Key::D, true);
    tick(90);
    keyboard.set(Key::D, false);
    tick(4);
    CHECK(player()->worldPosition().x > 25.0F);
    CHECK(object("Escape rules")->get<LevelFlow>()->completed());
    CHECK(blackboard.text("level_state") == "complete");
    CHECK(blackboard.text("level_message") == "ESCAPED!");
    press(Key::R);
    tick(30);
    CHECK(blackboard.text("level_state") != "complete");
    CHECK(blackboard.number("keycard") == 0.0);
    CHECK(!object("Cell gate")->get<StateGate>()->open());
    return test::finish("simulation_demo");
}
