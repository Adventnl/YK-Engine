#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Project.hpp"
#include "yk/gameplay/Exploration.hpp"
#include "yk/items/Inventory.hpp"
#include "yk/runtime/GameSession.hpp"
#include "yk/scene/SceneSerializer.hpp"

using namespace yk;

int main() {
    ComponentRegistry registry;
    registerStandardComponents(registry);
    auto loaded = Project::load(YK_EXPLORATION_PROJECT_DIR);
    CHECK(loaded);
    if (!loaded)
        return test::finish("invisible_castle_frontend");
    const auto project = loaded.value();
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
        return test::finish("invisible_castle_frontend");
    RuntimeOptions options;
    options.layers = project.layers;
    options.inputMap = project.input;
    options.assets = &assets;
    options.viewportSize = {1280, 720};
    options.transitionSeconds = 0;
    auto made = GameSession::create(std::move(first.value()), project.startScene, options, load);
    CHECK(made);
    if (!made)
        return test::finish("invisible_castle_frontend");
    auto session = std::move(made.value());
    InputFrame input;
    auto tick = [&](int count = 1) {
        for (int i = 0; i < count; ++i) {
            session->update(1.0 / 60.0, input);
            input.beginFrame();
        }
    };
    auto press = [&](Key key) {
        input.keyboard.set(key, true);
        tick();
        input.keyboard.set(key, false);
        tick();
    };
    auto click = [&](Vec2 point) {
        input.pointer = {point, {true, true, false}, true};
        tick();
        input.pointer.primary = {false, false, true};
        tick();
        input.pointer.inside = false;
        tick(2);
    };
    auto object = [&](const char *name) { return session->runtime().scene().findByName(name); };
    auto pauseMenu = [&]() { return object("Game pause menu")->get<PauseMenu>(); };
    auto pauseClick = [&](int row) {
        auto box = PauseMenu::buttonRect(session->runtime().viewportSize(), row);
        click(box.position + box.size * .5F);
    };
    tick(3);
    CHECK(session->scenePath() == "scenes/menu.ykscene");
    press(Key::P);
    CHECK(!session->paused());
    click({360, 465});
    CHECK(session->scenePath() == "scenes/controls.ykscene");
    press(Key::Escape);
    CHECK(session->scenePath() == "scenes/menu.ykscene");
    // A held Enter carries into the selector without starting a level.
    input.keyboard.set(Key::Enter, true);
    tick(30);
    CHECK(session->scenePath() == "scenes/levels.ykscene");
    input.keyboard.set(Key::Enter, false);
    tick();
    click({382, 380});
    CHECK(session->scenePath() == "scenes/courtyard.ykscene");
    for (const auto viewport : {Vec2{1280, 720}, Vec2{1600, 900}, Vec2{800, 450}}) {
        session->setViewportSize(viewport);
        press(Key::Escape);
        CHECK(session->paused());
        const auto time = session->runtime().tick();
        const auto hero = object("Explorer")->worldPosition();
        const auto guard = object("Keep guard")->worldPosition();
        input.keyboard.set(Key::D, true);
        tick(120);
        input.keyboard.set(Key::D, false);
        CHECK(session->runtime().tick() == time);
        CHECK(object("Explorer")->worldPosition() == hero);
        CHECK(object("Keep guard")->worldPosition() == guard);
        pauseClick(0);
        CHECK(!session->paused());
    }
    session->setViewportSize({1280, 720});
    // Escape belongs to conversations and searches before it belongs to pause.
    session->runtime().teleport(*object("Explorer"),
                                object("Mara")->worldPosition() + Vec2{0, .9F});
    tick(3);
    press(Key::E);
    CHECK(session->runtime().inputLocked());
    press(Key::Escape);
    CHECK(!session->paused());
    CHECK(!session->runtime().inputLocked());
    session->runtime().teleport(*object("Explorer"),
                                object("Tool bench")->worldPosition() + Vec2{0, .9F});
    tick(3);
    press(Key::E);
    tick(30);
    const double progress = session->runtime().blackboard().number("search_ui_progress");
    press(Key::P);
    tick(120);
    CHECK(session->paused());
    CHECK(session->runtime().blackboard().number("search_ui_progress") == progress);
    press(Key::Escape);
    CHECK(!session->paused());
    press(Key::Escape);
    CHECK(!session->runtime().inputLocked());
    session->runtime().blackboard().set("tool_found", 1.0);
    press(Key::P);
    pauseClick(1);
    CHECK(!session->paused());
    CHECK(session->runtime().blackboard().number("tool_found") == 0);
    auto &pad = input.gamepads[0];
    pad.setConnected(true);
    auto padPress = [&](GamepadButton button) {
        pad.setButton(button, true);
        tick();
        pad.setButton(button, false);
        tick();
    };
    session->runtime().blackboard().set("tool_found", 1.0);
    padPress(GamepadButton::Start);
    CHECK(session->paused());
    padPress(GamepadButton::DPadDown);
    CHECK(pauseMenu()->selected == 1);
    padPress(GamepadButton::South);
    CHECK(!session->paused());
    CHECK(session->runtime().blackboard().number("tool_found") == 0);
    press(Key::P);
    press(Key::Down);
    press(Key::Down);
    CHECK(pauseMenu()->selected == 2);
    press(Key::Enter);
    CHECK(session->scenePath() == "scenes/levels.ykscene");
    press(Key::Num2);
    CHECK(session->scenePath() == "scenes/level2.ykscene");
    press(Key::P);
    pauseClick(3);
    CHECK(session->scenePath() == "scenes/menu.ykscene");
    press(Key::Enter);
    press(Key::Num2);
    CHECK(session->scenePath() == "scenes/level2.ykscene");
    // Finishing the final chapter offers a route back to the title.
    session->runtime().blackboard().set("l2_gate_open", 1.0);
    session->runtime().teleport(*object("Explorer"),
                                object("South escape passage")->worldPosition());
    tick(5);
    CHECK(session->runtime().blackboard().text("level_state") == "complete");
    tick(180);
    CHECK(session->scenePath() == "scenes/level2.ykscene");
    press(Key::Enter);
    CHECK(session->scenePath() == "scenes/menu.ykscene");
    press(Key::Enter);
    press(Key::Num1);
    press(Key::Tab);
    CHECK(session->scenePath() == "scenes/levels.ykscene");
    press(Key::Num2);
    press(Key::M);
    CHECK(session->scenePath() == "scenes/menu.ykscene");
    return test::finish("invisible_castle_frontend");
}
