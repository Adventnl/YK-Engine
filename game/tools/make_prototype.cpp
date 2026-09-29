// Writes the Elemental Prototype sample project (project file, prefabs, scenes) to a directory.
//
//   yk_make_prototype <project-directory>
//
// This is a bootstrap tool: it recreates the initial content. Once designers edit the scenes in the
// editor, the checked-in files are the source of truth and this tool should not be re-run over them.
#include "PrototypeGame.hpp"
#include "yk/assets/Project.hpp"
#include "yk/core/Log.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <cstdio>

using namespace yk;
using namespace yk::prototype;

namespace {
constexpr Color groundColor{86, 96, 116, 255};
constexpr Color wallColor{52, 58, 76, 255};

LayerConfig projectLayers() {
    LayerConfig layers = LayerConfig::defaults();
    for (const char *name : {layers::solid, layers::player, layers::sensor, layers::prop})
        layers.addLayer(name);
    const auto index = [&](const char *name) { return static_cast<std::size_t>(layers.indexOf(name)); };
    layers.setInteraction(index(layers::player), index(layers::solid), true);
    // Characters pass through each other (no Player <-> Player), which keeps two-player puzzles from
    // jamming. Enable it in the project's layer matrix to let them block or stand on each other.
    layers.setInteraction(index(layers::player), index(layers::sensor), true);
    layers.setInteraction(index(layers::prop), index(layers::solid), true);
    layers.setInteraction(index(layers::prop), index(layers::player), true);
    layers.setInteraction(index(layers::prop), index(layers::sensor), true);
    layers.setInteraction(index(layers::prop), index(layers::prop), true);
    return layers;
}

struct Builder {
    ComponentRegistry &registry;
    Scene &scene;

