#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Project.hpp"
#include "yk/gameplay/Exploration.hpp"
#include "yk/items/Crafting.hpp"
#include "yk/items/Inventory.hpp"
#include "yk/runtime/GameSession.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
using namespace yk;
int main() {
    ComponentRegistry registry;
    registerStandardComponents(registry);
    auto loaded = Project::load(YK_EXPLORATION_PROJECT_DIR);
    CHECK(loaded);
    if (!loaded)
        return test::finish("exploration");
    Project project = loaded.value();
    ProjectAssets assets(project);
    auto load = [&](const std::string &path) -> Result<std::unique_ptr<Scene>> {
        auto resolved = project.resolve(path);
        if (!resolved)
            return Error{resolved.error()};
        return loadScene(resolved.value(), registry);
    };
    auto first = load("scenes/courtyard.ykscene");
    CHECK(first);
    if (!first)
        return test::finish("exploration");
    RuntimeOptions options;
    options.layers = project.layers;
    options.inputMap = project.input;
    options.assets = &assets;
    auto created =
        GameSession::create(std::move(first.value()), "scenes/courtyard.ykscene", options, load);
    CHECK(created);
    if (!created)
        return test::finish("exploration");
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
    auto actor = [&]() { return object("Explorer"); };
    auto inventory = [&]() { return actor()->get<Inventory>(); };
    auto &board = session->runtime().blackboard();
    auto near = [&](const char *name, Vec2 offset = Vec2{0, .9F}) {
        session->runtime().teleport(*actor(), object(name)->worldPosition() + offset);
        tick(2);
    };
    auto search = [&](const char *name) {
        near(name);
        press(Key::E);
        CHECK(object(name)->get<SearchAction>()->active());
        CHECK(board.number("search_ui_busy") == 1);
        const auto at = actor()->worldPosition();
        keyboard.set(Key::D, true);
        tick(5);
        keyboard.set(Key::D, false);
        CHECK(distance(at, actor()->worldPosition()) < .02F);
        tick(190);
        CHECK(board.number("search_ui_busy") == 0);
        CHECK(session->runtime().inputLocked());
        press(Key::E);
        CHECK(!session->runtime().inputLocked());
    };
    auto walkTo = [&](Vec2 destination) {
        for (int i = 0; i < 600 && distance(actor()->worldPosition(), destination) > .12F; ++i) {
            const Vec2 delta = destination - actor()->worldPosition();
            keyboard.set(Key::D, delta.x > .08F);
            keyboard.set(Key::A, delta.x < -.08F);
            keyboard.set(Key::S, delta.y > .08F);
            keyboard.set(Key::W, delta.y < -.08F);
            tick();
        }
        for (Key key : {Key::A, Key::D, Key::W, Key::S})
            keyboard.set(key, false);
        tick(3);
        if (distance(actor()->worldPosition(), destination) >= .3F &&
            board.text("level_state") != "complete")
            std::fprintf(stderr, "Walk blocked: at %.2f %.2f, goal %.2f %.2f\n",
                         actor()->worldPosition().x, actor()->worldPosition().y, destination.x,
                         destination.y);
        CHECK(distance(actor()->worldPosition(), destination) < .3F ||
              board.text("level_state") == "complete");
    };
    tick(2);
    for (const char *name : {"Cell door", "Stores door", "Office door", "Workshop door",
                             "Laundry door", "Archive door"})
        CHECK(!object(name));
    CHECK(inventory()->freeSlots() == 6);
    CHECK(board.number("inventory_hud") == 1);
    auto *animation = actor()->get<AnimatedSprite>();
    CHECK(animation);
    // Actual direction-specific sheets, and stationary facing after releasing movement.
    session->runtime().teleport(*actor(), {14, 10});
    for (auto [key, clip] : {std::pair{Key::W, "walk_up"},
                             {Key::S, "walk_down"},
                             {Key::A, "walk_left"},
                             {Key::D, "walk_right"}}) {
        keyboard.set(key, true);
        tick(5);
        CHECK(animation->state() == clip);
        const int frame = actor()->get<SpriteRenderer>()->frame;
        tick(8);
        CHECK(actor()->get<SpriteRenderer>()->frame != frame);
        keyboard.set(key, false);
        tick(8);
        CHECK(animation->state().starts_with("idle_"));
    }
    // Static torches and furniture cannot be pushed. Perimeter and closed gate have no seams.
    CHECK(!object("Fixed wall torch")->has<NpcPath>());
    CHECK(!object("Fixed wall torch")->has<RigidBody>());
    // The removed table must leave a direct route from the actual spawn,
    // through the cell opening and into the common room.
    CHECK(!object("Common room table"));
    session->runtime().teleport(*actor(), {3.5F, 4.5F});
    walkTo({3.5F, 9.5F});
    CHECK(!object("Common room heraldic rug")->has<Collider>());
    session->runtime().teleport(*actor(), {2, 8.4F});
    walkTo({2, 9.5F});
    const Vec2 cabinetAt = object("Locked cabinet")->worldPosition();
    near("Locked cabinet", {0, 1.5F});
    keyboard.set(Key::W, true);
    tick(80);
    keyboard.set(Key::W, false);
    tick(4);
    CHECK(object("Locked cabinet")->worldPosition() == cabinetAt);
    const auto *cabinetFoot = object("Locked cabinet")->get<Collider>();
    CHECK(actor()->getAll<Collider>().size() == 1);
    CHECK(actor()->worldPosition().y > cabinetAt.y + cabinetFoot->offset.y +
                                           cabinetFoot->size.y * .5F +
                                           actor()->get<Collider>()->size.y * .5F - .04F);
    for (float y : {8.8F, 9.3F, 10.0F}) {
        session->runtime().teleport(*actor(), {19.4F, y});
        keyboard.set(Key::D, true);
        tick(50);
        keyboard.set(Key::D, false);
        tick(3);
        CHECK(actor()->worldPosition().x < 19.8F);
    }
    // The restored south return joins the corridor wall. Its west side is
    // solid stone, so approach its exposed east edge instead of spawning inside it.
    for (float y : {10.7F, 11.2F}) {
        session->runtime().teleport(*actor(), {21.6F, y});
        keyboard.set(Key::A, true);
        tick(50);
        keyboard.set(Key::A, false);
        tick(3);
        CHECK(actor()->worldPosition().x > 21.2F);
    }
    // Searching through the stores' wall is refused.
    session->runtime().teleport(*actor(), {12.7F, 4.7F});
    tick();
    object("Locked cabinet")->get<Interactable>()->range = 4;
    press(Key::E);
    CHECK(!object("Locked cabinet")->get<SearchAction>()->active());
    object("Locked cabinet")->get<Interactable>()->range = 1.8F;
    near("Locked cabinet");
    press(Key::E);
    CHECK(board.number("search_ui_active") == 1 && board.number("search_ui_busy") == 0);
    CHECK(board.text("search_ui_message").find("LOCKED") != std::string::npos);
    press(Key::Escape);
    CHECK(!session->runtime().inputLocked());
    near("Mara");
    press(Key::E);
    auto *dialogue = object("Mara")->get<Dialogue>();
    CHECK(dialogue->active());
    for (int i = 0; dialogue->active() && i < 6; ++i)
        press(Key::E);
    CHECK(board.number("met_mara") == 1);
    // Traverse the workshop doorway. Cancelled actions give no loot or objective credit.
    session->runtime().teleport(*actor(), {8, 10});
    walkTo({8, 12});
    // Approach below the tool rack's complete silhouette, then turn into its aisle.
    walkTo({4.4F, 15.2F});
    walkTo({3, 15.2F});
    press(Key::E);
    tick(30);
    CHECK(board.number("search_ui_busy") == 1);
    CHECK(animation->state() == "search_up");
    CHECK(inventory()->count(ItemId("screwdriver")) == 0);
    press(Key::Escape);
    CHECK(!session->runtime().inputLocked());
    CHECK(board.number("tool_found") == 0);
    search("Tool bench");
    CHECK(inventory()->count(ItemId("screwdriver")) == 1);
    CHECK(board.text("search_ui_icon") == "assets/keep/screwdriver.png");
    near("Tool bench");
    press(Key::E);
    tick(190);
    press(Key::E);
    CHECK(inventory()->count(ItemId("screwdriver")) == 1); // No duplicate rewards.
    // Leave the tool rack through the work aisle, around the storage run.
    walkTo({6.4F, 14.6F});
    walkTo({6.4F, 13.3F});
    walkTo({8, 13.3F});
    walkTo({8, 12});
    walkTo({8, 9.8F});
    walkTo({10, 8.5F});
    walkTo({10, 6.2F});
    walkTo({9.5F, 5.6F});
    search("Locked cabinet");
    CHECK(inventory()->count(ItemId("rope")) == 1);
    near("Rope workbench");
    press(Key::E);
    CHECK(board.number("search_ui_busy") == 0);
    CHECK(board.text("search_ui_message").find("hook") != std::string::npos);
    press(Key::Escape);
    search("Laundry chest");
    CHECK(inventory()->count(ItemId("iron_hook")) == 1);
    search("Rope workbench");
    CHECK(inventory()->count(ItemId("grappling_line")) == 1);
    CHECK(inventory()->count(ItemId("rope")) == 0 && inventory()->count(ItemId("iron_hook")) == 0);
    // A patrol runs during searches. Exposure is recoverable and doesn't destroy collected tools.
    auto *guard = object("Keep guard");
    guard->get<NpcPath>()->enabled = false;
    session->runtime().teleport(*guard, {16, 5});
    session->runtime().teleport(*actor(), {17, 5});
    tick(175);
    CHECK(distance(actor()->worldPosition(), {3.5F, 4.5F}) < .1F);
    CHECK(board.text("interaction_notice").find("Caught") != std::string::npos);
    CHECK(inventory()->count(ItemId("grappling_line")) == 1);
    session->runtime().teleport(*guard, {12, 9});
    search("Warden desk");
    CHECK(inventory()->count(ItemId("brass_key")) == 1);
    near("Castle gate", {-1, 0});
    press(Key::E);
    tick(30);
    CHECK(object("Castle gate")->get<StateGate>()->open());
    CHECK(!object("Castle gate")->get<Collider>()->enabled);
    walkTo({23, 10});
    near("Drain grate");
    CHECK(board.text("level_state") == "playing"); // Walking around it is not an escape.
    search("Drain grate");
    tick(30);
    CHECK(board.number("drain_open") == 1);
    CHECK(!object("Drain grate")->get<Collider>()->enabled);
    walkTo({28.5F, 16.65F});
    tick(4);
    CHECK(board.text("level_state") == "complete");
    press(Key::R);
    tick(3);
    // Session restart replaces the runtime (and its blackboard).
    CHECK(session->runtime().blackboard().number("drain_open") == 0);
    CHECK(inventory()->count(ItemId("screwdriver")) == 0);
    CHECK(!object("Castle gate")->get<StateGate>()->open());
    CHECK(!session->runtime().inputLocked());
    // Completing chapter one and continuing loads the authored second chapter.
    session->runtime().blackboard().set("drain_open", 1.0);
    session->runtime().teleport(*actor(), {28.5F, 16.65F});
    tick(4);
    CHECK(session->runtime().blackboard().text("level_state") == "complete");
    press(Key::Enter);
    tick(60);
    CHECK(session->scenePath() == "scenes/level2.ykscene");
    CHECK(session->runtime().blackboard().text("inventory_title") == "INVISIBLE CASTLE / LEVEL 2");
    CHECK(inventory()->freeSlots() == 6);
    return test::finish("exploration");
}
