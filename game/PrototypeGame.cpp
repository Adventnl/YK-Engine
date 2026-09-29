#include "PrototypeGame.hpp"
#include <algorithm>
#include <cmath>

namespace yk::prototype {
namespace {
constexpr Color fireColor{235, 90, 45, 255};
constexpr Color waterColor{60, 140, 240, 255};

Entity &place(Scene &scene, const char *name, Vec2 at) {
    Entity &entity = scene.createEntity(name);
    entity.setWorldPosition(at);
    return entity;
}
Entity &character(Scene &scene, const char *name, Vec2 at, const char *tag, Color color,
                  const char *actionSet) {
    Entity &entity = place(scene, name, at);
    auto &sprite = entity.add<SpriteRenderer>();
    sprite.size = {0.6F, 0.95F};
    sprite.color = color;
    sprite.layer = 5;
    entity.add<PlatformerController>();
    entity.get<PlayerInput>()->actionSet = actionSet;
    entity.add<Killable>();
    entity.addTag(tag);
    return entity;
}
Entity &zone(Scene &scene, const char *name, Vec2 at, Vec2 size, Color color, int drawLayer) {
    Entity &entity = place(scene, name, at);
    auto &sprite = entity.add<SpriteRenderer>();
    sprite.size = size;
    sprite.color = color;
    sprite.layer = drawLayer;
    auto &collider = entity.add<Collider>();
    collider.size = size;
    collider.isTrigger = true;
    collider.layer = layers::sensor;
    return entity;
}
Entity &pool(Scene &scene, const char *name, Vec2 at, Vec2 size, Color color,
             std::vector<std::string> kills) {
    Entity &entity = zone(scene, name, at, size, color, 1);
    entity.add<Hazard>().affectsTags = std::move(kills);
    return entity;
}
Entity &gem(Scene &scene, const char *name, Vec2 at, const char *collector, const char *variable,
            Color color) {
    Entity &entity = zone(scene, name, at, {0.5F, 0.5F}, color, 2);
    entity.get<SpriteRenderer>()->shape = SpriteShape::Ellipse;
    auto &item = entity.add<Collectible>();
    item.collectorTags = {collector};
    item.variable = variable;
    item.sound.path = "tone:988,0.09,sine";
    return entity;
}
Entity &exit(Scene &scene, const char *name, Vec2 at, const char *tag, Color color) {
    Entity &entity = zone(scene, name, at, {1.4F, 1.8F}, color, 1);
    auto &goal = entity.add<Goal>();
    goal.requiredTag = tag;
    goal.satisfiedColor = {255, 255, 255, 255};
    goal.sound.path = "tone:660,0.15,sine";
    return entity;
}
} // namespace

EntityId createFireCharacter(Scene &scene, Vec2 at) {
    return character(scene, "Fire Character", at, fireTag, fireColor, "Player1").id();
}
EntityId createWaterCharacter(Scene &scene, Vec2 at) {
    return character(scene, "Water Character", at, waterTag, waterColor, "Player2").id();
}
EntityId createFireExit(Scene &scene, Vec2 at) {
    return exit(scene, "Fire Exit", at, fireTag, {150, 55, 35, 255}).id();
}
EntityId createWaterExit(Scene &scene, Vec2 at) {
    return exit(scene, "Water Exit", at, waterTag, {35, 85, 160, 255}).id();
}
EntityId createLavaPool(Scene &scene, Vec2 at, Vec2 size) {
    return pool(scene, "Lava Pool", at, size, {240, 100, 30, 255}, {waterTag}).id();
}
EntityId createWaterPool(Scene &scene, Vec2 at, Vec2 size) {
    return pool(scene, "Water Pool", at, size, {50, 120, 230, 255}, {fireTag}).id();
}
EntityId createGooPool(Scene &scene, Vec2 at, Vec2 size) {
    return pool(scene, "Goo Pool", at, size, {110, 200, 60, 255}, {}).id();
}
EntityId createFireGem(Scene &scene, Vec2 at) {
    return gem(scene, "Fire Gem", at, fireTag, "fire_gems", {255, 150, 60, 255}).id();
}
EntityId createWaterGem(Scene &scene, Vec2 at) {
    return gem(scene, "Water Gem", at, waterTag, "water_gems", {110, 190, 255, 255}).id();
}
void registerPrototypeGame(ComponentRegistry &registry) {
    registry.addTemplate({"Fire Character", "Prototype",
                          [](Scene &scene, Vec2 at) { return createFireCharacter(scene, at); }});
    registry.addTemplate({"Water Character", "Prototype",
                          [](Scene &scene, Vec2 at) { return createWaterCharacter(scene, at); }});
    registry.addTemplate({"Fire Exit", "Prototype",
                          [](Scene &scene, Vec2 at) { return createFireExit(scene, at); }});
    registry.addTemplate({"Water Exit", "Prototype",
                          [](Scene &scene, Vec2 at) { return createWaterExit(scene, at); }});
    registry.addTemplate({"Lava Pool", "Prototype", [](Scene &scene, Vec2 at) {
                              return createLavaPool(scene, at, {3.0F, 0.6F});
                          }});
    registry.addTemplate({"Water Pool", "Prototype", [](Scene &scene, Vec2 at) {
                              return createWaterPool(scene, at, {3.0F, 0.6F});
                          }});
    registry.addTemplate({"Goo Pool", "Prototype", [](Scene &scene, Vec2 at) {
                              return createGooPool(scene, at, {3.0F, 0.6F});
                          }});
    registry.addTemplate(
        {"Fire Gem", "Prototype", [](Scene &scene, Vec2 at) { return createFireGem(scene, at); }});
    registry.addTemplate({"Water Gem", "Prototype",
                          [](Scene &scene, Vec2 at) { return createWaterGem(scene, at); }});
}
} // namespace yk::prototype
