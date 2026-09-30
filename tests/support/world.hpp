#pragma once
// A scene under construction plus the runtime once started: the scaffold the gameplay and mechanism
// tests build their little levels with (a floor, characters driven by scripted keys, real physics).
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/audio/Audio.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace yk::test {
constexpr float floorTop = 10.0F;

inline LayerConfig testLayers() {
    LayerConfig layers = LayerConfig::defaults();
    for (const char *name : {layers::solid, layers::player, layers::sensor, layers::prop})
        layers.addLayer(name);
    const auto index = [&](const char *name) {
        return static_cast<std::size_t>(layers.indexOf(name));
    };
    layers.setInteraction(index(layers::player), index(layers::solid), true);
    layers.setInteraction(index(layers::player), index(layers::player), true);
    layers.setInteraction(index(layers::player), index(layers::sensor), true);
    layers.setInteraction(index(layers::prop), index(layers::solid), true);
    layers.setInteraction(index(layers::prop), index(layers::player), true);
    layers.setInteraction(index(layers::prop), index(layers::sensor), true);
    layers.setInteraction(index(layers::prop), index(layers::prop), true);
    return layers;
}

inline ComponentRegistry makeRegistry() {
    ComponentRegistry registry;
    registerEngineComponents(registry);
    registerGameplayComponents(registry);
    return registry;
}

// A scene under construction plus the runtime once started.
struct World {
    ComponentRegistry registry = makeRegistry();
    std::unique_ptr<Scene> scene = std::make_unique<Scene>(registry, 11);
    std::unique_ptr<GameRuntime> runtime;
    RecordingAudio audio;
    MemoryAssets assets;
    Keyboard keyboard;
    std::vector<std::string> events;

    Entity &box(const char *name, Vec2 center, Vec2 size, const char *layer = layers::solid) {
        Entity &entity = scene->createEntity(name);
        entity.transform().position = center;
        auto &collider = entity.add<Collider>();
        collider.size = size;
        collider.layer = layer;
        return entity;
    }
    Entity &ground(float left = -40.0F, float right = 40.0F) {
        return box("Ground", {(left + right) / 2, floorTop + 0.5F}, {right - left, 1.0F});
    }
    // `actionSet` names the input-map set that drives it ("Player1" is A/D/W, "Player2" the arrow
    // keys); a set that does not exist leaves the character uncontrolled.
    Entity &character(const char *name, Vec2 at, const char *actionSet = "Player1",
                      const char *tag = "") {
        Entity &entity = scene->createEntity(name);
        entity.transform().position = at;
        entity.add<PlatformerController>();
        entity.get<PlayerInput>()->actionSet = actionSet;
        entity.add<Killable>();
        entity.add<SpriteRenderer>().size = {0.6F, 0.95F};
        if (*tag)
            entity.addTag(tag);
        return entity;
    }
    GameRuntime &start() {
        RuntimeOptions options;
        options.layers = testLayers();
        options.audio = &audio;
        options.assets = &assets;
        auto created = GameRuntime::create(std::move(scene), options);
        CHECK(created);
        runtime = std::move(created.value());
        runtime->events().subscribe(
            "*", [this](const GameEvent &event) { events.push_back(event.name); });
        return *runtime;
    }
    // Input driver: keys stay down until released; press/release edges last one tick.
    void down(Key key) {
        keyboard.set(key, true);
    }
    void up(Key key) {
        keyboard.set(key, false);
    }
    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) {
            runtime->stepOnce(keyboard);
            keyboard.beginFrame();
        }
    }
    // Ticks `count` times and calls `each(tickIndex)` after every one (to sample motion).
    template <class Each> void tick(int count, Each each) {
        for (int i = 0; i < count; ++i) {
            tick();
            each(i);
        }
    }
    // A box with a body of the given type (kinematic: moved by components; dynamic: simulated).
    Entity &body(const char *name, Vec2 center, Vec2 size, RigidBodyType type,
                 const char *layer = layers::solid) {
        Entity &entity = box(name, center, size, layer);
        entity.add<RigidBody>().type = type;
        return entity;
    }
    Entity &at(const char *name) {
        return *runtime->scene().findByName(name);
    }
    Vec2 position(const char *name) {
        return at(name).worldPosition();
    }
    bool happened(const std::string &name) const {
        return std::find(events.begin(), events.end(), name) != events.end();
    }
    int count(const std::string &name) const {
        return static_cast<int>(std::count(events.begin(), events.end(), name));
    }
};

constexpr float restingHeight = floorTop - 0.475F; // Capsule center when standing on the floor.
} // namespace yk::test
