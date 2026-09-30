#include "support/check.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <cmath>

using namespace yk;

namespace {
// Records every hook so tests can assert on ordering and arguments.
struct Probe final : Component {
    std::vector<std::string> log;
    std::string script; // "destroy@5", "velocity" ...
    int fixedCalls{};
    static void describe(TypeBuilder<Probe> &type) {
        type.category("Test");
        type.field("script", &Probe::script);
    }
    void onStart(GameContext &) override {
        log.push_back("start");
    }
    void onFixedUpdate(GameContext &context, float) override {
        ++fixedCalls;
        log.push_back(context.keyboard().state(Key::Space).pressed ? "pressed" : "tick");
        if (script == "destroy" && fixedCalls == 5)
            context.destroyLater(entity().id());
        if (script == "actions") {
            if (context.input().state("Player1", "Jump").pressed)
                log.push_back("jump");
            context.blackboard().add("right", context.input().value("Player1", "MoveRight"));
        }
        if (script == "velocity")
            if (const auto body = context.bodyOf(entity().id()))
                context.physics().setVelocity(*body, {2.0F, 0.0F});
        context.blackboard().add("ticks", 1);
    }
    void onUpdate(GameContext &, float) override {
        log.push_back("update");
    }
    void onLateUpdate(GameContext &, float) override {
        log.push_back("late");
    }
    void onTriggerEnter(GameContext &, Entity &other) override {
        log.push_back("enter:" + other.name());
    }
    void onTriggerExit(GameContext &, Entity &other) override {
        log.push_back("exit:" + other.name());
    }
    void onCollisionEnter(GameContext &, Entity &other, const CollisionInfo &) override {
        log.push_back("hit:" + other.name());
    }
    void onDestroy(GameContext &context) override {
        context.blackboard().add("destroyed", 1);
        log.push_back("destroy");
    }
    int count(const std::string &entry) const {
        return static_cast<int>(std::count(log.begin(), log.end(), entry));
    }
};

ComponentRegistry makeRegistry() {
    ComponentRegistry registry;
    registerEngineComponents(registry);
    registry.add<Probe>("Probe");
    return registry;
}

Entity &addBody(Scene &scene, const char *name, Vec2 position, Vec2 size, RigidBodyType type,
                bool trigger = false, const char *layer = "Default") {
    Entity &entity = scene.createEntity(name);
    entity.transform().position = position;
    entity.add<RigidBody>().type = type;
    auto &collider = entity.add<Collider>();
    collider.size = size;
    collider.isTrigger = trigger;
    collider.layer = layer;
    return entity;
}

Keyboard nothing() {
    return Keyboard{};
}
struct Fixture {
    ComponentRegistry registry = makeRegistry();
    std::unique_ptr<Scene> scene = std::make_unique<Scene>(registry, 7);
    std::unique_ptr<GameRuntime> runtime;
    RecordingAudio audio;
    RuntimeOptions options;
    GameRuntime &start() {
        options.audio = &audio;
        auto created = GameRuntime::create(std::move(scene), options);
        CHECK(created);
        runtime = std::move(created.value());
        return *runtime;
    }
};
void run(GameRuntime &runtime, int ticks, const Keyboard &keyboard = Keyboard{}) {
    for (int i = 0; i < ticks; ++i)
        runtime.stepOnce(keyboard);
}

void fallingBodyRests() {
    Fixture f;
    addBody(*f.scene, "Floor", {0, 10}, {20, 1}, RigidBodyType::Static);
    Entity &box = addBody(*f.scene, "Box", {0, 0}, {1, 1}, RigidBodyType::Dynamic);
    const EntityId id = box.id();
    GameRuntime &runtime = f.start();
    run(runtime, 180);
    const Entity *entity = runtime.scene().find(id);
    CHECK_NEAR(entity->worldPosition().y, 9.0,
               0.05); // Floor top is y=9.5; box center rests half a unit above.
    CHECK_NEAR(entity->worldPosition().x, 0.0, 0.01);
    const auto state = runtime.physics().state(*runtime.bodyOf(id)).value();
    CHECK(std::fabs(state.linearVelocity.y) < 0.05F);
    CHECK(runtime.tick() == 180 && std::fabs(runtime.time() - 3.0) < 1e-6);
    CHECK(runtime.entityOfBody(*runtime.bodyOf(id)) == entity);
    CHECK(!runtime.bodyOf(EntityId{12345}));
}

void layersAndScale() {
    Fixture f;
    f.options.layers = LayerConfig::defaults();
    f.options.layers.addLayer("Ground");
    f.options.layers.addLayer("Ghost");
    f.options.layers.setInteraction(0, 1, true); // Default <-> Ground.
    f.options.layers.setInteraction(1, 1, true);
    addBody(*f.scene, "Floor", {0, 10}, {20, 1}, RigidBodyType::Static, false, "Ground");
    const EntityId solid = addBody(*f.scene, "Solid", {-3, 0}, {1, 1}, RigidBodyType::Dynamic).id();
    const EntityId ghost =
        addBody(*f.scene, "Ghost", {3, 0}, {1, 1}, RigidBodyType::Dynamic, false, "Ghost").id();
    Entity &scaled = addBody(*f.scene, "Scaled", {8, 0}, {1, 1}, RigidBodyType::Dynamic);
    scaled.transform().scale = {2, 2}; // World-space collider becomes 2x2.
    const EntityId scaledId = scaled.id();
    setLogStderrEnabled(false);
    GameRuntime &runtime = f.start();
    setLogStderrEnabled(true);
    run(runtime, 180);
    CHECK_NEAR(runtime.scene().find(solid)->worldPosition().y, 9.0, 0.05);
    CHECK(runtime.scene().find(ghost)->worldPosition().y >
          12.0); // Fell straight through the floor.
    CHECK_NEAR(runtime.scene().find(scaledId)->worldPosition().y, 8.5,
               0.06); // Scaled box rests 1.0 above the top.
}

void triggers() {
    Fixture f;
    f.scene->settings.gravity = {0, 0};
    Entity &zone = addBody(*f.scene, "Zone", {0, 0}, {4, 4}, RigidBodyType::Static, true);
    zone.add<Probe>();
    Entity &other = addBody(*f.scene, "OtherZone", {0, 0}, {4, 4}, RigidBodyType::Static, true);
    (void)other;
    Entity &mover = addBody(*f.scene, "Mover", {-6, 0}, {1, 1}, RigidBodyType::Dynamic);
    mover.add<Probe>().script = "velocity"; // Sets +2 m/s each tick.
    const EntityId zoneId = zone.id(), moverId = mover.id();
    GameRuntime &runtime = f.start();
    run(runtime, 60); // x = -4.0 : just outside the zone edge (x=-2 minus half width).
    auto &zoneProbe = *runtime.scene().find(zoneId)->get<Probe>();
    auto &moverProbe = *runtime.scene().find(moverId)->get<Probe>();
    CHECK(zoneProbe.count("enter:Mover") == 0 && runtime.overlapping(zoneId).empty());
    run(runtime, 90); // Passes through: enters around x=-2.5, exits around x=2.5.
    CHECK(zoneProbe.count("enter:Mover") == 1 && zoneProbe.count("exit:Mover") == 0);
    CHECK(moverProbe.count("enter:Zone") == 1); // Both sides are notified.
    CHECK(runtime.overlapping(zoneId).size() == 1 && runtime.overlapping(zoneId)[0] == moverId);
    run(runtime, 120);
    CHECK(zoneProbe.count("exit:Mover") == 1 && moverProbe.count("exit:Zone") == 1);
    CHECK(runtime.overlapping(zoneId).empty());
    CHECK(zoneProbe.count("enter:OtherZone") == 0); // Triggers ignore each other by default.
    // Order: enter precedes exit.
    const auto enter = std::find(zoneProbe.log.begin(), zoneProbe.log.end(), "enter:Mover");
    const auto exit = std::find(zoneProbe.log.begin(), zoneProbe.log.end(), "exit:Mover");
    CHECK(enter < exit);
}

void detectTriggers() {
    Fixture f;
    f.scene->settings.gravity = {0, 0};
    Entity &first = addBody(*f.scene, "A", {0, 0}, {2, 2}, RigidBodyType::Static, true);
    first.add<Probe>();
    first.get<Collider>()->detectTriggers = true;
    addBody(*f.scene, "B", {0.5F, 0}, {2, 2}, RigidBodyType::Static, true);
    const EntityId id = first.id();
    GameRuntime &runtime = f.start();
    run(runtime, 3);
    CHECK(runtime.scene().find(id)->get<Probe>()->count("enter:B") == 1);
}

void activation() {
    Fixture f;
    Entity &box = addBody(*f.scene, "Box", {0, 0}, {1, 1}, RigidBodyType::Dynamic);
    box.setActive(false);
    const EntityId id = box.id();
    GameRuntime &runtime = f.start();
    run(runtime, 60);
    CHECK_NEAR(runtime.scene().find(id)->worldPosition().y, 0.0,
               1e-4); // Inactive bodies do not simulate.
    runtime.scene().find(id)->setActive(true);
    run(runtime, 30);
    CHECK(runtime.scene().find(id)->worldPosition().y > 1.0);
    runtime.scene().find(id)->setActive(false);
    const float frozen = runtime.scene().find(id)->worldPosition().y;
    run(runtime, 30);
    CHECK_NEAR(runtime.scene().find(id)->worldPosition().y, frozen, 1e-4);
}

void destruction() {
    Fixture f;
    Entity &victim = addBody(*f.scene, "Victim", {0, 0}, {1, 1}, RigidBodyType::Dynamic);
    victim.add<Probe>().script = "destroy";
    Entity &child = f.scene->createEntity("Child", victim.id());
    child.add<Collider>();
    const EntityId victimId = victim.id(), childId = child.id();
    addBody(*f.scene, "Bystander", {10, 0}, {1, 1}, RigidBodyType::Dynamic);
    GameRuntime &runtime = f.start();
    CHECK(runtime.physics().stats().bodies == 2 && runtime.physics().stats().shapes == 3);
    run(runtime, 10);
    CHECK(!runtime.scene().find(victimId) && !runtime.scene().find(childId) &&
          runtime.scene().size() == 1);
    CHECK(runtime.physics().stats().bodies == 1 && runtime.physics().stats().shapes == 1);
    CHECK(runtime.blackboard().number("destroyed") == 1); // onDestroy exactly once.
    CHECK(!runtime.bodyOf(victimId));
    run(runtime, 10); // The world keeps running afterwards.
    CHECK(runtime.physics().stats().bodies == 1);
}

void hookOrderAndRestart() {
    Fixture f;
    Entity &box = addBody(*f.scene, "Box", {1, 2}, {1, 1}, RigidBodyType::Dynamic);
    box.add<Probe>();
    const EntityId id = box.id();
    GameRuntime &runtime = f.start();
    runtime.stepOnce(nothing());
    auto *probe = runtime.scene().find(id)->get<Probe>();
    CHECK(probe->log.size() >= 4 && probe->log[0] == "start" && probe->log[1] == "tick" &&
          probe->log[2] == "update" && probe->log[3] == "late");
    run(runtime, 20);
    CHECK(probe->count("start") == 1 && probe->count("tick") == 21);
    CHECK(runtime.scene().find(id)->worldPosition().y > 2.5F);
    runtime.blackboard().set("mine", 5.0);
    runtime.events().emit({"x", {}, {}});
    CHECK(runtime.restart());
    const Entity *reborn = runtime.scene().find(id);
    CHECK(reborn && reborn->id() == id && std::fabs(reborn->worldPosition().y - 2.0F) < 1e-5F);
    CHECK(runtime.tick() == 0 && !runtime.blackboard().has("mine") &&
          runtime.events().pending() == 0);
    CHECK(reborn->get<Probe>()->log.empty()); // Fresh component instance.
    runtime.stepOnce(nothing());
    CHECK(runtime.scene().find(id)->get<Probe>()->count("start") == 1);
    CHECK(runtime.physics().stats().bodies == 1);
}

void inputEdges() {
    Fixture f;
    addBody(*f.scene, "Box", {0, 0}, {1, 1}, RigidBodyType::Static).add<Probe>();
    GameRuntime &runtime = f.start();
    Keyboard tap;
    tap.beginFrame();
    tap.set(Key::Space, true);
    runtime.update(1.0 / 30.0, tap); // Two ticks in one frame.
    auto *probe = runtime.scene().findByName("Box")->get<Probe>();
    CHECK(probe->count("pressed") == 1 &&
          probe->count("tick") == 1); // The edge reaches one tick only.
    Keyboard held;
    held.beginFrame();
    held.set(Key::Space, true);
    held.beginFrame(); // Still held, no new edge.
    runtime.update(1.0 / 60.0, held);
    CHECK(probe->count("pressed") == 1);

    Fixture g;
    addBody(*g.scene, "Box", {0, 0}, {1, 1}, RigidBodyType::Static).add<Probe>();
    GameRuntime &second = g.start();
    Keyboard early;
    early.beginFrame();
    early.set(Key::Space, true);
    second.update(0.001, early); // Too short for a tick: the edge must wait.
    auto *waiting = second.scene().findByName("Box")->get<Probe>();
    CHECK(waiting->count("pressed") == 0 && waiting->count("tick") == 0);
    Keyboard later;
    later.beginFrame();
    later.set(Key::Space, true);
    later.beginFrame();
    second.update(1.0 / 60.0, later);
    CHECK(waiting->count("pressed") == 1);
}

// Named actions reach components through the same fixed-tick retiming as raw keys: a button edge
// lands on exactly one tick even when the frame runs several, and analog values reach every tick.
void actionsThroughTheRuntime() {
    Fixture f;
    Entity &box = addBody(*f.scene, "Box", {0, 0}, {1, 1}, RigidBodyType::Static);
    box.add<Probe>().script = "actions";
    GameRuntime &runtime = f.start();
    InputFrame pad;
    pad.gamepads[0].setConnected(true);
    pad.gamepads[0].setButton(GamepadButton::South, true);
    pad.gamepads[0].setAxis(GamepadAxis::LeftX, 1.0F);
    runtime.update(1.0 / 30.0, pad); // Two ticks in one frame.
    auto *probe = runtime.scene().findByName("Box")->get<Probe>();
    CHECK(probe->count("jump") == 1);
    CHECK_NEAR(runtime.blackboard().number("right"), 2.0);
    CHECK(runtime.input().state("Player1", "Jump").held);

    // Keyboard-only callers still work: W is Player1's Jump in the standard map.
    Keyboard keys;
    keys.beginFrame();
    keys.set(Key::W, true);
    runtime.update(1.0 / 60.0, keys);
    // The pad was holding Jump; W taking over from it is a hand-over, not a new press.
    CHECK(probe->count("jump") == 1);
    keys.beginFrame();
    keys.set(Key::W, false);
    runtime.update(1.0 / 60.0, keys);
    keys.beginFrame();
    keys.set(Key::W, true);
    runtime.update(1.0 / 60.0, keys);
    CHECK(probe->count("jump") == 2);

    // A restart releases every action.
    CHECK(runtime.restart());
    CHECK(!runtime.input().state("Player1", "Jump").held);
}

void fixedStepping() {
    Fixture f;
    addBody(*f.scene, "Box", {0, 0}, {1, 1}, RigidBodyType::Static);
    GameRuntime &runtime = f.start();
    runtime.update(1.0 / 30.0, nothing());
    CHECK(runtime.tick() == 2);
    runtime.update(1.0 / 60.0 / 4.0, nothing()); // A quarter tick accumulates without running.
    CHECK(runtime.tick() == 2);
    runtime.update(1.0 / 60.0 * 0.75, nothing()); // Now a full tick has accumulated.
    CHECK(runtime.tick() == 3);
    runtime.update(10.0, nothing()); // Clamped to 0.25s and to 8 catch-up ticks.
    CHECK(runtime.tick() == 11);
    runtime.update(-5.0, nothing());
    runtime.update(std::nan(""), nothing());
    CHECK(runtime.tick() == 11);
    runtime.setPaused(true);
    runtime.update(1.0, nothing());
    CHECK(runtime.tick() == 11);
    runtime.setPaused(false);
    runtime.update(1.0 / 60.0, nothing());
    CHECK(runtime.tick() == 12);
}

void compoundBodyAndKinematic() {
    Fixture f;
    f.scene->settings.gravity = {0, 0};
    Entity &body = f.scene->createEntity("Compound");
    body.add<RigidBody>().type = RigidBodyType::Kinematic;
    Entity &left = f.scene->createEntity("Left", body.id());
    left.transform().position = {-3, 0};
    left.add<Collider>().size = {1, 1};
    Entity &right = f.scene->createEntity("Right", body.id());
    right.transform().position = {3, 0};
    right.add<Collider>().size = {1, 1};
    body.add<Probe>().script = "velocity";
    const EntityId id = body.id();
    GameRuntime &runtime = f.start();
    CHECK(runtime.physics().stats().bodies == 1 && runtime.physics().stats().shapes == 2);
    CHECK(runtime.physics().rayCast({-3, -5}, {0, 10}).value().has_value());
    CHECK(!runtime.physics().rayCast({0, -5}, {0, 10}).value().has_value()); // Nothing between the
                                                                             // two shapes.
    run(runtime, 60);
    CHECK_NEAR(runtime.scene().find(id)->worldPosition().x, 2.0,
               0.05); // Kinematic body follows its velocity.
    CHECK(runtime.physics().rayCast({-1, -5}, {0, 10}).value().has_value()); // Left shape moved
                                                                             // with it.
    CHECK_NEAR(runtime.scene().findByName("Left")->worldPosition().x, -1.0,
               0.05); // Children follow the body.
}

void colliderGeometry() {
    Fixture f;
    f.scene->settings.gravity = {0, 0};
    Entity &wall = addBody(*f.scene, "Wall", {0, 0}, {4, 0.5F}, RigidBodyType::Static);
    wall.transform().rotationDegrees = 90; // A 4 x 0.5 slab turned into a vertical wall.
    Entity &offsetBox = addBody(*f.scene, "Offset", {20, 0}, {2, 2}, RigidBodyType::Static);
    offsetBox.get<Collider>()->offset = {5, 0};
    Entity &circle = addBody(*f.scene, "Circle", {-20, 0}, {2, 2}, RigidBodyType::Static);
    circle.get<Collider>()->shape = ColliderShape::Circle;
    Entity &capsule = addBody(*f.scene, "Capsule", {-40, 0}, {1, 3}, RigidBodyType::Static);
    capsule.get<Collider>()->shape = ColliderShape::Capsule;
    GameRuntime &runtime = f.start();
    auto &world = runtime.physics();
    // The 4 x 0.5 slab turned 90 degrees occupies x in [-0.25, 0.25], y in [-2, 2].
    CHECK(world.rayCast({-3, 0}, {6, 0}).value().has_value());
    CHECK(world.rayCast({-3, 1.8F}, {6, 0})
              .value()
              .has_value()); // Only true when rotated (unrotated: y within 0.25).
    CHECK(!world.rayCast({1.5F, -3}, {0, 6})
               .value()
               .has_value()); // Only true when rotated (unrotated: x within 2).
    // The offset box sits 5 units right of its entity: x in [24, 26], y in [-1, 1].
    CHECK(world.rayCast({22, 0}, {4, 0}).value().has_value());
    CHECK(world.rayCast({25, -3}, {0, 6}).value().has_value());
    CHECK(!world.rayCast({20, -3}, {0, 6})
               .value()
               .has_value()); // Where an ignored offset would put it.
    CHECK(world.queryPoint({-20.9F, 0}).value().size() == 1 &&
          world.queryPoint({-21.1F, 0}).value().empty());
    CHECK(world.queryPoint({-40, 1.4F}).value().size() == 1 &&
          world.queryPoint({-40, 1.7F}).value().empty());
}

void spawning() {
    Fixture f;
    f.scene->settings.gravity = {0, 0};
    Entity &template_ = addBody(*f.scene, "Template", {0, 0}, {1, 1}, RigidBodyType::Dynamic);
    const Json prefab = subtreeToJson(*f.scene, template_.id());
    GameRuntime &runtime = f.start();
    CHECK(runtime.physics().stats().bodies == 1);
    auto spawned = runtime.spawn(prefab, {5, 5});
    CHECK(spawned && runtime.physics().stats().bodies == 2);
    const Entity *copy = runtime.scene().find(spawned.value());
    CHECK(copy && std::fabs(copy->worldPosition().x - 5.0F) < 1e-5F);
    CHECK(runtime.bodyOf(spawned.value()).has_value());
    const auto state = runtime.physics().state(*runtime.bodyOf(spawned.value())).value();
    CHECK_NEAR(state.pose.position.x, 5.0);
    CHECK(!runtime.spawn(Json::object(), {0, 0}));
    runtime.destroyLater(spawned.value());
    runtime.stepOnce(nothing());
    CHECK(runtime.physics().stats().bodies == 1 && !runtime.scene().find(spawned.value()));
}

void teleportAndCollisions() {
    Fixture f;
    Entity &floor = addBody(*f.scene, "Floor", {0, 10}, {20, 1}, RigidBodyType::Static);
    floor.add<Probe>();
    Entity &box = addBody(*f.scene, "Box", {0, 0}, {1, 1}, RigidBodyType::Dynamic);
    box.add<Probe>();
    const EntityId floorId = floor.id(), boxId = box.id();
    GameRuntime &runtime = f.start();
    run(runtime, 120);
    CHECK(runtime.scene().find(floorId)->get<Probe>()->count("hit:Box") >= 1);
    CHECK(runtime.scene().find(boxId)->get<Probe>()->count("hit:Floor") >= 1);
    runtime.teleport(*runtime.scene().find(boxId), {5, -3});
    const auto state = runtime.physics().state(*runtime.bodyOf(boxId)).value();
    CHECK_NEAR(state.pose.position.x, 5.0);
    CHECK_NEAR(state.pose.position.y, -3.0);
    CHECK_NEAR(state.linearVelocity.y, 0.0, 1e-6);
}

void cameraBehaviour() {
    Fixture f;
    f.options.viewportSize = {1600, 900};
    Entity &a = f.scene->createEntity("A");
    a.transform().position = {10, 10};
    Entity &b = f.scene->createEntity("B");
    b.transform().position = {30, 20};
    Entity &cameraEntity = f.scene->createEntity("Cam");
    auto &camera = cameraEntity.add<Camera>();
    camera.mode = CameraMode::FitTargets;
    camera.targets = {a.id(), b.id()};
    camera.padding = 2;
    camera.smoothTime = 0;
    camera.minHeight = 5;
    camera.maxHeight = 100;
    const EntityId camId = cameraEntity.id();
    const CameraView before = camera.view();
    CHECK(before.position == Vec2{0, 0} &&
          before.visibleHeight == 18.0F); // Authored state before running.
    GameRuntime &runtime = f.start();
    runtime.stepOnce(nothing());
    auto *live = runtime.scene().find(camId)->get<Camera>();
    CHECK_NEAR(live->view().position.x, 20.0);
    CHECK_NEAR(live->view().position.y, 15.0);
    // Needs width 24 (=20+2*2): at 16:9 that is 13.5 high, versus 14 (=10+2*2) needed for height.
    CHECK_NEAR(live->view().visibleHeight, 14.0, 1e-3);
    live->mode = CameraMode::Follow;
    live->orthographicHeight = 12;
    live->clampToBounds = true;
    live->boundsMin = {0, 0};
    live->boundsMax = {25, 18};
    runtime.stepOnce(nothing());
    CHECK_NEAR(live->view().visibleHeight, 12.0, 1e-3);
    CHECK(live->view().position.x <= 25 - 12 * (16.0 / 9.0) / 2 + 1e-3); // Kept inside the bounds.
    live->mode = CameraMode::Fixed;
    live->orthographicHeight = 30; // Larger than the bounds: centers on them instead of clamping.
    runtime.stepOnce(nothing());
    CHECK_NEAR(live->view().position.x, 12.5, 1e-3);
    CHECK_NEAR(live->view().position.y, 9.0, 1e-3);
    b.setActive(false); // Inactive targets are ignored.
    live->mode = CameraMode::Follow;
    live->clampToBounds = false;
    runtime.stepOnce(nothing());
    CHECK_NEAR(live->view().position.x, 10.0, 1e-3);
}

void smoothCamera() {
    Fixture f;
    Entity &target = f.scene->createEntity("T");
    target.transform().position = {100, 0};
    Entity &cameraEntity = f.scene->createEntity("Cam");
    auto &camera = cameraEntity.add<Camera>();
    camera.mode = CameraMode::Follow;
    camera.targets = {target.id()};
    camera.smoothTime = 0.5F;
    const EntityId id = cameraEntity.id(), targetId = target.id();
    GameRuntime &runtime = f.start();
    runtime.stepOnce(nothing()); // First frame snaps to the target.
    CHECK_NEAR(runtime.scene().find(id)->get<Camera>()->view().position.x, 100.0);
    runtime.scene().find(targetId)->transform().position = {200, 0};
    run(runtime, 30); // Half a second: should have closed about 63% of the distance.
    const float x = runtime.scene().find(id)->get<Camera>()->view().position.x;
    CHECK(x > 155.0F && x < 170.0F);
    run(runtime, 600);
    CHECK_NEAR(runtime.scene().find(id)->get<Camera>()->view().position.x, 200.0, 0.01);
}

void blackboardAndEvents() {
    Blackboard board;
    board.set("gems", 3.0);
    board.set("name", std::string("Lava"));
    board.set("ratio", 0.5);
    board.add("gems", 2);
    board.add("fresh", 1);
    CHECK(board.number("gems") == 5 && board.number("fresh") == 1 && board.number("none", 9) == 9);
    CHECK(board.number("name", 7) == 7 && board.text("gems") == "5" &&
          board.text("ratio") == "0.50");
    CHECK(board.format("Gems: {gems}/{name} {missing}!") == "Gems: 5/Lava !");
    CHECK(board.format("{{literal}} {open") == "{literal} {open");
    CHECK(board.format("{gems:0}/{later:9} {name:none}") ==
          "5/9 Lava"); // Fallback only while unset.
    CHECK(board.format("{a:b:c}") == "b:c");
    board.clear();
    CHECK(!board.has("gems"));

    EventBus bus;
    std::vector<std::string> seen;
    const auto a =
        bus.subscribe("go", [&](const GameEvent &event) { seen.push_back("a:" + event.name); });
    bus.subscribe("*", [&](const GameEvent &event) { seen.push_back("any:" + event.name); });
    bus.subscribe("chain", [&](const GameEvent &) { bus.emit({"go", {}, {}}); });
    bus.emit({"go", EntityId{1}, EntityId{2}});
    CHECK(seen.empty() && bus.pending() == 1); // Emission only queues.
    CHECK(bus.dispatch() == 1 && seen.size() == 2 && seen[0] == "a:go" && seen[1] == "any:go");
    bus.unsubscribe(a);
    seen.clear();
    bus.emit({"chain", {}, {}});
    CHECK(bus.dispatch() == 2); // Chained events delivered in the same dispatch.
    CHECK(seen.size() == 2 && seen[0] == "any:chain" && seen[1] == "any:go");
    CHECK(bus.recent().size() >= 2 && bus.recent().back().name == "go");
    bus.subscribe("loop", [&](const GameEvent &) { bus.emit({"loop", {}, {}}); });
    bus.emit({"loop", {}, {}});
    setLogStderrEnabled(false);
    CHECK(bus.dispatch() == 1024); // Runaway handlers are cut off, not looped forever.
    setLogStderrEnabled(true);
    CHECK(bus.pending() == 0);
}

void animationAndAudio() {
    Fixture f;
    auto assets = std::make_unique<MemoryAssets>();
    assets->files["anim/hero.ykanim"] =
        R"({"format":"yk.animation","version":1,"columns":4,"rows":1,"clips":[)"
        R"({"name":"idle","first":0,"count":1,"fps":2,"loop":true},)"
        R"({"name":"run","first":1,"count":3,"fps":10,"loop":true}]})";
    f.options.assets = assets.get();
    Entity &hero = f.scene->createEntity("Hero");
    hero.add<SpriteRenderer>();
    auto &animator = hero.add<AnimatedSprite>();
    animator.animation.path = "anim/hero.ykanim";
    animator.clip = "run";
    hero.add<AudioSource>().sound.path = "tone:440,0.1";
    hero.get<AudioSource>()->playOnStart = true;
    hero.get<AudioSource>()->volume = 0.5F;
    const EntityId id = hero.id();
    GameRuntime &runtime = f.start();
    runtime.stepOnce(nothing());
    auto *sprite = runtime.scene().find(id)->get<SpriteRenderer>();
    CHECK(sprite->columns == 4 && sprite->frame >= 1); // Sheet layout comes from the asset.
    run(runtime, 12);                                  // 0.2s at 10 fps = two frames later.
    CHECK(sprite->frame >= 1 && sprite->frame <= 3);
    runtime.scene().find(id)->get<AnimatedSprite>()->play("idle");
    runtime.scene().find(id)->get<AnimatedSprite>()->play("missing"); // Ignored.
    run(runtime, 2);
    CHECK(sprite->frame == 0);
    CHECK(f.audio.count("tone:440,0.1") == 1 && f.audio.requests[0].volume == 0.5F);

    Fixture missing;
    Entity &lonely = missing.scene->createEntity("Lonely");
    lonely.add<SpriteRenderer>();
    lonely.add<AnimatedSprite>().animation.path = "nope.ykanim";
    missing.options.assets = assets.get();
    setLogStderrEnabled(false);
    GameRuntime &tolerant = missing.start();
    tolerant.stepOnce(nothing()); // A missing animation asset is reported, not fatal.
    setLogStderrEnabled(true);
    CHECK(tolerant.tick() == 1);
}

void invalidOptions() {
    ComponentRegistry registry = makeRegistry();
    CHECK(!GameRuntime::create(nullptr));
    RuntimeOptions bad;
    bad.fixedSeconds = 0;
    CHECK(!GameRuntime::create(std::make_unique<Scene>(registry, 1), bad));
    RuntimeOptions viewport;
    viewport.viewportSize = {0, 100};
    CHECK(!GameRuntime::create(std::make_unique<Scene>(registry, 1), viewport));
    RuntimeOptions layers;
    layers.layers.masks = {};
    CHECK(!GameRuntime::create(std::make_unique<Scene>(registry, 1), layers));
    RuntimeOptions input;
    input.inputMap.sets.push_back(input.inputMap.sets.front()); // A set defined twice.
    CHECK(!GameRuntime::create(std::make_unique<Scene>(registry, 1), input));
}
} // namespace

int main() {
    fallingBodyRests();
    layersAndScale();
    triggers();
    detectTriggers();
    activation();
    destruction();
    hookOrderAndRestart();
    inputEdges();
    actionsThroughTheRuntime();
    fixedStepping();
    compoundBodyAndKinematic();
    colliderGeometry();
    spawning();
    teleportAndCollisions();
    cameraBehaviour();
    smoothCamera();
    blackboardAndEvents();
    animationAndAudio();
    invalidOptions();
    return yk::test::finish("game_runtime");
}
