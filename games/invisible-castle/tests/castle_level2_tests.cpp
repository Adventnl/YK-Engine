#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Project.hpp"
#include "yk/gameplay/Exploration.hpp"
#include "yk/items/Crafting.hpp"
#include "yk/items/Inventory.hpp"
#include "yk/runtime/GameSession.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <algorithm>
#include <array>
#include <cmath>

using namespace yk;

int main() {
    ComponentRegistry registry;
    registerStandardComponents(registry);
    auto loaded = Project::load(YK_EXPLORATION_PROJECT_DIR);
    CHECK(loaded);
    if (!loaded)
        return test::finish("castle_level2");
    auto project = loaded.value();
    ProjectAssets assets(project);
    auto load = [&](const std::string &path) -> Result<std::unique_ptr<Scene>> {
        auto file = project.resolve(path);
        if (!file)
            return Error{file.error()};
        return loadScene(file.value(), registry);
    };
    auto scene = load("scenes/level2.ykscene");
    CHECK(scene);
    if (!scene)
        return test::finish("castle_level2");
    RuntimeOptions options;
    options.layers = project.layers;
    options.inputMap = project.input;
    options.assets = &assets;
    options.viewportSize = {1280, 720};
    auto created =
        GameSession::create(std::move(scene.value()), "scenes/level2.ykscene", options, load);
    CHECK(created);
    if (!created)
        return test::finish("castle_level2");
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
    auto teleport = [&](Vec2 at) {
        session->runtime().teleport(*actor(), at);
        tick(3);
    };
    // Source landmarks follow the compact architecture; furniture checks below
    // use actual entity bounds because furnishings are authored independently.
    const auto at = [](float x, float y) {
        const auto compact = [](float value, const auto &spans) {
            float result = value;
            for (const auto &span : spans)
                result -= span[2] * std::clamp((value - span[0]) / (span[1] - span[0]), 0.0F, 1.0F);
            return result;
        };
        const std::array<std::array<float, 3>, 6> horizontal{{{76, 231, 40},
                                                              {262, 395, 28},
                                                              {425, 792, 92},
                                                              {824, 1131, 62},
                                                              {1162, 1414, 47},
                                                              {1446, 1597, 6}}};
        const std::array<std::array<float, 3>, 5> vertical{
            {{99, 229, 20}, {289, 337, 20}, {429, 511, 30}, {685, 714, 8}, {762, 823, 20}}};
        return Vec2{(compact(x, horizontal) - 40) / 35, (compact(y, vertical) - 25) / 35};
    };
    auto near = [&](const char *name, Vec2 offset = {0, .9F}) {
        const std::string target{name};
        if (offset == Vec2{0, .9F}) {
            if (target == "Linen table")
                offset = {.8F, .7F};
            if (target == "Repair workbench")
                offset = {0, .5F};
            if (target == "Fuse cabinet")
                offset = {.95F, .45F};
            if (target == "Security desk")
                offset = {1.5F, .75F};
            if (target == "Power terminal")
                offset = {1.2F, -.45F};
        }
        teleport(object(name)->worldPosition() + offset);
    };
    auto walk = [&](Vec2 destination) {
        for (int i = 0; i < 700 && distance(actor()->worldPosition(), destination) > .12F; ++i) {
            const Vec2 delta = destination - actor()->worldPosition();
            keyboard.set(Key::D, delta.x > .08F);
            keyboard.set(Key::A, delta.x < -.08F);
            keyboard.set(Key::S, delta.y > .08F);
            keyboard.set(Key::W, delta.y < -.08F);
            tick();
        }
        for (Key key : {Key::A, Key::D, Key::W, Key::S})
            keyboard.set(key, false);
        tick(4);
        if (distance(actor()->worldPosition(), destination) >= .3F &&
            board.text("level_state") != "complete")
            std::fprintf(stderr, "Walk blocked: at %.2f %.2f, goal %.2f %.2f\n",
                         actor()->worldPosition().x, actor()->worldPosition().y, destination.x,
                         destination.y);
        CHECK(distance(actor()->worldPosition(), destination) < .3F ||
              board.text("level_state") == "complete");
    };
    auto search = [&](const char *name, bool reposition = true) {
        if (reposition)
            near(name);
        press(Key::E);
        CHECK(object(name)->get<SearchAction>()->active());
        CHECK(board.number("search_ui_busy") == 1);
        tick(195);
        CHECK(board.number("search_ui_busy") == 0);
        press(Key::E);
        CHECK(!session->runtime().inputLocked());
    };
    tick(3);
    CHECK(project.startScene == "scenes/menu.ykscene");
    CHECK(board.text("inventory_title") == "INVISIBLE CASTLE / LEVEL 2");
    CHECK(inventory()->freeSlots() == 6);
    CHECK(board.text("current_room") == "DORMITORY NORTH");
    CHECK(actor()->get<SpriteRenderer>()->shadowSize.x > .5F);
    CHECK(actor()->getAll<Collider>().size() == 1);
    CHECK(object("Player follow camera")->get<Camera>()->pixelsPerUnit == 35);
    auto chapterOne = load("scenes/courtyard.ykscene");
    CHECK(chapterOne);
    if (chapterOne) {
        const auto *firstCamera = chapterOne.value()->findByName("Camera")->get<Camera>();
        CHECK(firstCamera->pixelsPerUnit == 35);
        CHECK(firstCamera->boundsMin.y <= -1.4F); // Include the whole projected north facade.
        for (const auto &[firstName, secondName] :
             {std::pair{"Your straw bed", "Dorm Bed Nw"}, std::pair{"Tool bench", "Tool bench"}}) {
            const auto *first = chapterOne.value()->findByName(firstName)->get<SpriteRenderer>();
            const auto *second = object(secondName)->get<SpriteRenderer>();
            CHECK(first->texture.path == second->texture.path && first->size == second->size);
        }
    }
    // Narrow front-facing wall returns must not be mistaken for long vertical columns.
    // Neither chapter may hide a furnishing's floor footprint inside solid masonry.
    auto clearFurniture = [&](const Scene &world) {
        for (const auto propId : world.orderedIds()) {
            const auto *prop = world.find(propId);
            const auto *sprite = prop->get<SpriteRenderer>();
            if (!sprite || (!sprite->texture.path.starts_with("assets/castle/pixel/") &&
                            !sprite->texture.path.starts_with("assets/castle/reference/") &&
                            !sprite->texture.path.starts_with("assets/castle/props/")))
                continue;
            for (const auto *feet : prop->getAll<Collider>()) {
                if (feet->isTrigger)
                    continue;
                const Vec2 center = prop->worldPosition() + feet->offset;
                for (const auto wallId : world.orderedIds()) {
                    const auto *wall = world.find(wallId);
                    if (!wall->hasTag("wall_solid"))
                        continue;
                    for (const auto *solid : wall->getAll<Collider>()) {
                        const Vec2 wallCenter = wall->worldPosition() + solid->offset;
                        const Vec2 radius = (feet->size + solid->size) * .5F;
                        const Vec2 delta = center - wallCenter;
                        const bool intersects = radius.x - std::abs(delta.x) > .06F &&
                                                radius.y - std::abs(delta.y) > .06F;
                        if (intersects)
                            std::fprintf(stderr, "Furniture in stone: %s / %s\n",
                                         prop->name().c_str(), wall->name().c_str());
                        CHECK(!intersects);
                    }
                }
            }
        }
    };
    clearFurniture(session->runtime().scene());
    if (chapterOne)
        clearFurniture(*chapterOne.value());
    // Every freestanding prop is solid in both chapters. Mounting artwork and
    // flat rugs/court lines are the only exceptions; control machines are not decor.
    auto solidFurniture = [&](const Scene &world) {
        for (auto id : world.orderedIds()) {
            const auto *entity = world.find(id);
            const auto *sprite = entity->get<SpriteRenderer>();
            if (!sprite ||
                (!sprite->texture.path.starts_with("assets/castle/pixel/") &&
                 !sprite->texture.path.starts_with("assets/castle/props/") &&
                 !sprite->texture.path.starts_with("assets/castle/reference/png/props/")))
                continue;
            const std::string &path = sprite->texture.path;
            if (path.find("/tile_") != std::string::npos || path.ends_with("banner.png") ||
                path.ends_with("corridor_runner.png") ||
                path.ends_with("arena_court_markings.png") ||
                path.ends_with("laundry_switch.png") ||
                path.ends_with("workshop_rack_top_crate.png"))
                continue;
            bool solid = false;
            for (const auto *collider : entity->getAll<Collider>())
                solid = solid || !collider->isTrigger;
            if (!solid)
                std::fprintf(stderr, "Missing furniture collision: %s\n", entity->name().c_str());
            CHECK(solid);
        }
    };
    solidFurniture(session->runtime().scene());
    if (chapterOne)
        solidFurniture(*chapterOne.value());
    // Shipping walls are native, reusable blocks. Pixel-level coverage of joins,
    // facade support and matching collision is checked by pixel_art_tests.py.
    int topBlocks = 0, faces = 0, solids = 0;
    for (auto id : session->runtime().scene().orderedIds()) {
        const auto *wall = session->runtime().scene().find(id);
        if (!wall->hasTag("masonry"))
            continue;
        if (wall->hasTag("wall_solid")) {
            CHECK(wall->has<Collider>() && !wall->has<SpriteRenderer>());
            ++solids;
        } else {
            const auto *art = wall->get<SpriteRenderer>();
            CHECK(art && art->size == Vec2{1, 1});
            CHECK(art && art->drawMode == SpriteDrawMode::Tiled);
            CHECK(!wall->has<Collider>());
            topBlocks += wall->hasTag("wall_top") ? 1 : 0;
            faces += wall->hasTag("wall_face") ? 1 : 0;
        }
    }
    CHECK(topBlocks > 100 && faces > 100 && solids > 20);
    CHECK(object("Store Upper Crates")->get<SpriteRenderer>()->size.y ==
          object("Corridor Crate West")->get<SpriteRenderer>()->size.y);
    CHECK(!object("Yard Bench South")); // One bench fits the compact yard's seating zone.
    CHECK(object("Corridor Weapon Rack West")->get<SpriteRenderer>()->texture.path ==
          object("Corridor Weapon Rack East")->get<SpriteRenderer>()->texture.path);
    // Both controls occupy the service bays above the final anteroom.
    // clearFurniture above checks their complete silhouettes against the walls.
    for (const char *name : {"Power terminal", "Emergency exit switch"}) {
        CHECK(object(name)->worldPosition().y > at(0, 660).y);
        CHECK(object(name)->worldPosition().y < at(0, 714).y);
    }
    // Walk into opaque parts from exposed sides. Compound rectangles follow the
    // silhouette; their union (not the first rectangle) is the physical object.
    for (const char *name : {"Utility Energy Device Pipes", "Dorm Bed Nw", "Infirmary Bed West",
                             "Power terminal", "Emergency exit switch", "Yard Bench North"}) {
        const auto *furniture = object(name);
        CHECK(furniture);
        if (!furniture)
            continue;
        const auto shapes = furniture->getAll<Collider>();
        const auto *feet =
            *std::max_element(shapes.begin(), shapes.end(), [](const auto *a, const auto *b) {
                return a->size.x * a->size.y < b->size.x * b->size.y;
            });
        const auto *art = furniture->get<SpriteRenderer>();
        CHECK(feet && art && art->shadowSize.x > 0);
        if (!feet || !art)
            continue;
        CHECK(furniture->getAll<Collider>().size() > 1);
        CHECK(session->runtime().bodyShapes(furniture->id()).size() ==
              furniture->getAll<Collider>().size());
        const physics::QueryFilter propQuery{project.layers.categoryBits("Player"),
                                             project.layers.categoryBits("Solid")};
        for (const auto *piece : furniture->getAll<Collider>()) {
            const auto found = session->runtime().physics().queryPoint(
                furniture->worldPosition() + piece->offset, propQuery);
            CHECK(found);
            bool bound = false;
            if (found)
                for (const auto &shape : found.value())
                    bound = bound || session->runtime().entityOfShape(shape) == furniture;
            CHECK(bound); // Every baked opaque rectangle must exist in the physics world.
        }
        const Vec2 center = furniture->worldPosition() + feet->offset;
        // The bed and power terminal sit beside the west wall. Their exposed
        // right side is reachable; the wall-side gap cannot contain an actor.
        const bool rightSide = std::string_view(name) == "Dorm Bed Nw" ||
                               std::string_view(name) == "Infirmary Bed West" ||
                               std::string_view(name) == "Power terminal";
        for (bool fromSide : {false, true}) {
            const Vec2 radius = (feet->size + actor()->get<Collider>()->size) * .5F;
            const Key towards = fromSide ? (rightSide ? Key::A : Key::D) : Key::W;
            teleport(center - actor()->get<Collider>()->offset +
                     (fromSide ? Vec2{(rightSide ? 1 : -1) * (radius.x + .25F), 0}
                               : Vec2{0, radius.y + .25F}));
            keyboard.set(towards, true);
            tick(65);
            keyboard.set(towards, false);
            tick(3);
            const Vec2 position = actor()->worldPosition() + actor()->get<Collider>()->offset;
            // Compound pixel edges can settle a few native pixels into contact
            // under continuous controller input. They must still stop traversal.
            constexpr float contactTolerance = 3.0F / 35;
            const bool outside =
                fromSide ? (rightSide ? position.x >= center.x + radius.x - contactTolerance
                                      : position.x <= center.x - radius.x + contactTolerance)
                         : position.y >= center.y + radius.y - contactTolerance;
            if (!outside)
                std::fprintf(stderr,
                             "Furniture contact failed: %s, %s, at %.2f %.2f, center %.2f %.2f, "
                             "radius %.2f %.2f\n",
                             name, fromSide ? (rightSide ? "right" : "left") : "front", position.x,
                             position.y, center.x, center.y, radius.x, radius.y);
            CHECK(outside);
        }
    }
    // The arena entrance needs a real route around the fence's east edge.
    // The old oversized enclosure left a gap narrower than the player's feet.
    const auto *fence = object("Yard Exercise Fence");
    const auto *fenceArt = fence->get<SpriteRenderer>();
    const Vec2 entrance =
        fence->worldPosition() + Vec2{fenceArt->size.x / 2, -fenceArt->size.y / 2};
    teleport(entrance + Vec2{.5F, -2.0F});
    walk(entrance + Vec2{.5F, 0});
    walk(entrance + Vec2{-1.0F, 0});
    walk(entrance + Vec2{.5F, 0});
    teleport(at(172, 196));
    const Vec2 idleOffset = actor()->get<SpriteRenderer>()->offset;
    bool breathed = false;
    for (int i = 0; i < 5; ++i) {
        tick(15);
        breathed = breathed || distance(idleOffset, actor()->get<SpriteRenderer>()->offset) > .003F;
    }
    CHECK(breathed);
    // Every resident has a real room routine and independently phased breathing/stepping.
    for (const char *name : {"Mara", "Cook Bram", "Ada", "Tomas", "Nurse Iona", "Rowan"}) {
        auto *resident = object(name);
        CHECK(resident->has<CharacterMotion>() && resident->has<NpcPath>());
        CHECK(!resident->get<NpcPath>()->waypoints.empty());
        CHECK(!resident->get<Dialogue>()->portrait.path.empty());
        CHECK(resident->get<RigidBody>()->type == RigidBodyType::Kinematic);
        CHECK(resident->get<Collider>()->layer == "Solid");
        const Vec2 start = resident->worldPosition();
        bool moved = false, animatedWalk = false;
        for (int i = 0; i < 80; ++i) {
            const auto state = resident->get<AnimatedSprite>()->state();
            const int frame = resident->get<SpriteRenderer>()->frame;
            tick(5);
            moved = moved || distance(start, resident->worldPosition()) > .1F;
            animatedWalk = animatedWalk || (state.starts_with("walk_") &&
                                            state == resident->get<AnimatedSprite>()->state() &&
                                            frame != resident->get<SpriteRenderer>()->frame);
        }
        CHECK(moved && animatedWalk);
    }
    // Walking into a waiting resident cannot move it away from its authored routine.
    auto *mara = object("Mara");
    mara->get<NpcPath>()->enabled = false;
    session->runtime().teleport(*mara, at(313, 488));
    const Vec2 anchored = mara->worldPosition();
    teleport(anchored + Vec2{0, .8F});
    keyboard.set(Key::W, true);
    tick(90);
    keyboard.set(Key::W, false);
    tick(3);
    CHECK(distance(mara->worldPosition(), anchored) < .001F);
    CHECK(actor()->worldPosition().y >=
          anchored.y + (mara->get<Collider>()->size.y + actor()->get<Collider>()->size.y) * .5F -
              .04F);
    mara->get<NpcPath>()->enabled = true;
    teleport(at(620, 675));
    CHECK(board.text("current_room") == "WORKSHOP");
    teleport(at(960, 780));
    CHECK(board.text("current_room") == "INFIRMARY");
    auto *camera = object("Player follow camera")->get<Camera>();
    CHECK(camera->mode == CameraMode::Follow && camera->targets.size() == 1);
    CHECK(camera->orthographicHeight >= 9 && camera->orthographicHeight <= 9.5F);
    CHECK(camera->viewportInset == Vec2{0, 140});
    // Camera follows actual movement, then clamps against the rendered world aspect.
    teleport(at(700, 475));
    tick(90);
    CHECK(distance(camera->view().position, actor()->worldPosition()) < .02F);
    keyboard.set(Key::D, true);
    tick(50);
    keyboard.set(Key::D, false);
    tick(50);
    CHECK(distance(camera->view().position, actor()->worldPosition()) < .03F);
    for (Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}, Vec2{1024, 768}}) {
        session->runtime().setViewportSize(size);
        teleport(at(1519, 805));
        tick(90);
        const float scale = std::min(size.x / 1280, size.y / 720);
        const float halfWidth = camera->orthographicHeight * size.x / (size.y - 140 * scale) / 2;
        CHECK(std::abs(camera->view().position.x - (camera->boundsMax.x - halfWidth)) < .03F);
        CHECK(std::abs(camera->view().position.y -
                       (camera->boundsMax.y - camera->orthographicHeight / 2)) < .03F);
    }
    session->runtime().setViewportSize({1280, 720});
    auto *guard = object("Control guard");
    guard->get<NpcPath>()->enabled = false;
    guard->get<PatrolWatch>()->enabled = false;
    // Routines and resident contact were exercised above. Isolate the static
    // passage and quest walkthrough from residents crossing its straight segments.
    for (const char *name : {"Mara", "Cook Bram", "Ada", "Tomas", "Nurse Iona", "Rowan"})
        object(name)->get<Collider>()->enabled = false;
    // Ordinary rooms have open passages: no door entity, interaction or wait to pass.
    struct Passage {
        const char *removedDoor;
        Vec2 center;
        bool side = false;
    };
    for (const auto &[name, center, side] :
         {Passage{"North cell door", at(148, 260)}, Passage{"North east cell door", at(321, 260)},
          Passage{"South cell door", at(144, 655)}, Passage{"South east cell door", at(320, 655)},
          Passage{"Canteen door", at(614, 393)}, Passage{"Laundry door", at(961, 393)},
          Passage{"Control door", at(1330, 399)}, Passage{"Stores door", at(1523, 399)},
          Passage{"Workshop door", at(614, 571)}, Passage{"Infirmary door", at(961, 571)},
          Passage{"Utility door", at(1523, 643)}, Passage{"Exit anteroom door", at(1523, 732)},
          Passage{"Common corridor door", at(410, 467), true},
          Passage{"Middle corridor door", at(808, 467), true},
          Passage{"Yard corridor door", at(1146.5F, 467), true},
          Passage{"Utility corridor door", at(1430, 467), true},
          Passage{"North cell connecting door", at(246.5F, 207), true},
          Passage{"South cell connecting door", at(238.5F, 784), true}}) {
        CHECK(!object(name));
        const Vec2 across = side ? Vec2{1.3F, 0} : Vec2{0, 1.5F};
        teleport(center - across);
        walk(center + across);
        walk(center - across);
    }
    // Releasing movement stops translation, keeps facing and uses separate idle cells.
    teleport(at(700, 475));
    keyboard.set(Key::D, true);
    tick(15);
    keyboard.set(Key::D, false);
    tick(3);
    const Vec2 stopped = actor()->worldPosition();
    tick(20);
    CHECK(distance(stopped, actor()->worldPosition()) < .005F);
    CHECK(actor()->get<AnimatedSprite>()->state() == "idle_right");
    CHECK(actor()->get<SpriteRenderer>()->frame % 10 >= 8);
    // Animation follows resolved movement when holding a key against solid stone.
    teleport(at(500, 443));
    keyboard.set(Key::W, true);
    tick(90);
    CHECK(actor()->get<AnimatedSprite>()->state() == "idle_up");
    keyboard.set(Key::W, false);
    tick(3);
    // Physically walk from the starting cell through the common-room and workshop openings.
    teleport(at(172, 196));
    walk(at(148, 202));
    walk(at(148, 320));
    walk(at(275, 320));
    walk(at(275, 475));
    near("Mara");
    press(Key::E);
    auto *dialogue = object("Mara")->get<Dialogue>();
    CHECK(dialogue->active());
    CHECK(dialogue->currentPage().portrait.path == "assets/level2/portrait_companion.png");
    const Vec2 talkingAt = object("Mara")->worldPosition();
    const Vec2 cookAt = object("Cook Bram")->worldPosition();
    bool backgroundMoved = false;
    for (int i = 0; i < 4; ++i) {
        tick(80);
        backgroundMoved =
            backgroundMoved || distance(cookAt, object("Cook Bram")->worldPosition()) > .1F;
    }
    CHECK(distance(talkingAt, object("Mara")->worldPosition()) < .02F && backgroundMoved);
    press(Key::Escape);
    CHECK(!dialogue->active() && !session->runtime().inputLocked());
    press(Key::E);
    CHECK(dialogue->active());
    for (int i = 0; i < 6 && dialogue->active(); ++i)
        press(Key::E);
    CHECK(board.number("l2_met_mara") == 1);
    teleport(at(313, 475));
    walk(at(560, 475));
    walk(at(614, 475));
    walk(at(614, 650));
    walk(at(560, 718));
    walk(at(482, 718));
    press(Key::E);
    tick(30);
    CHECK(board.number("search_ui_busy") == 1);
    CHECK(actor()->get<AnimatedSprite>()->state() == "search_up");
    press(Key::Escape);
    CHECK(inventory()->count(ItemId("l2_screwdriver")) == 0);
    CHECK(board.number("l2_tool_found") == 0);
    search("Tool bench", false);
    CHECK(inventory()->count(ItemId("l2_screwdriver")) == 1);
    CHECK(board.text("search_ui_icon") == "assets/level2/screwdriver.png");
    near("Tool bench");
    press(Key::E);
    tick(190);
    press(Key::E);
    CHECK(inventory()->count(ItemId("l2_screwdriver")) == 1);
    walk(at(550, 718));
    walk(object("Tool chest")->worldPosition() + Vec2{.7F, .7F});
    search("Tool chest", false);
    CHECK(inventory()->count(ItemId("l2_cutters")) == 1);
    // Workshop -> laundry and stores: all room entrances must really pass the feet collider.
    walk(at(614, 650));
    walk(at(614, 475));
    walk(at(961, 475));
    walk(at(961, 328));
    walk({at(961, 328).x, object("Linen table")->worldPosition().y + .7F});
    walk(object("Linen table")->worldPosition() + Vec2{.8F, .7F});
    search("Linen table", false);
    CHECK(inventory()->count(ItemId("l2_linen")) == 1);
    walk(at(961, 328));
    walk(at(961, 475));
    walk(at(1330, 475));
    walk(at(1330, 495));
    walk(at(1469, 495));
    walk(at(1469, 475));
    walk(at(1523, 475));
    walk(at(1523, 335));
    walk(object("Fuse cabinet")->worldPosition() + Vec2{.95F, .45F});
    search("Fuse cabinet", false);
    CHECK(inventory()->count(ItemId("l2_fuse")) == 1);
    near("Power terminal");
    press(Key::E);
    CHECK(object("Power terminal")->get<WirePuzzle>()->active());
    CHECK(!object("Power terminal")->get<WirePuzzle>()->ready());
    press(Key::Escape);
    search("Repair workbench");
    CHECK(inventory()->count(ItemId("l2_insulated_fuse")) == 1);
    CHECK(board.text("search_ui_icon") == "assets/level2/safe_fuse.png");
    CHECK(inventory()->count(ItemId("l2_fuse")) == 0 &&
          inventory()->count(ItemId("l2_linen")) == 0);
    // Captured searches cancel after teleport; no remote reward, hidden tools remain.
    guard->get<PatrolWatch>()->enabled = true;
    session->runtime().teleport(*guard, at(1390, 240));
    object("Security desk")->get<SearchAction>()->duration = 8;
    near("Security desk");
    press(Key::E);
    tick(225);
    CHECK(distance(actor()->worldPosition(), at(172, 196)) < .1F);
    CHECK(!session->runtime().inputLocked());
    CHECK(inventory()->count(ItemId("l2_keycard")) == 0);
    CHECK(inventory()->count(ItemId("l2_insulated_fuse")) == 1);
    guard->get<PatrolWatch>()->enabled = false;
    object("Security desk")->get<SearchAction>()->duration = 2.2F;
    search("Security desk");
    CHECK(inventory()->count(ItemId("l2_keycard")) == 1);
    near("Power terminal");
    press(Key::E);
    auto *puzzle = object("Power terminal")->get<WirePuzzle>();
    CHECK(puzzle->active() && puzzle->ready());
    press(Key::E); // Incorrect pair must not grant power.
    CHECK(board.number("l2_power_restored") == 0 && puzzle->connected() == 0);
    for (int source = 0; source < 4; ++source) {
        for (int i = 0; puzzle->target() != WirePuzzle::targetFor(puzzle->source()) && i < 4; ++i)
            press(Key::D);
        press(Key::E);
    }
    CHECK(!puzzle->active() && board.number("l2_power_restored") == 1);
    CHECK(!session->runtime().inputLocked());
    // Real walk through utility into the exit anteroom; gate stays solid until chain work ends.
    walk(at(1520, 705));
    walk(at(1520, 795));
    keyboard.set(Key::S, true);
    tick(45);
    keyboard.set(Key::S, false);
    tick(4);
    CHECK(actor()->worldPosition().y < at(1519, 845).y);
    // The barrier seals the two-tile perimeter opening, including both frame edges.
    for (float x : {1492.0F, 1519.0F, 1546.0F}) {
        teleport(at(x, 802));
        keyboard.set(Key::S, true);
        tick(45);
        keyboard.set(Key::S, false);
        tick(4);
        CHECK(actor()->worldPosition().y < at(1519, 845).y);
        CHECK(board.text("level_state") == "playing");
    }
    near("Green exit gate", {0, -1.1F});
    search("Green exit gate", false);
    tick(30);
    CHECK(board.number("l2_exit_open") == 1);
    CHECK(!object("Green exit gate")->get<Collider>()->enabled);
    auto *exitGate = object("Green exit gate")->get<StateGate>();
    auto *exitSprite = object("Green exit gate")->get<SpriteRenderer>();
    const Vec2 openedOffset = exitSprite->offset;
    CHECK(exitSprite->visible && exitSprite->color.a == 255);
    CHECK(exitSprite->frame == 7);
    press(Key::E);
    tick(30);
    CHECK(!exitGate->open() && object("Green exit gate")->get<Collider>()->enabled);
    CHECK(exitSprite->frame == 0);
    CHECK(board.number("l2_exit_open") == 1 && board.number("l2_gate_open") == 0);
    CHECK(board.number("search_ui_active") == 0); // Cutting the chain is not repeated.
    press(Key::E);
    tick(30);
    CHECK(exitGate->open() && !object("Green exit gate")->get<Collider>()->enabled);
    CHECK(distance(exitSprite->offset, openedOffset) < .01F);
    walk(at(1519, 884));
    tick(3);
    CHECK(board.text("level_state") == "complete");
    CHECK(board.text("inventory_completion_title") == "THE INNER KEEP IS BEHIND YOU");
    press(Key::R);
    tick(3);
    CHECK(inventory()->freeSlots() == 6);
    CHECK(session->runtime().blackboard().number("l2_power_restored") == 0);
    CHECK(!object("Green exit gate")->get<StateGate>()->open());
    CHECK(!session->runtime().inputLocked());
    return test::finish("castle_level2");
}