    Entity &group(const char *name) {
        return scene.createEntity(name);
    }
    Entity &solid(const char *name, Vec2 center, Vec2 size, Color color, EntityId parent) {
        Entity &entity = scene.createEntity(name, parent);
        entity.transform().position = center;
        auto &sprite = entity.add<SpriteRenderer>();
        sprite.size = size;
        sprite.color = color;
        auto &collider = entity.add<Collider>();
        collider.size = size;
        collider.layer = layers::solid;
        return entity;
    }
    Entity &adopt(EntityId id, EntityId parent) {
        scene.setParent(id, parent);
        return *scene.find(id);
    }
    void resize(Entity &entity, Vec2 size) {
        entity.get<SpriteRenderer>()->size = size;
        entity.get<Collider>()->size = size;
    }
    Entity &text(const char *name, const char *content, UiAnchor anchor, Vec2 offset, float scale, Color color, EntityId parent) {
        Entity &entity = scene.createEntity(name, parent);
        auto &label = entity.add<UiText>();
        label.text = content;
        label.anchor = anchor;
        label.offset = offset;
        label.scale = scale;
        label.color = color;
        return entity;
    }
};

// Saves reusable entities as prefab assets, mirroring the editor's "Save as Prefab".
Status writePrefabs(const Project &project, ComponentRegistry &registry) {
    struct Item {
        const char *file;
        EntityId (*build)(Scene &, Vec2);
    };
    const Item items[] = {
        {"fire_character", createFireCharacter}, {"water_character", createWaterCharacter},
        {"fire_exit", createFireExit},           {"water_exit", createWaterExit},
        {"fire_gem", createFireGem},             {"water_gem", createWaterGem},
        {"lava_pool", [](Scene &s, Vec2 at) { return createLavaPool(s, at, {3.0F, 0.6F}); }},
        {"water_pool", [](Scene &s, Vec2 at) { return createWaterPool(s, at, {3.0F, 0.6F}); }},
        {"goo_pool", [](Scene &s, Vec2 at) { return createGooPool(s, at, {3.0F, 0.6F}); }},
    };
    for (const Item &item : items) {
        Scene scratch(registry, 1);
        const EntityId root = item.build(scratch, {0.0F, 0.0F});
        auto path = project.resolve(std::string("prefabs/") + item.file + prefabExtension);
        if (!path)
            return Error{path.error()};
        if (auto status = savePrefab(scratch, root, path.value()); !status)
            return status;
    }
    return success();
}

// Instantiates a saved prefab, so the level really is built from the project's reusable entities.
EntityId instance(const Project &project, Scene &scene, const char *file, Vec2 at, EntityId parent) {
    auto document = loadPrefabDocument(project.resolve(std::string("prefabs/") + file + prefabExtension).value());
    if (!document) {
        std::fprintf(stderr, "%s\n", document.error().c_str());
        std::exit(1);
    }
    auto id = instantiateSubtree(scene, document.value(), parent, at);
    if (!id) {
        std::fprintf(stderr, "%s\n", id.error().c_str());
        std::exit(1);
    }
    return id.value();
}

// The main level. 40 x 22.5 meters; +Y is down; the floor's top surface is y = 21.
std::unique_ptr<Scene> buildTestLevel(const Project &project, ComponentRegistry &registry) {
    auto scene = std::make_unique<Scene>(registry, 20240929);
    scene->settings.name = "Test Level";
    scene->settings.background = {24, 28, 42, 255};
    Builder b{registry, *scene};

    Entity &camera = b.group("Camera");
    camera.transform().position = {20.0F, 11.25F};
    auto &view = camera.add<Camera>();
    view.orthographicHeight = 22.5F;

    Entity &level = b.group("Level");
    b.solid("Ground A", {7.0F, 22.0F}, {14.0F, 2.0F}, groundColor, level.id());
    b.solid("Lava Basin", {16.0F, 22.5F}, {4.0F, 1.0F}, wallColor, level.id());
    b.solid("Ground B1", {20.75F, 22.0F}, {5.5F, 2.0F}, groundColor, level.id());
    b.solid("Shaft Bottom", {25.0F, 23.5F}, {3.0F, 1.0F}, wallColor, level.id());
    b.solid("Ground B2", {28.25F, 22.0F}, {3.5F, 2.0F}, groundColor, level.id());
    b.solid("Water Basin", {32.0F, 22.5F}, {4.0F, 1.0F}, wallColor, level.id());
    b.solid("Ground C", {37.0F, 22.0F}, {6.0F, 2.0F}, groundColor, level.id());
    b.solid("Ledge", {33.25F, 10.5F}, {13.5F, 1.0F}, {96, 106, 130, 255}, level.id()); // Starts flush with the elevator (x = 26.5).
    b.solid("Left Wall", {-0.5F, 11.25F}, {1.0F, 24.0F}, wallColor, level.id());
    b.solid("Right Wall", {40.5F, 11.25F}, {1.0F, 24.0F}, wallColor, level.id());
    b.solid("Ceiling", {20.0F, -0.5F}, {42.0F, 1.0F}, wallColor, level.id());

    Entity &hazards = b.group("Hazards");
    b.resize(b.adopt(instance(project, *scene, "lava_pool", {16.0F, 21.5F}, {}), hazards.id()), {4.0F, 1.0F});
    b.resize(b.adopt(instance(project, *scene, "water_pool", {32.0F, 21.5F}, {}), hazards.id()), {4.0F, 1.0F});
    b.resize(b.adopt(instance(project, *scene, "goo_pool", {31.0F, 9.6F}, {}), hazards.id()), {1.4F, 0.8F});

    Entity &pickups = b.group("Pickups");
    instance(project, *scene, "fire_gem", {4.5F, 20.4F}, pickups.id());
    instance(project, *scene, "water_gem", {8.5F, 20.4F}, pickups.id());
    instance(project, *scene, "fire_gem", {27.6F, 9.4F}, pickups.id());
    instance(project, *scene, "water_gem", {29.05F, 9.4F}, pickups.id());
    instance(project, *scene, "water_gem", {32.0F, 21.55F}, pickups.id()); // Bonus: only water can fetch it.

    Entity &exits = b.group("Exits");
    const EntityId fireExit = instance(project, *scene, "fire_exit", {34.5F, 9.1F}, exits.id());
    const EntityId waterExit = instance(project, *scene, "water_exit", {37.5F, 9.1F}, exits.id());

    Entity &mechanisms = b.group("Mechanisms");
    // Lever (water character) -> Door blocking the way to the lava pit.
    Entity &leverDoor = b.solid("Lever Door", {11.5F, 19.0F}, {0.6F, 4.0F}, {200, 160, 60, 255}, mechanisms.id());
    leverDoor.add<RigidBody>().type = RigidBodyType::Kinematic;
    auto &leverDoorParts = leverDoor.add<Door>();
    leverDoorParts.openOffset = {0.0F, -4.4F};
    leverDoorParts.speed = 6.0F;
    leverDoorParts.openSound.path = "tone:300,0.2,saw";
    Entity &lever = scene->createEntity("Lever", mechanisms.id());
    lever.transform().position = {6.5F, 20.6F};
    auto &leverSprite = lever.add<SpriteRenderer>();
    leverSprite.size = {0.5F, 0.8F};
    leverSprite.color = {210, 80, 70, 255};
    leverSprite.layer = 1;
    auto &leverCollider = lever.add<Collider>();
    leverCollider.size = {0.5F, 0.8F};
    leverCollider.layer = layers::sensor;
    auto &leverParts = lever.add<Lever>();
    leverParts.activatorTags = {waterTag};
    leverParts.targets = {leverDoor.id()};
    leverParts.sound.path = "tone:520,0.08,square";
    // Two plates raise the elevator while either is held. One character boards the elevator while the
    // other crosses it to the ground plate beyond; once up, the rider holds the ledge plate so the
    // partner can be lifted in turn.
    Entity &elevator = b.solid("Elevator", {25.0F, 21.2F}, {3.0F, 0.4F}, {120, 150, 190, 255}, mechanisms.id());
    elevator.add<RigidBody>().type = RigidBodyType::Kinematic;
    auto &elevatorParts = elevator.add<Door>();
    elevatorParts.openOffset = {0.0F, -11.0F};
    elevatorParts.speed = 4.0F;
    const auto plate = [&](const char *name, Vec2 at) -> Entity & {
        Entity &entity = scene->createEntity(name, mechanisms.id());
        entity.transform().position = at;
        auto &sprite = entity.add<SpriteRenderer>();
        sprite.size = {1.4F, 0.2F};
        sprite.color = {200, 70, 60, 255};
        sprite.layer = 1;
        auto &collider = entity.add<Collider>();
        collider.size = {1.4F, 0.5F};
        collider.offset = {0.0F, -0.15F};
        collider.layer = layers::sensor;
        auto &parts = entity.add<PressurePlate>();
        parts.targets = {elevator.id()};
        parts.pressSound.path = "tone:440,0.06,square";
        return entity;
    };
    plate("Ground Plate", {28.5F, 20.9F});
    plate("Ledge Plate", {28.5F, 9.9F});
    Entity &checkpoint = scene->createEntity("Checkpoint", mechanisms.id());
    checkpoint.transform().position = {19.0F, 20.4F};
    auto &checkpointSprite = checkpoint.add<SpriteRenderer>();
    checkpointSprite.size = {0.4F, 1.2F};
    checkpointSprite.color = {130, 130, 150, 255};
    checkpointSprite.layer = 1;
    auto &checkpointCollider = checkpoint.add<Collider>();
    checkpointCollider.size = {0.6F, 1.2F};
    checkpointCollider.layer = layers::sensor;
    checkpoint.add<Checkpoint>().sound.path = "tone:784,0.12,sine";

    Entity &characters = b.group("Characters");
    const EntityId fire = b.adopt(instance(project, *scene, "fire_character", {2.0F, 20.5F}, {}), characters.id()).id();
    const EntityId water = b.adopt(instance(project, *scene, "water_character", {3.6F, 20.5F}, {}), characters.id()).id();
    for (const EntityId id : {fire, water}) {
        auto *killable = scene->find(id)->get<Killable>();
        killable->deathSound.path = "tone:110,0.35,saw";
        killable->respawnSound.path = "tone:523,0.15,sine";
        scene->find(id)->get<PlatformerController>()->jumpSound.path = "tone:330,0.08,square";
    }
    Entity &spawns = b.group("Spawn Points");
    for (const auto &[name, at, who] : {std::tuple{"Fire Spawn", Vec2{2.0F, 20.5F}, fire}, std::tuple{"Water Spawn", Vec2{3.6F, 20.5F}, water}}) {
        Entity &spawn = scene->createEntity(name, spawns.id());
        spawn.transform().position = at;
        auto &marker = spawn.add<SpriteRenderer>();
        marker.size = {0.3F, 0.3F};
        marker.shape = SpriteShape::Ellipse;
        marker.color = {255, 255, 255, 90};
        marker.layer = 3;
        spawn.add<SpawnPoint>().character = who;
    }

    Entity &ui = b.group("HUD");
    Entity &bar = scene->createEntity("Top Bar", ui.id());
    auto &barPanel = bar.add<UiPanel>();
    barPanel.anchor = UiAnchor::Top;
    barPanel.size = {1280.0F, 44.0F};
    barPanel.offset = {0.0F, 0.0F};
    barPanel.color = {0, 0, 0, 120};
    b.text("Fire Gems", "FIRE GEMS {fire_gems:0}/2", UiAnchor::TopLeft, {16.0F, 12.0F}, 3.0F, {255, 150, 70, 255}, ui.id());
    b.text("Water Gems", "WATER GEMS {water_gems:0}/3", UiAnchor::TopRight, {16.0F, 12.0F}, 3.0F, {120, 190, 255, 255}, ui.id());
    b.text("Time", "TIME {level_time:0}", UiAnchor::Top, {0.0F, 12.0F}, 3.0F, {235, 235, 245, 255}, ui.id());
    b.text("Message", "{level_message}", UiAnchor::Center, {0.0F, -60.0F}, 6.0F, {255, 240, 150, 255}, ui.id());
    b.text("Controls", "P1: WASD   P2: ARROWS   R: RESTART   F1: DEBUG", UiAnchor::BottomLeft, {16.0F, 10.0F}, 2.0F,
           {190, 195, 215, 255}, ui.id());

    Entity &flow = scene->createEntity("Level Flow");
    auto &rules = flow.add<LevelFlow>();
    rules.goals = {fireExit, waterExit};
    rules.completeSound.path = "tone:880,0.4,sine";
    return scene;
}

// A physics and mechanics sandbox for manual testing in the editor.
std::unique_ptr<Scene> buildPlayground(const Project &project, ComponentRegistry &registry) {
    auto scene = std::make_unique<Scene>(registry, 20240930);
    scene->settings.name = "Playground";
    scene->settings.background = {30, 34, 48, 255};
    Builder b{registry, *scene};
    Entity &camera = b.group("Camera");
    camera.transform().position = {20.0F, 11.25F};
    camera.add<Camera>().orthographicHeight = 22.5F;

    Entity &level = b.group("Level");
    b.solid("Ground", {20.0F, 22.0F}, {40.0F, 2.0F}, groundColor, level.id());
    b.solid("Left Wall", {-0.5F, 11.25F}, {1.0F, 24.0F}, wallColor, level.id());
    b.solid("Right Wall", {40.5F, 11.25F}, {1.0F, 24.0F}, wallColor, level.id());
    b.solid("Ceiling", {20.0F, -0.5F}, {42.0F, 1.0F}, wallColor, level.id());
    // Slopes: gentle and steep ramps that meet the floor.
    Entity &gentle = b.solid("Gentle Ramp", {7.5F, 19.6F}, {9.0F, 0.5F}, groundColor, level.id());
    gentle.transform().rotationDegrees = -12;
    Entity &steep = b.solid("Steep Ramp", {17.0F, 19.0F}, {6.0F, 0.5F}, groundColor, level.id());
    steep.transform().rotationDegrees = -32;
    b.solid("Step", {24.0F, 20.0F}, {2.0F, 2.0F}, groundColor, level.id());
    b.solid("High Ledge", {36.0F, 14.0F}, {8.0F, 0.6F}, groundColor, level.id());

    Entity &props = b.group("Props");
    for (int i = 0; i < 3; ++i) {
        Entity &crate = b.solid("Crate", {28.0F + 0.05F * static_cast<float>(i), 20.0F - 1.05F * static_cast<float>(i)}, {1.0F, 1.0F},
                                {176, 124, 72, 255}, props.id());
        crate.get<Collider>()->layer = layers::prop;
        crate.get<Collider>()->friction = 0.5F;
        crate.get<SpriteRenderer>()->layer = 1;
        crate.add<RigidBody>().allowSleep = false;
    }
    Entity &ball = b.solid("Ball", {31.0F, 8.0F}, {0.9F, 0.9F}, {230, 200, 90, 255}, props.id());
    ball.get<Collider>()->shape = ColliderShape::Circle;
    ball.get<Collider>()->layer = layers::prop;
    ball.get<Collider>()->restitution = 0.6F;
    ball.get<SpriteRenderer>()->shape = SpriteShape::Ellipse;
    ball.add<RigidBody>();

    Entity &mech = b.group("Mechanisms");
    Entity &shuttle = b.solid("Shuttle", {12.0F, 13.0F}, {3.0F, 0.4F}, {120, 150, 190, 255}, mech.id());
    shuttle.add<RigidBody>().type = RigidBodyType::Kinematic;
    auto &move = shuttle.add<MovingPlatform>();
    move.travel = {8.0F, 0.0F};
    move.speed = 2.5F;
    // A plate that a crate (or a character) can hold down, opening a door to the high ledge.
    Entity &door = b.solid("Ledge Door", {32.0F, 12.0F}, {0.6F, 3.0F}, {200, 160, 60, 255}, mech.id());
    door.add<RigidBody>().type = RigidBodyType::Kinematic;
    door.add<Door>().openOffset = {0.0F, -3.2F};
    Entity &plate = scene->createEntity("Crate Plate", mech.id());
    plate.transform().position = {22.0F, 20.9F};
    auto &plateSprite = plate.add<SpriteRenderer>();
    plateSprite.size = {1.4F, 0.2F};
    plateSprite.color = {200, 70, 60, 255};
    plateSprite.layer = 1;
    auto &plateCollider = plate.add<Collider>();
    plateCollider.size = {1.4F, 0.5F};
    plateCollider.offset = {0.0F, -0.15F};
    plateCollider.layer = layers::sensor;
    plate.add<PressurePlate>().targets = {door.id()};

    Entity &characters = b.group("Characters");
    b.adopt(instance(project, *scene, "fire_character", {2.0F, 20.5F}, {}), characters.id());
    b.adopt(instance(project, *scene, "water_character", {4.0F, 20.5F}, {}), characters.id());
    Entity &ui = b.group("HUD");
    b.text("Help", "PLAYGROUND  P1: WASD  P2: ARROWS  R: RESTART", UiAnchor::TopLeft, {16.0F, 12.0F}, 2.0F, {235, 235, 245, 255}, ui.id());
    Entity &flow = scene->createEntity("Level Flow");
    flow.add<LevelFlow>();
    return scene;
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: yk_make_prototype <project-directory>\n");
        return 2;
    }
    ComponentRegistry registry;
    registerEngineComponents(registry);
    registerGameplayComponents(registry);
    registerPrototypeGame(registry);

    Project project = Project::create(argv[1], "Elemental Prototype");
    project.layers = projectLayers();
    project.startScene = "scenes/test_level.ykscene";
    project.window = {"Elemental Prototype", 1280, 720};
    const auto check = [](const Status &status) {
        if (!status) {
            std::fprintf(stderr, "error: %s\n", status.error().c_str());
            std::exit(1);
        }
    };
    check(project.save());
    check(writePrefabs(project, registry));
    for (const auto &[file, build] : {std::pair<const char *, std::unique_ptr<Scene> (*)(const Project &, ComponentRegistry &)>{"test_level", buildTestLevel},
                                      {"playground", buildPlayground}}) {
        const auto scene = build(project, registry);
        check(saveScene(*scene, project.resolve(std::string("scenes/") + file + sceneExtension).value()));
        std::printf("wrote scenes/%s%s (%zu entities)\n", file, sceneExtension, scene->size());
    }
    return 0;
}
