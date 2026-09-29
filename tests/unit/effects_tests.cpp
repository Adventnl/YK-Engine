// Particle emitters, glows, oscillators and self-destroying effect prefabs, run headlessly. Time
// advances one 1/60 s tick per stepOnce, so everything here is deterministic.
#include "support/check.hpp"
#include "yk/components/Effects.hpp"
#include "yk/core/Log.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <cmath>

using namespace yk;

namespace {
ComponentRegistry makeRegistry() {
    ComponentRegistry registry;
    registerEngineComponents(registry);
    return registry;
}

struct Fixture {
    ComponentRegistry registry = makeRegistry();
    std::unique_ptr<Scene> scene = std::make_unique<Scene>(registry, 21);
    MemoryAssets assets;
    RuntimeOptions options;
    std::unique_ptr<GameRuntime> runtime;

    GameRuntime &start() {
        options.assets = &assets;
        auto created = GameRuntime::create(std::move(scene), options);
        CHECK(created);
        runtime = std::move(created.value());
        return *runtime;
    }
    void tick(int count) {
        for (int i = 0; i < count; ++i)
            runtime->stepOnce(Keyboard{});
    }
};

// A fixed-lifetime, fixed-size emitter that shoots straight along `direction`.
ParticleEmitter &shooter(Entity &entity, float directionDegrees, float speed) {
    auto &emitter = entity.add<ParticleEmitter>();
    emitter.spread = 0.0F;
    emitter.direction = directionDegrees;
    emitter.speed = {speed, speed};
    emitter.lifetime = {1.0F, 1.0F};
    emitter.startSize = {0.2F, 0.2F};
    return emitter;
}

void emission() {
    // A one-shot burst: exactly `burst` particles at once, all gone after their lifetime.
    {
        Fixture f;
        Entity &puff = f.scene->createEntity("Puff");
        auto &emitter = shooter(puff, 0, 0);
        emitter.loop = false;
        emitter.duration = 0.1F;
        emitter.rate = 0.0F;
        emitter.burst = 10;
        const EntityId id = puff.id();
        GameRuntime &runtime = f.start();
        f.tick(1);
        auto *live = runtime.scene().find(id)->get<ParticleEmitter>();
        CHECK(live->particles().size() == 10);
        CHECK(!live->finished()); // Particles still alive.
        f.tick(50);
        CHECK(live->particles().size() == 10);
        f.tick(15); // Past the 1 s lifetime.
        CHECK(live->particles().empty() && live->finished() && !live->playing());
    }
    // Rate emission: 30 per second for a second of a looping emitter is about 30 particles alive
    // (lifetime 1 s), and never more than maxParticles.
    {
        Fixture f;
        Entity &spout = f.scene->createEntity("Spout");
        auto &emitter = shooter(spout, 0, 1);
        emitter.rate = 30.0F;
        emitter.maxParticles = 20;
        const EntityId id = spout.id();
        GameRuntime &runtime = f.start();
        f.tick(45);
        CHECK(runtime.scene().find(id)->get<ParticleEmitter>()->particles().size() == 20);
        f.tick(200); // Long after: still capped, still emitting.
        CHECK(runtime.scene().find(id)->get<ParticleEmitter>()->particles().size() == 20);

        Fixture g;
        Entity &slow = g.scene->createEntity("Slow");
        shooter(slow, 0, 1).rate = 30.0F;
        const EntityId slowId = slow.id();
        GameRuntime &second = g.start();
        g.tick(60);
        const auto alive = second.scene().find(slowId)->get<ParticleEmitter>()->particles().size();
        CHECK(alive >= 28 && alive <= 31);
    }
    // A stopped emitter emits nothing more; play() starts it again with a fresh burst.
    {
        Fixture f;
        Entity &spout = f.scene->createEntity("Spout");
        auto &emitter = shooter(spout, 0, 0);
        emitter.rate = 60.0F;
        emitter.burst = 3;
        emitter.playOnStart = false;
        const EntityId id = spout.id();
        GameRuntime &runtime = f.start();
        f.tick(10);
        auto *live = runtime.scene().find(id)->get<ParticleEmitter>();
        CHECK(live->particles().empty() && !live->playing());
        live->play();
        CHECK(live->particles().size() == 3);
        f.tick(10);
        const auto after = live->particles().size();
        CHECK(after >= 12 && after <= 14);
        live->stop();
        f.tick(10);
        CHECK(live->particles().size() == after);
    }
}

void motion() {
    Fixture f;
    // Direction is clockwise from up: 0 goes up (-y), 90 right (+x), 180 down (+y).
    Entity &up = f.scene->createEntity("Up");
    shooter(up, 0, 2).burst = 1;
    Entity &right = f.scene->createEntity("Right");
    shooter(right, 90, 2).burst = 1;
    Entity &down = f.scene->createEntity("Down");
    auto &falling = shooter(down, 180, 2);
    falling.burst = 1;
    falling.gravity = {0.0F, 10.0F};
    for (Entity *entity : {&up, &right, &down}) {
        entity->transform().position = {10.0F, 10.0F};
        entity->get<ParticleEmitter>()->rate = 0.0F;
    }
    const EntityId upId = up.id(), rightId = right.id(), downId = down.id();
    GameRuntime &runtime = f.start();
    f.tick(31); // Half a second (plus the start tick).
    const auto position = [&](EntityId id) {
        return runtime.scene().find(id)->get<ParticleEmitter>()->particles().front().position;
    };
    CHECK_NEAR(position(upId).x, 10.0F, 1e-3);
    CHECK(position(upId).y < 10.0F - 0.9F && position(upId).y > 10.0F - 1.1F);   // ~1 m up.
    CHECK(position(rightId).x > 10.9F && position(rightId).x < 11.1F);          // ~1 m right.
    CHECK_NEAR(position(rightId).y, 10.0F, 1e-3);
    // Down with gravity, semi-implicit Euler over 31 steps of 1/60 s: sum of (2 + 10 i/60)/60 for
    // i = 1..31 = 1.033 + 1.378 = 2.41 m below (the continuous answer for 0.517 s is 2.40).
    CHECK(position(downId).y > 12.35F && position(downId).y < 12.47F);
}

void spaces() {
    // A moving emitter: world-space particles stay where they were born; local-space ones follow.
    Fixture f;
    Entity &worldSpace = f.scene->createEntity("World");
    shooter(worldSpace, 0, 0).burst = 1;
    Entity &localSpace = f.scene->createEntity("Local");
    auto &local = shooter(localSpace, 0, 0);
    local.burst = 1;
    local.localSpace = true;
    for (Entity *entity : {&worldSpace, &localSpace}) {
        entity->get<ParticleEmitter>()->rate = 0.0F;
        entity->get<ParticleEmitter>()->lifetime = {5.0F, 5.0F};
    }
    const EntityId worldId = worldSpace.id(), localId = localSpace.id();
    GameRuntime &runtime = f.start();
    f.tick(2);
    runtime.scene().find(worldId)->transform().position = {5.0F, 0.0F};
    runtime.scene().find(localId)->transform().position = {5.0F, 0.0F};
    f.tick(2);
    const auto &worldParticle = runtime.scene().find(worldId)->get<ParticleEmitter>()->particles().front();
    const auto &localParticle = runtime.scene().find(localId)->get<ParticleEmitter>()->particles().front();
    CHECK_NEAR(worldParticle.position.x, 0.0F, 1e-3); // Left behind.
    CHECK_NEAR(localParticle.position.x, 0.0F, 1e-3); // Local position unchanged: it moves with the entity.
}

void determinism() {
    const auto run = [] {
        Fixture f;
        Entity &sparks = f.scene->createEntity("Sparks");
        auto &emitter = sparks.add<ParticleEmitter>();
        emitter.rate = 50.0F;
        emitter.area = EmitterArea::Circle;
        emitter.speed = {1.0F, 4.0F};
        emitter.gravity = {0.0F, 9.0F};
        const EntityId id = sparks.id();
        GameRuntime &runtime = f.start();
        f.tick(90);
        std::vector<Vec2> positions;
        for (const auto &particle : runtime.scene().find(id)->get<ParticleEmitter>()->particles())
            positions.push_back(particle.position);
        return positions;
    };
    const auto first = run();
    const auto second = run();
    CHECK(!first.empty() && first == second); // Same scene, same steps, same particles.
}

void cleanup() {
    // Lifetime destroys the entity after its time...
    {
        Fixture f;
        Entity &short_lived = f.scene->createEntity("Short");
        short_lived.add<Lifetime>().seconds = 0.5F;
        Entity &long_lived = f.scene->createEntity("Long");
        long_lived.add<Lifetime>().seconds = 5.0F;
        GameRuntime &runtime = f.start();
        f.tick(20);
        CHECK(runtime.scene().findByName("Short") && runtime.scene().findByName("Long"));
        f.tick(20);
        CHECK(!runtime.scene().findByName("Short") && runtime.scene().findByName("Long"));
    }
    // ...or as soon as a one-shot emitter has finished.
    {
        Fixture f;
        Entity &effect = f.scene->createEntity("Effect");
        auto &emitter = shooter(effect, 0, 1);
        emitter.loop = false;
        emitter.duration = 0.05F;
        emitter.rate = 0.0F;
        emitter.burst = 5;
        emitter.lifetime = {0.3F, 0.3F};
        auto &life = effect.add<Lifetime>();
        life.seconds = 60.0F;
        life.untilEmitterFinished = true;
        GameRuntime &runtime = f.start();
        f.tick(10);
        CHECK(runtime.scene().findByName("Effect") != nullptr);
        f.tick(15);
        CHECK(runtime.scene().findByName("Effect") == nullptr);
    }
    // Effect prefabs spawn through GameContext::spawnPrefab, clean up after themselves, and the
    // document is only read once.
    {
        Fixture f;
        Scene prefabScene(f.registry, 3);
        Entity &root = prefabScene.createEntity("Sparkle");
        auto &emitter = shooter(root, 0, 1);
        emitter.loop = false;
        emitter.rate = 0.0F;
        emitter.burst = 4;
        root.add<Lifetime>().seconds = 0.5F;
        f.assets.files["effects/sparkle.ykprefab"] = subtreeToJson(prefabScene, root.id()).dump();
        f.scene->createEntity("Anchor");
        GameRuntime &runtime = f.start();
        const auto spawned = runtime.spawnPrefab("effects/sparkle.ykprefab", {3.0F, 4.0F});
        CHECK(spawned);
        if (spawned) {
            const Entity *entity = runtime.scene().find(spawned.value());
            CHECK(entity && entity->worldPosition() == Vec2{3.0F, 4.0F});
        }
        f.tick(1);
        CHECK(runtime.scene().findByName("Sparkle")->get<ParticleEmitter>()->particles().size() == 4);
        f.tick(40);
        CHECK(runtime.scene().findByName("Sparkle") == nullptr); // Cleaned itself up.
        CHECK(runtime.spawnPrefab("effects/sparkle.ykprefab", {0, 0})); // From the cache.
        setLogStderrEnabled(false);
        CHECK(!runtime.spawnPrefab("effects/missing.ykprefab", {0, 0}));
        setLogStderrEnabled(true);
    }
}

void oscillatorAndGlow() {
    Fixture f;
    Entity &grass = f.scene->createEntity("Grass");
    grass.transform().position = {4.0F, 2.0F};
    auto &sway = grass.add<Oscillator>();
    sway.rotation = 10.0F;
    sway.position = {0.5F, 0.0F};
    sway.frequency = 1.0F;
    sway.randomPhase = false;
    Entity &crate = f.scene->createEntity("Body");
    crate.add<RigidBody>();
    crate.add<Oscillator>().position = {5.0F, 0.0F};
    Entity &torch = f.scene->createEntity("Torch");
    auto &glow = torch.add<Light2D>();
    glow.intensity = 0.8F;
    glow.flicker = 0.5F;
    const EntityId grassId = grass.id(), torchId = torch.id();
    setLogStderrEnabled(false);
    GameRuntime &runtime = f.start();
    setLogStderrEnabled(true);
    // A quarter cycle at 1 Hz (15 ticks after the start tick) is the sine's peak.
    f.tick(16);
    const Transform2D peak = runtime.scene().find(grassId)->transform();
    CHECK_NEAR(peak.rotationDegrees, 10.0F, 0.3);
    CHECK_NEAR(peak.position.x, 4.5F, 0.02);
    CHECK_NEAR(peak.position.y, 2.0F, 1e-4);
    f.tick(30); // Half a cycle further: the opposite peak.
    CHECK_NEAR(runtime.scene().find(grassId)->transform().rotationDegrees, -10.0F, 0.5);
    // An entity with a body is left alone (physics owns it).
    CHECK_NEAR(runtime.scene().findByName("Body")->transform().position.x, 0.0F, 1e-4);

    // Flicker stays inside [intensity * (1 - flicker), intensity].
    float lowest = 10.0F, highest = -10.0F;
    for (int i = 0; i < 300; ++i) {
        f.tick(1);
        const float now = runtime.scene().find(torchId)->get<Light2D>()->currentIntensity();
        lowest = std::min(lowest, now);
        highest = std::max(highest, now);
    }
    CHECK(lowest >= 0.8F * 0.5F - 1e-4F && highest <= 0.8F + 1e-4F);
    CHECK(highest - lowest > 0.1F); // It actually varies.
}

void savedAndLoaded() {
    ComponentRegistry registry = makeRegistry();
    Scene scene(registry, 5);
    Entity &entity = scene.createEntity("Fx");
    auto &emitter = entity.add<ParticleEmitter>();
    emitter.rate = 77.0F;
    emitter.area = EmitterArea::Box;
    emitter.blend = SpriteBlend::Alpha;
    emitter.startColor = {1, 2, 3, 4};
    emitter.lifetime = {0.25F, 0.75F};
    entity.add<Light2D>().flicker = 0.3F;
    entity.add<Oscillator>().wave = Wave::Triangle;
    entity.add<Lifetime>().untilEmitterFinished = true;
    const Json saved = sceneToJson(scene);
    auto loaded = sceneFromJson(saved, registry);
    CHECK(loaded);
    if (!loaded)
        return;
    const Entity *again = loaded.value()->findByName("Fx");
    CHECK(again != nullptr);
    if (!again)
        return;
    CHECK(again->get<ParticleEmitter>()->rate == 77.0F && again->get<ParticleEmitter>()->area == EmitterArea::Box);
    CHECK(again->get<ParticleEmitter>()->startColor == Color{1, 2, 3, 4});
    CHECK(again->get<ParticleEmitter>()->lifetime == Vec2{0.25F, 0.75F});
    CHECK(again->get<Light2D>()->flicker == 0.3F && again->get<Oscillator>()->wave == Wave::Triangle);
    CHECK(again->get<Lifetime>()->untilEmitterFinished);
    CHECK(sceneToJson(*loaded.value()).dump() == saved.dump()); // Stable text.
}
} // namespace

int main() {
    emission();
    motion();
    spaces();
    determinism();
    cleanup();
    oscillatorAndGlow();
    savedAndLoaded();
    return yk::test::finish("effects");
}
