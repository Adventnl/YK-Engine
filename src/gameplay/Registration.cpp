#include "yk/gameplay/Gameplay.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <filesystem>

namespace yk {
LayerConfig layers::standard() {
    LayerConfig config = LayerConfig::defaults();
    for (const char *name : {solid, player, sensor, prop})
        config.addLayer(name);
    const auto index = [&](const char *name) {
        return static_cast<std::size_t>(config.indexOf(name));
    };
    config.setInteraction(index(player), index(solid), true);
    config.setInteraction(index(player), index(sensor), true);
    config.setInteraction(index(prop), index(solid), true);
    config.setInteraction(index(prop), index(player), true);
    config.setInteraction(index(prop), index(sensor), true);
    config.setInteraction(index(prop), index(prop), true);
    return config;
}

Result<Project> createProject(const std::filesystem::path &directory, const std::string &name,
                              const ComponentRegistry &registry) {
    if (name.empty())
        return Error{"A project needs a name"};
    Project project = Project::create(directory, name);
    if (std::filesystem::exists(project.file()))
        return Error{"'" + project.root.string() + "' already contains a project"};
    std::error_code error;
    for (const char *folder : {"scenes", "prefabs", "assets"}) {
        std::filesystem::create_directories(project.root / folder, error);
        if (error)
            return Error{"Cannot create '" + (project.root / folder).string() +
                         "': " + error.message()};
    }
    project.layers = layers::standard();
    project.startScene = "scenes/main.ykscene";
    Scene scene(registry);
    scene.settings.name = "Main";
    scene.createEntity("Main Camera").addComponent("Camera");
    if (auto status = saveScene(scene, project.root / project.startScene); !status)
        return Error{status.error()};
    if (auto status = project.save(); !status)
        return Error{status.error()};
    return project;
}

namespace {
constexpr Color groundColor{86, 96, 116, 255};

Entity &place(Scene &scene, const char *name, Vec2 at) {
    Entity &entity = scene.createEntity(name);
    entity.setWorldPosition(at);
    return entity;
}
// A colored placeholder rectangle with a matching collider on `layer`.
Entity &block(Scene &scene, const char *name, Vec2 at, Vec2 size, Color color, const char *layer,
              bool trigger = false, int drawLayer = 0) {
    Entity &entity = place(scene, name, at);
    auto &sprite = entity.add<SpriteRenderer>();
    sprite.size = size;
    sprite.color = color;
    sprite.layer = drawLayer;
    auto &collider = entity.add<Collider>();
    collider.size = size;
    collider.layer = layer;
    collider.isTrigger = trigger;
    return entity;
}
} // namespace

void registerGameplayComponents(ComponentRegistry &registry) {
    registry.add<Killable>("Killable");
    registry.add<PlatformerController>("PlatformerController");
    registry.add<PressurePlate>("PressurePlate");
    registry.add<Lever>("Lever");
    registry.add<Door>("Door");
    registry.add<MovingPlatform>("MovingPlatform");
    registry.add<Hazard>("Hazard");
    registry.add<Collectible>("Collectible");
    registry.add<Checkpoint>("Checkpoint");
    registry.add<SpawnPoint>("SpawnPoint");
    registry.add<Goal>("Goal");
    registry.add<TriggerZone>("TriggerZone");
    registry.add<LevelFlow>("LevelFlow");

    registry.addTemplate(
        {"Platform", "Level", [](Scene &scene, Vec2 at) {
             return block(scene, "Platform", at, {4.0F, 0.5F}, groundColor, layers::solid).id();
         }});
    registry.addTemplate({"Crate", "Level", [](Scene &scene, Vec2 at) {
                              Entity &crate = block(scene, "Crate", at, {1.0F, 1.0F},
                                                    {170, 120, 70, 255}, layers::prop, false, 1);
                              crate.add<RigidBody>().allowSleep = false;
                              crate.get<Collider>()->friction = 0.5F;
                              return crate.id();
                          }});
    registry.addTemplate({"Moving Platform", "Mechanisms", [](Scene &scene, Vec2 at) {
                              Entity &platform = block(scene, "Moving Platform", at, {3.0F, 0.4F},
                                                       {120, 150, 190, 255}, layers::solid);
                              platform.add<MovingPlatform>();
                              return platform.id();
                          }});
    registry.addTemplate({"Door", "Mechanisms", [](Scene &scene, Vec2 at) {
                              Entity &door = block(scene, "Door", at, {0.6F, 3.0F},
                                                   {200, 160, 60, 255}, layers::solid);
                              door.add<Door>();
                              return door.id();
                          }});
    registry.addTemplate({"Pressure Plate", "Mechanisms", [](Scene &scene, Vec2 at) {
                              // A slab whose top is 0.14 above the origin (put the origin on the
                              // floor); it sinks into the floor under a load. Adding the component
                              // gives it its kinematic body and a solid pad the size of the art.
                              Entity &plate = place(scene, "Pressure Plate", at);
                              auto &sprite = plate.add<SpriteRenderer>();
                              sprite.size = {1.4F, 0.5F};
                              sprite.offset = {0.0F, 0.11F};
                              sprite.color = {200, 70, 60, 255};
                              sprite.layer = -1; // Behind the floor, so the sunk part is hidden.
                              plate.add<PressurePlate>();
                              return plate.id();
                          }});
    registry.addTemplate({"Lever", "Mechanisms", [](Scene &scene, Vec2 at) {
                              Entity &lever = block(scene, "Lever", at, {0.5F, 0.8F},
                                                    {200, 70, 60, 255}, layers::sensor, true, 1);
                              lever.add<Lever>();
                              return lever.id();
                          }});
    registry.addTemplate({"Hazard", "Gameplay", [](Scene &scene, Vec2 at) {
                              Entity &hazard = block(scene, "Hazard", at, {3.0F, 0.6F},
                                                     {220, 60, 50, 255}, layers::sensor, true, 1);
                              hazard.add<Hazard>();
                              return hazard.id();
                          }});
    registry.addTemplate({"Collectible", "Gameplay", [](Scene &scene, Vec2 at) {
                              Entity &item = block(scene, "Collectible", at, {0.5F, 0.5F},
                                                   {255, 210, 60, 255}, layers::sensor, true, 2);
                              item.get<SpriteRenderer>()->shape = SpriteShape::Ellipse;
                              item.add<Collectible>();
                              return item.id();
                          }});
    registry.addTemplate({"Checkpoint", "Gameplay", [](Scene &scene, Vec2 at) {
                              Entity &checkpoint =
                                  block(scene, "Checkpoint", at, {0.6F, 1.2F}, {150, 150, 170, 255},
                                        layers::sensor, true);
                              checkpoint.add<Checkpoint>();
                              return checkpoint.id();
                          }});
    registry.addTemplate({"Goal", "Gameplay", [](Scene &scene, Vec2 at) {
                              Entity &goal = block(scene, "Goal", at, {1.2F, 1.8F},
                                                   {120, 120, 140, 255}, layers::sensor, true);
                              goal.add<Goal>();
                              return goal.id();
                          }});
    registry.addTemplate({"Trigger Zone", "Gameplay", [](Scene &scene, Vec2 at) {
                              Entity &zone = block(scene, "Trigger Zone", at, {3.0F, 3.0F},
                                                   {90, 160, 255, 70}, layers::sensor, true);
                              zone.add<TriggerZone>();
                              return zone.id();
                          }});
    registry.addTemplate({"Spawn Point", "Gameplay", [](Scene &scene, Vec2 at) {
                              Entity &spawn = place(scene, "Spawn Point", at);
                              auto &sprite = spawn.add<SpriteRenderer>();
                              sprite.size = {0.4F, 0.4F};
                              sprite.shape = SpriteShape::Ellipse;
                              sprite.color = {255, 255, 255, 120};
                              sprite.layer = 3;
                              spawn.add<SpawnPoint>();
                              return spawn.id();
                          }});
    registry.addTemplate({"Level Flow", "Gameplay", [](Scene &scene, Vec2 at) {
                              Entity &flow = place(scene, "Level Flow", at);
                              flow.add<LevelFlow>();
                              return flow.id();
                          }});
    registry.addTemplate({"Character", "Gameplay", [](Scene &scene, Vec2 at) {
                              Entity &character = place(scene, "Character", at);
                              auto &sprite = character.add<SpriteRenderer>();
                              sprite.size = {0.6F, 0.95F};
                              sprite.color = {235, 235, 245, 255};
                              sprite.layer = 5;
                              character.add<PlatformerController>();
                              character.add<Killable>();
                              return character.id();
                          }});
}
} // namespace yk

namespace yk {
void registerStandardComponents(ComponentRegistry &registry) {
    registerEngineComponents(registry);
    registerGameplayComponents(registry);
}
} // namespace yk
