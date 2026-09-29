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
Entity &character(Scene &scene, const char *name, Vec2 at, const char *tag, Color color, Key left, Key right, Key jump) {
    Entity &entity = place(scene, name, at);
    auto &sprite = entity.add<SpriteRenderer>();
    sprite.size = {0.6F, 0.95F};
    sprite.color = color;
    sprite.layer = 5;
    auto &controller = entity.add<PlatformerController>();
    controller.leftKey = left;
    controller.rightKey = right;
    controller.jumpKey = jump;
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
Entity &pool(Scene &scene, const char *name, Vec2 at, Vec2 size, Color color, std::vector<std::string> kills) {
    Entity &entity = zone(scene, name, at, size, color, 1);
    entity.add<Hazard>().affectsTags = std::move(kills);
    return entity;
}
Entity &gem(Scene &scene, const char *name, Vec2 at, const char *collector, const char *variable, Color color) {
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
    return character(scene, "Fire Character", at, fireTag, fireColor, Key::A, Key::D, Key::W).id();
}
EntityId createWaterCharacter(Scene &scene, Vec2 at) {
    return character(scene, "Water Character", at, waterTag, waterColor, Key::Left, Key::Right, Key::Up).id();
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
EntityId createLevelFlow(Scene &scene) {
    Entity &entity = place(scene, "Level Flow", {0.0F, 0.0F});
    entity.add<LevelFlow>();
    return entity.id();
}

void LevelFlow::describe(TypeBuilder<LevelFlow> &type) {
    type.category("Prototype")
        .description("Level rules: complete when every goal is satisfied; optionally restart when anyone dies.");
    type.field("goals", &LevelFlow::goals).tooltip("Goal entities that must all be satisfied at once.");
    type.field("restartOnDeath", &LevelFlow::restartOnDeath).tooltip("Restart the whole level when anyone dies.");
    type.field("restartDelay", &LevelFlow::restartDelay).range(0, 30, 0.1);
    type.field("completeDelay", &LevelFlow::completeDelay).range(0, 30, 0.1);
    type.field("nextScene", &LevelFlow::nextScene).tooltip("Project-relative scene to load after completion.");
    type.field("restartKey", &LevelFlow::restartKey).keyOptions();
    type.field("completeMessage", &LevelFlow::completeMessage);
    type.field("failMessage", &LevelFlow::failMessage);
    type.field("completeSound", &LevelFlow::completeSound).asset("sound");
    type.field("failSound", &LevelFlow::failSound).asset("sound");
    type.field("state", &LevelFlow::state_).readOnly().options({"Playing", "Complete", "Failed"});
}

void LevelFlow::onStart(GameContext &context) {
    state_ = State::Playing;
    elapsed_ = 0.0F;
    context.blackboard().set("level_state", std::string("playing"));
    context.blackboard().set("level_message", std::string());
    context.blackboard().set("level_time", 0.0);
    deathSubscription_ = context.events().subscribe("entity_died", [this, &context](const GameEvent &) {
        if (!restartOnDeath || state_ != State::Playing)
            return;
        state_ = State::Failed;
        timer_ = restartDelay;
        context.blackboard().set("level_state", std::string("failed"));
        context.blackboard().set("level_message", failMessage);
        if (!failSound.path.empty())
            context.audio().play(failSound.path);
    });
}

void LevelFlow::onFixedUpdate(GameContext &context, float seconds) {
    if (context.keyboard().state(restartKey).pressed) {
        context.requestRestart();
        return;
    }
    if (state_ == State::Playing) {
        elapsed_ += seconds;
        context.blackboard().set("level_time", std::floor(static_cast<double>(elapsed_)));
        bool all = !goals.empty();
        for (const EntityRef reference : goals) {
            const Entity *goalEntity = context.scene().find(reference);
            const auto *goal = goalEntity ? goalEntity->get<Goal>() : nullptr;
            all = all && goal && goal->satisfied();
        }
        if (all) {
            state_ = State::Complete;
            timer_ = completeDelay;
            context.blackboard().set("level_state", std::string("complete"));
            context.blackboard().set("level_message", completeMessage);
            if (!completeSound.path.empty())
                context.audio().play(completeSound.path);
            context.emit("level_completed", entity().id());
        }
        return;
    }
    timer_ -= seconds;
    if (timer_ > 0.0F)
        return;
    if (state_ == State::Failed) {
        context.requestRestart();
    } else if (!nextScene.empty()) {
        context.requestSceneChange(nextScene);
    }
}

void LevelFlow::onDestroy(GameContext &context) {
    context.events().unsubscribe(deathSubscription_);
}

void registerPrototypeGame(ComponentRegistry &registry) {
    registry.add<LevelFlow>("LevelFlow");
    registry.addTemplate({"Fire Character", "Prototype", [](Scene &scene, Vec2 at) { return createFireCharacter(scene, at); }});
    registry.addTemplate({"Water Character", "Prototype", [](Scene &scene, Vec2 at) { return createWaterCharacter(scene, at); }});
    registry.addTemplate({"Fire Exit", "Prototype", [](Scene &scene, Vec2 at) { return createFireExit(scene, at); }});
    registry.addTemplate({"Water Exit", "Prototype", [](Scene &scene, Vec2 at) { return createWaterExit(scene, at); }});
    registry.addTemplate({"Lava Pool", "Prototype", [](Scene &scene, Vec2 at) { return createLavaPool(scene, at, {3.0F, 0.6F}); }});
    registry.addTemplate({"Water Pool", "Prototype", [](Scene &scene, Vec2 at) { return createWaterPool(scene, at, {3.0F, 0.6F}); }});
    registry.addTemplate({"Goo Pool", "Prototype", [](Scene &scene, Vec2 at) { return createGooPool(scene, at, {3.0F, 0.6F}); }});
    registry.addTemplate({"Fire Gem", "Prototype", [](Scene &scene, Vec2 at) { return createFireGem(scene, at); }});
    registry.addTemplate({"Water Gem", "Prototype", [](Scene &scene, Vec2 at) { return createWaterGem(scene, at); }});
    registry.addTemplate({"Level Flow", "Prototype", [](Scene &scene, Vec2) { return createLevelFlow(scene); }});
}
} // namespace yk::prototype
