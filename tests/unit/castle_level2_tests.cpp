#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Project.hpp"
#include "yk/gameplay/Exploration.hpp"
#include "yk/items/Crafting.hpp"
#include "yk/items/Inventory.hpp"
#include "yk/runtime/GameSession.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <cmath>

using namespace yk;

int main() {
    ComponentRegistry registry;
    registerStandardComponents(registry);
    auto loaded = Project::load(YK_EXPLORATION_PROJECT_DIR);
    CHECK(loaded);
    if (!loaded) return test::finish("castle_level2");
    auto project = loaded.value();
    ProjectAssets assets(project);
    auto load = [&](const std::string &path) -> Result<std::unique_ptr<Scene>> {
        auto file = project.resolve(path);
        if (!file) return Error{file.error()};
        return loadScene(file.value(), registry);
    };
    auto scene = load("scenes/level2.ykscene");
    CHECK(scene);
    if (!scene) return test::finish("castle_level2");
    RuntimeOptions options;
    options.layers = project.layers;
    options.inputMap = project.input;
    options.assets = &assets;
    options.viewportSize = {1280, 720};
    auto created = GameSession::create(std::move(scene.value()), "scenes/level2.ykscene", options, load);
    CHECK(created);
    if (!created) return test::finish("castle_level2");
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
    auto press = [&](Key key) { keyboard.set(key, true); tick(); keyboard.set(key, false); tick(); };
    auto object = [&](const char *name) { return session->runtime().scene().findByName(name); };
    auto actor = [&]() { return object("Explorer"); };
    auto inventory = [&]() { return actor()->get<Inventory>(); };
    auto &board = session->runtime().blackboard();
    auto teleport = [&](Vec2 at) { session->runtime().teleport(*actor(), at); tick(3); };
    auto near = [&](const char *name, Vec2 offset = {0, .9F}) { teleport(object(name)->worldPosition() + offset); };
    auto walk = [&](Vec2 destination) {
        for (int i = 0; i < 700 && distance(actor()->worldPosition(), destination) > .12F; ++i) {
            const Vec2 delta = destination - actor()->worldPosition();
            keyboard.set(Key::D, delta.x > .08F);
            keyboard.set(Key::A, delta.x < -.08F);
            keyboard.set(Key::S, delta.y > .08F);
            keyboard.set(Key::W, delta.y < -.08F);
            tick();
        }
        for (Key key : {Key::A, Key::D, Key::W, Key::S}) keyboard.set(key, false);
        tick(4);
        if (distance(actor()->worldPosition(), destination) >= .3F && board.text("level_state") != "complete")
            std::fprintf(stderr, "Walk blocked: at %.2f %.2f, goal %.2f %.2f\n", actor()->worldPosition().x, actor()->worldPosition().y, destination.x, destination.y);
        CHECK(distance(actor()->worldPosition(), destination) < .3F || board.text("level_state") == "complete");
    };
    auto search = [&](const char *name, bool reposition = true) {
        if (reposition) near(name);
        press(Key::E);
        CHECK(object(name)->get<SearchAction>()->active());
        CHECK(board.number("search_ui_busy") == 1);
        tick(195);
        CHECK(board.number("search_ui_busy") == 0);
        press(Key::E);
        CHECK(!session->runtime().inputLocked());
    };
    tick(3);
    CHECK(project.startScene == "scenes/level2.ykscene");
    CHECK(board.text("escape_title") == "BLACKTHORN KEEP / LEVEL 2");
    CHECK(inventory()->freeSlots() == 6);
    auto *camera = object("Player follow camera")->get<Camera>();
    CHECK(camera->mode == CameraMode::Follow && camera->targets.size() == 1);
    CHECK(camera->orthographicHeight < 13);
    CHECK(camera->viewportInset == Vec2(0, 140));
    // Camera follows actual movement, then clamps against the rendered world aspect.
    teleport({24, 14}); tick(90);
    CHECK(distance(camera->view().position, {24, 14}) < .02F);
    keyboard.set(Key::D, true); tick(50); keyboard.set(Key::D, false); tick(50);
    CHECK(distance(camera->view().position, actor()->worldPosition()) < .03F);
    for (Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}, Vec2{1024, 768}}) {
        session->runtime().setViewportSize(size);
        teleport({46, 25}); tick(90);
        const float scale = std::min(size.x / 1280, size.y / 720);
        const float halfWidth = camera->orthographicHeight * size.x / (size.y - 140 * scale) / 2;
        CHECK(std::abs(camera->view().position.x - (48 - halfWidth)) < .03F);
        CHECK(std::abs(camera->view().position.y - (28 - camera->orthographicHeight / 2)) < .03F);
    }
    session->runtime().setViewportSize({1280, 720});
    auto *guard = object("Control guard");
    guard->get<NpcPath>()->enabled = false;
    guard->get<PatrolWatch>()->enabled = false;
    // Physically walk from the starting cell through the common-room and workshop openings.
    teleport({3, 5.8F});
    walk({4.5F, 6.5F}); walk({4.5F, 10.5F}); walk({7, 10.5F}); walk({7, 12}); walk({8.5F, 12}); walk({8.5F, 14.5F});
    near("Mara"); press(Key::E);
    auto *dialogue = object("Mara")->get<Dialogue>();
    CHECK(dialogue->active());
    for (int i = 0; i < 6 && dialogue->active(); ++i) press(Key::E);
    CHECK(board.number("l2_met_mara") == 1);
    teleport({8.5F, 14.5F}); walk({16.5F, 14.5F}); walk({16.5F, 20}); walk({16.5F, 23.5F}); walk({14, 23.5F}); walk({14, 22.9F});
    press(Key::E); tick(30);
    CHECK(board.number("search_ui_busy") == 1);
    CHECK(actor()->get<AnimatedSprite>()->state() == "search_up");
    press(Key::Escape);
    CHECK(inventory()->count(ItemId("l2_screwdriver")) == 0);
    CHECK(board.number("l2_tool_found") == 0);
    search("Tool bench", false);
    CHECK(inventory()->count(ItemId("l2_screwdriver")) == 1);
    CHECK(board.text("search_ui_icon") == "assets/level2/screwdriver.png");
    near("Tool bench"); press(Key::E); tick(190); press(Key::E);
    CHECK(inventory()->count(ItemId("l2_screwdriver")) == 1);
    walk({14.8F, 24}); walk({14.8F, 26.5F}); walk({13, 26.5F}); search("Tool chest", false);
    CHECK(inventory()->count(ItemId("l2_cutters")) == 1);
    // Workshop -> laundry and stores: all room entrances must really pass the feet collider.
    walk({16.5F, 25}); walk({16.5F, 20}); walk({16.5F, 14.5F}); walk({27.5F, 14.5F});
    walk({27.5F, 10.5F}); walk({27.5F, 7.8F}); walk({30.5F, 7.4F}); search("Linen table", false);
    CHECK(inventory()->count(ItemId("l2_linen")) == 1);
    walk({27.5F, 8}); walk({27.5F, 11}); walk({32, 14.5F}); walk({44.5F, 14.5F});
    walk({44.5F, 10.5F}); walk({44.5F, 7.9F}); walk({43.3F, 7.9F}); search("Fuse cabinet", false);
    CHECK(inventory()->count(ItemId("l2_fuse")) == 1);
    near("Power terminal"); press(Key::E);
    CHECK(object("Power terminal")->get<WirePuzzle>()->active());
    CHECK(!object("Power terminal")->get<WirePuzzle>()->ready());
    press(Key::Escape);
    search("Repair workbench");
    CHECK(inventory()->count(ItemId("l2_insulated_fuse")) == 1);
    CHECK(board.text("search_ui_icon") == "assets/level2/safe_fuse.png");
    CHECK(inventory()->count(ItemId("l2_fuse")) == 0 && inventory()->count(ItemId("l2_linen")) == 0);
    // Captured searches cancel after teleport; no remote reward, hidden tools remain.
    guard->get<PatrolWatch>()->enabled = true;
    session->runtime().teleport(*guard, {37.5F, 5});
    object("Security desk")->get<SearchAction>()->duration = 8;
    near("Security desk"); press(Key::E); tick(225);
    CHECK(distance(actor()->worldPosition(), {3, 5.8F}) < .1F);
    CHECK(!session->runtime().inputLocked());
    CHECK(inventory()->count(ItemId("l2_keycard")) == 0);
    CHECK(inventory()->count(ItemId("l2_insulated_fuse")) == 1);
    guard->get<PatrolWatch>()->enabled = false;
    object("Security desk")->get<SearchAction>()->duration = 2.2F;
    search("Security desk"); CHECK(inventory()->count(ItemId("l2_keycard")) == 1);
    near("Power terminal"); press(Key::E);
    auto *puzzle = object("Power terminal")->get<WirePuzzle>();
    CHECK(puzzle->active() && puzzle->ready());
    press(Key::E); // Incorrect pair must not grant power.
    CHECK(board.number("l2_power_restored") == 0 && puzzle->connected() == 0);
    for (int source = 0; source < 4; ++source) {
        for (int i = 0; puzzle->target() != WirePuzzle::targetFor(puzzle->source()) && i < 4; ++i) press(Key::D);
        press(Key::E);
    }
    CHECK(!puzzle->active() && board.number("l2_power_restored") == 1);
    CHECK(!session->runtime().inputLocked());
    // Real walk through utility into the exit anteroom; gate stays solid until chain work ends.
    walk({39.5F, 23}); walk({44.5F, 23}); walk({45, 24.5F});
    keyboard.set(Key::S, true); tick(45); keyboard.set(Key::S, false); tick(4);
    CHECK(actor()->worldPosition().y < 26.9F);
    // The barrier seals the two-tile perimeter opening, including both frame edges.
    for (float x : {44.1F, 45.0F, 45.9F}) {
        teleport({x, 26.2F});
        keyboard.set(Key::S, true); tick(45); keyboard.set(Key::S, false); tick(4);
        CHECK(actor()->worldPosition().y < 26.9F);
        CHECK(board.text("level_state") == "playing");
    }
    near("Green exit gate", {0, -1.1F}); search("Green exit gate", false); tick(30);
    CHECK(board.number("l2_exit_open") == 1);
    CHECK(!object("Green exit gate")->get<Collider>()->enabled);
    walk({45, 27.6F}); tick(3);
    CHECK(board.text("level_state") == "complete");
    CHECK(board.text("escape_completion_title") == "THE INNER KEEP IS BEHIND YOU");
    press(Key::R); tick(3);
    CHECK(inventory()->freeSlots() == 6);
    CHECK(session->runtime().blackboard().number("l2_power_restored") == 0);
    CHECK(!object("Green exit gate")->get<StateGate>()->open());
    CHECK(!session->runtime().inputLocked());
    return test::finish("castle_level2");
}
