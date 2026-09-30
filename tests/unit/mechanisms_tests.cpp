// Physical mechanisms on real physics: plates that are solid surfaces and sink under a load,
// platforms that lift, lower and turn with riders on them, angled surfaces, hinged planks, props
// that stack, and things that move fast. Each test states what a player would see.
#include "support/check.hpp"
#include "support/world.hpp"
#include "yk/core/Log.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace yk;
using namespace yk::test;

namespace {
// Counts the collision callbacks its entity receives (a component a game module might write).
class Probe final : public Component {
  public:
    int entered{};
    int exited{};
    void onCollisionEnter(GameContext &, Entity &, const CollisionInfo &) override {
        ++entered;
    }
    void onCollisionExit(GameContext &, Entity &, const CollisionInfo &) override {
        ++exited;
    }
};

ComponentRegistry makeProbeRegistry() {
    ComponentRegistry registry = makeRegistry();
    registry.add<Probe>("Probe");
    return registry;
}

// A world whose registry also knows the probe component.
struct ProbeWorld : World {
    ProbeWorld() {
        registry = makeProbeRegistry();
        scene = std::make_unique<Scene>(registry, 11);
    }
};

// YK_TEST_TRACE=1 prints what the tests sample, to see what a failing check saw.
bool tracing() {
    static const bool on = std::getenv("YK_TEST_TRACE") != nullptr;
    return on;
}

// Where a character's feet are (the bottom of its capsule).
float feet(World &w, const char *name) {
    return w.position(name).y + 0.475F;
}

// A plate whose top sticks up `rise` above the floor: a root (the art would go here) and a pad, a
// kinematic solid box, as a child. The pad's edges are rounded so that a character can run up onto
// it: the controller treats a surface steeper than its slope limit as a wall.
constexpr float plateRise = 0.14F;
constexpr float plateDepth = 0.09F;
struct BuiltPlate {
    Entity *root{};
    Entity *pad{};
};
BuiltPlate buildPlate(World &w, float x, float rise = plateRise, float depth = plateDepth,
                      const char *name = "Plate") {
    Entity &root = w.scene->createEntity(name);
    root.transform().position = {x, floorTop};
    // The pad's collider is 0.5 tall with its top `rise` above the floor; its edges slope down
    // to below the floor line, so a crate pushed at it meets a ramp and not a lip.
    Entity &pad = w.scene->createEntity("Pad", root.id());
    pad.transform().position = {0.0F, -rise + 0.25F};
    pad.add<RigidBody>().type = RigidBodyType::Kinematic;
    auto &collider = pad.add<Collider>();
    collider.size = {1.4F, 0.5F};
    collider.chamfer = {0.35F, 0.2F};
    collider.layer = layers::solid;
    collider.friction = 0.8F;
    auto &plate = *static_cast<PressurePlate *>(root.addComponent("PressurePlate", false));
    plate.pad = pad.id();
    plate.pressDepth = depth;
    return {&root, &pad};
}

float padTop(World &w, const BuiltPlate &plate) {
    return w.at(plate.pad->name().c_str()).worldPosition().y - 0.25F;
}

// The tests --------------------------------------------------------------------------------

// The floor button, as the brief describes it: the player falls on it, lands on its top surface,
// stands there, the button goes down under the player's weight carrying the player, stops at the
// bottom, and comes back up when the player leaves. No sinking into it, no jitter, no teleport.
void playerStandsOnAPlateThatSinks() {
    World w;
    w.ground();
    const BuiltPlate plate = buildPlate(w, 0.0F);
    Entity &door = w.body("Door", {8.0F, floorTop - 1.5F}, {0.6F, 3.0F}, RigidBodyType::Kinematic);
    auto &gate = door.add<Door>();
    gate.openOffset = {0.0F, -3.2F};
    gate.speed = 4.0F;
    plate.root->get<PressurePlate>()->targets = {door.id()};
    w.character("Hero", {0.0F, floorTop - 4.0F}, "Player1", "hero");
    w.start();
    const float restTop = padTop(w, plate);
    CHECK_NEAR(restTop, floorTop - plateRise, 1e-3);

    // Fall onto it, then stand. Sample every tick: the feet must never sink below the pad's top,
    // and after the first contact they never float more than a hair above it.
    float deepest = -1e9F;
    float lastFeet = feet(w, "Hero");
    float biggestJump = 0.0F;
    bool touched = false;
    std::vector<float> padHistory;
    w.tick(240, [&](int i) {
        const float top = padTop(w, plate);
        const float gap = top - feet(w, "Hero"); // Positive: feet above the pad's top.
        if (tracing() && (i < 90))
            std::printf("t=%3d feet=%.3f top=%.3f gap=%.3f vy=%.2f amount=%.2f\n", i,
                        static_cast<double>(feet(w, "Hero")), static_cast<double>(top),
                        static_cast<double>(gap),
                        static_cast<double>(w.runtime->physics()
                                                .state(*w.runtime->bodyOf(w.at("Hero").id()))
                                                .value()
                                                .linearVelocity.y),
                        static_cast<double>(plate.root->get<PressurePlate>()->pressAmount()));
        deepest = std::max(deepest, -gap);
        if (touched) { // From the tick after first contact: the fall itself is not a jump.
            biggestJump = std::max(biggestJump, std::fabs(feet(w, "Hero") - lastFeet));
            CHECK(gap > -0.05F && gap < 0.06F);
        }
        if (gap < 0.02F)
            touched = true;
        padHistory.push_back(top);
        lastFeet = feet(w, "Hero");
    });
    CHECK(touched);
    CHECK(deepest < 0.05F); // Never clipped into the plate.
    CHECK(w.at("Hero").get<PlatformerController>()->grounded());
    // The plate went all the way down and stopped there.
    const PressurePlate &state = *plate.root->get<PressurePlate>();
    CHECK(state.pressed());
    CHECK_NEAR(state.pressAmount(), 1.0, 1e-4);
    CHECK_NEAR(padTop(w, plate), restTop + plateDepth, 0.01);
    CHECK_NEAR(feet(w, "Hero"), padTop(w, plate), 0.03); // Standing on it, at the bottom.
    // Going down, the pad never went back up (no bounce, no jitter).
    for (std::size_t i = 1; i < padHistory.size(); ++i)
        CHECK(padHistory[i] >= padHistory[i - 1] - 1e-4F);
    CHECK(biggestJump < 0.08F); // No teleporting: the fall itself is the biggest move.
    CHECK(w.happened("plate_pressed"));
    // The door it drives opened.
    CHECK_NEAR(w.at("Door").get<Door>()->openAmount(), 1.0, 1e-6);

    // Steady state: perfectly still.
    const float standing = feet(w, "Hero");
    w.tick(120);
    CHECK_NEAR(feet(w, "Hero"), standing, 0.002);
    CHECK_NEAR(padTop(w, plate), restTop + plateDepth, 0.002);

    // Leave: the plate rises to where it started, and the door closes again.
    w.runtime->teleport(w.at("Hero"), {-6.0F, restingHeight});
    w.tick(90);
    CHECK(!state.pressed() && w.happened("plate_released"));
    CHECK_NEAR(state.pressAmount(), 0.0, 1e-4);
    CHECK_NEAR(padTop(w, plate), restTop, 0.002);
    w.tick(90);
    CHECK_NEAR(w.at("Door").get<Door>()->openAmount(), 0.0, 1e-6);
}

// Running onto and over a plate from the side: the rounded corner lets the character up, the
// plate sinks while it stands on it, and the character walks off the other side onto the floor.
void walkingOverAPlate() {
    World w;
    w.ground();
    const BuiltPlate plate = buildPlate(w, 0.0F);
    w.character("Hero", {-4.0F, restingHeight});
    w.start();
    w.tick(20);
    w.down(Key::D);
    bool wasOnPlate = false, wasPressed = false;
    float lowest = 1e9F; // Smallest feet height while over the plate (up is smaller).
    w.tick(180, [&](int i) {
        const float x = w.position("Hero").x;
        if (tracing() && i < 60)
            std::printf("walk t=%3d x=%.3f feetY=%.3f grounded=%d\n", i, static_cast<double>(x),
                        static_cast<double>(feet(w, "Hero")),
                        w.at("Hero").get<PlatformerController>()->grounded() ? 1 : 0);
        if (std::fabs(x) < 0.5F) {
            wasOnPlate = wasOnPlate || feet(w, "Hero") < floorTop - 0.04F;
            lowest = std::min(lowest, feet(w, "Hero"));
        }
        wasPressed = wasPressed || plate.root->get<PressurePlate>()->pressed();
    });
    w.up(Key::D);
    CHECK(wasOnPlate);                // It climbed onto the plate rather than being stopped by it.
    CHECK(lowest < floorTop - 0.08F); // Its feet were well above the floor on it.
    CHECK(w.position("Hero").x > 3.0F); // It kept going to the far side.
    CHECK_NEAR(feet(w, "Hero"), floorTop, 0.03);
    w.tick(60);
    CHECK(!plate.root->get<PressurePlate>()->pressed()); // It let go, and the plate rose again.
    CHECK_NEAR(padTop(w, plate), floorTop - plateRise, 0.005);
}

// A jump-and-land repeated on the plate: the plate follows the load each time, never getting stuck
// half way, and the character never loses the surface.
void landingRepeatedly() {
    World w;
    w.ground();
    const BuiltPlate plate = buildPlate(w, 0.0F);
    w.character("Hero", {0.0F, restingHeight - 0.4F});
    w.start();
    w.tick(90);
    CHECK(plate.root->get<PressurePlate>()->pressed());
    for (int hop = 0; hop < 3; ++hop) {
        w.down(Key::W);
        w.tick(4);
        w.up(Key::W);
        w.tick(120, [&](int) {
            CHECK(feet(w, "Hero") <= padTop(w, plate) + 0.06F); // Never below the top surface.
        });
        CHECK(plate.root->get<PressurePlate>()->pressed());
        CHECK(w.at("Hero").get<PlatformerController>()->grounded());
        CHECK_NEAR(feet(w, "Hero"), padTop(w, plate), 0.03);
    }
}

// What presses a plate: a crate does, a plate with a mass threshold ignores something lighter, and
// a plate filtered by tag ignores the crate. Static level geometry never presses a weight plate.
void whatPressesAPlate() {
    for (const int mode : {0, 1, 2}) {
        World w;
        w.ground();
        const BuiltPlate plate = buildPlate(w, 0.0F);
        auto &state = *plate.root->get<PressurePlate>();
        if (mode == 1)
            state.minimumMass = 5.0F; // The crate below weighs about 0.9 kg.
        if (mode == 2)
            state.activatorTags = {"hero"};
        Entity &crate = w.box("Crate", {0.0F, floorTop - 3.0F}, {0.9F, 0.9F}, layers::prop);
        crate.add<RigidBody>();
        w.start();
        w.tick(150);
        CHECK(state.pressed() == (mode == 0));
        // The crate is standing on the pad, whatever the plate decides.
        CHECK_NEAR(w.position("Crate").y + 0.45F, padTop(w, plate), 0.03);
    }
    {
        // A heavy crate (mass over the threshold) does press a plate with a mass threshold.
        World w;
        w.ground();
        const BuiltPlate plate = buildPlate(w, 0.0F);
        plate.root->get<PressurePlate>()->minimumMass = 5.0F;
        Entity &crate = w.box("Crate", {0.0F, floorTop - 3.0F}, {0.9F, 0.9F}, layers::prop);
        crate.add<RigidBody>();
        crate.get<Collider>()->density = 20.0F;
        w.start();
        w.tick(150);
        CHECK(plate.root->get<PressurePlate>()->pressed());
    }
    {
        // A character walking past a plate's side, not onto it, does not press it.
        World w;
        w.ground();
        const BuiltPlate plate = buildPlate(w, 0.0F);
        w.body("Wall", {-1.4F, floorTop - 1.0F}, {0.4F, 2.0F}, RigidBodyType::Static);
        w.character("Hero", {-3.0F, restingHeight});
        w.start();
        w.down(Key::D);
        w.tick(120);
        CHECK(!plate.root->get<PressurePlate>()->pressed());
    }
}

// A crate is pushed onto a plate, the way the practice room's puzzle wants: the chamfered edge lets
// it slide up, it stays on the pad, and its weight presses the plate.
void crateIsPushedOntoAPlate() {
    World w;
    w.ground();
    const BuiltPlate plate = buildPlate(w, 4.0F);
    Entity &crate = w.box("Crate", {1.5F, floorTop - 0.48F}, {0.96F, 0.96F}, layers::prop);
    crate.add<RigidBody>().fixedRotation = true;
    crate.get<Collider>()->friction = 0.9F;
    crate.get<Collider>()->density = 0.8F;
    w.character("Hero", {-1.0F, restingHeight}).get<PlatformerController>()->moveSpeed =
        2.0F; // Pushing, not running.
    w.start();
    w.tick(20);
    w.down(Key::D);
    bool arrived = false;
    for (int i = 0; i < 420 && !arrived; ++i) {
        w.tick();
        arrived = w.position("Crate").x >= 3.9F;
        if (tracing() && i > 100 && i % 3 == 0)
            std::printf("push t=%3d hero=%.3f crate=(%.3f,%.3f) padTop=%.3f pressed=%d\n", i,
                        static_cast<double>(w.position("Hero").x),
                        static_cast<double>(w.position("Crate").x),
                        static_cast<double>(w.position("Crate").y),
                        static_cast<double>(padTop(w, plate)),
                        plate.root->get<PressurePlate>()->pressed() ? 1 : 0);
    }
    w.up(Key::D);
    CHECK(arrived);
    w.tick(120);
    CHECK_NEAR(w.position("Crate").x, 4.0F, 0.5); // It stopped on the plate, not beyond it.
    CHECK(plate.root->get<PressurePlate>()->pressed());
    CHECK_NEAR(w.position("Crate").y + 0.48F, padTop(w, plate), 0.03);
}

// Two crates stacked on a plate: the stack stays put, the plate takes the weight and is pressed,
// and nothing jitters once it has settled.
void stackOnAPlate() {
    World w;
    w.ground();
    const BuiltPlate plate = buildPlate(w, 0.0F);
    Entity &lower = w.box("Lower", {0.0F, floorTop - 2.0F}, {0.9F, 0.9F}, layers::prop);
    lower.add<RigidBody>();
    Entity &upper = w.box("Upper", {0.0F, floorTop - 3.5F}, {0.9F, 0.9F}, layers::prop);
    upper.add<RigidBody>();
    w.start();
    w.tick(240);
    CHECK(plate.root->get<PressurePlate>()->pressed());
    CHECK_NEAR(w.position("Lower").y + 0.45F, padTop(w, plate), 0.03);
    CHECK_NEAR(w.position("Upper").y + 0.45F, w.position("Lower").y - 0.45F, 0.03);
    const Vec2 lowerAt = w.position("Lower"), upperAt = w.position("Upper");
    float drift = 0.0F;
    w.tick(120, [&](int) {
        drift = std::max({drift, std::fabs(w.position("Lower").y - lowerAt.y),
                          std::fabs(w.position("Upper").y - upperAt.y),
                          std::fabs(w.position("Upper").x - upperAt.x)});
    });
    CHECK(drift < 0.003F);
}

// Elevators: a platform rising, and then lowering, keeps its rider on it the whole way, standing.
void elevatorsCarryUpAndDown() {
    World w;
    w.ground();
    Entity &lift = w.body("Lift", {0.0F, floorTop - 1.0F}, {3.0F, 0.4F}, RigidBodyType::Kinematic);
    auto &platform = lift.add<MovingPlatform>();
    platform.travel = {0.0F, -6.0F};
    platform.speed = 3.0F;
    platform.pause = 0.5F;
    w.character("Rider", {0.0F, floorTop - 1.0F - 0.2F - 0.475F});
    w.start();
    w.tick(20);
    float worstGap = 0.0F;
    bool grounded = true;
    float high = 1e9F, low = -1e9F;
    // A whole cycle: up 2 s, wait, down 2 s, wait.
    w.tick(330, [&](int) {
        const float gap = std::fabs((feet(w, "Rider")) - (w.position("Lift").y - 0.2F));
        worstGap = std::max(worstGap, gap);
        grounded = grounded && w.at("Rider").get<PlatformerController>()->grounded();
        high = std::min(high, feet(w, "Rider"));
        low = std::max(low, feet(w, "Rider"));
    });
    CHECK(high < floorTop - 5.0F); // It went up with the lift...
    CHECK(low > floorTop - 2.0F);  // ...and came back down.
    CHECK(worstGap < 0.06F);       // Feet on the lift's surface throughout.
    CHECK(grounded);
}

// A platform that starts downward with someone standing on it (a lever starts it): the rider is
// carried, not left floating, however fast it goes.
void platformsGoingDown() {
    World w;
    w.ground();
    Entity &lift = w.body("Lift", {0.0F, floorTop - 7.0F}, {3.0F, 0.4F}, RigidBodyType::Kinematic);
    auto &platform = lift.add<MovingPlatform>();
    platform.travel = {0.0F, 5.0F};
    platform.speed = 4.0F; // Faster than the character falls when it just steps off.
    platform.pause = 5.0F;
    platform.requireSignal = true;
    Entity &lever = w.box("Switch", {8.0F, floorTop - 0.4F}, {0.5F, 0.8F}, layers::sensor);
    lever.get<Collider>()->isTrigger = true;
    lever.add<Lever>().targets = {lift.id()};
    Entity &prop = w.box("Toucher", {14.0F, floorTop - 1.0F}, {0.4F, 0.4F}, layers::prop);
    prop.add<RigidBody>();
    w.character("Rider", {0.0F, floorTop - 7.0F - 0.2F - 0.475F});
    // A crate rides too: props are not steered by a controller, they have to stay on by contact.
    Entity &crate =
        w.box("Crate", {0.8F, floorTop - 7.0F - 0.2F - 0.4F}, {0.8F, 0.8F}, layers::prop);
    crate.add<RigidBody>();
    w.start();
    w.tick(60); // Settled on a platform that is not moving yet.
    CHECK(w.at("Rider").get<PlatformerController>()->grounded());
    const float startY = w.position("Lift").y;
    w.runtime->teleport(w.at("Toucher"), {8.0F, floorTop - 0.6F}); // Flips the switch.
    float worstGap = 0.0F, crateGap = 0.0F;
    bool grounded = true;
    w.tick(120, [&](int i) {
        const float surface = w.position("Lift").y - 0.2F;
        if (tracing() && i < 40)
            std::printf("down t=%2d feet=%.3f lift=%.3f crate=%.3f grounded=%d\n", i,
                        static_cast<double>(feet(w, "Rider")), static_cast<double>(surface),
                        static_cast<double>(w.position("Crate").y + 0.4F),
                        w.at("Rider").get<PlatformerController>()->grounded() ? 1 : 0);
        worstGap = std::max(worstGap, std::fabs(feet(w, "Rider") - surface));
        crateGap = std::max(crateGap, std::fabs(w.position("Crate").y + 0.4F - surface));
        grounded = grounded && w.at("Rider").get<PlatformerController>()->grounded();
    });
    CHECK(w.position("Lift").y > startY + 4.0F); // It went a long way down.
    CHECK(worstGap < 0.06F);
    CHECK(crateGap < 0.06F);
    CHECK(grounded);
}

// A rotating platform carries a character that stands away from its center: after several seconds
// the character is still where a point painted on the platform would be.
void rotatingPlatformsCarryRiders() {
    World w;
    w.ground(-40.0F, 40.0F);
    // A turntable a few meters over the floor; the rider starts on its right half.
    Entity &turntable =
        w.body("Turntable", {0.0F, floorTop - 4.0F}, {8.0F, 0.4F}, RigidBodyType::Kinematic);
    auto &spin = turntable.add<MovingPlatform>();
    spin.travel = {0.0F, 0.0F};
    spin.spinSpeed = 10.0F; // Degrees per second, clockwise on screen.
    Entity &rider = w.character("Rider", {2.0F, floorTop - 4.0F - 0.2F - 0.475F});
    (void)rider;
    w.start();
    w.tick(10);
    const Vec2 center = w.position("Turntable");
    const Vec2 startOffset = w.position("Rider") - center;
    float worstError = 0.0F;
    bool grounded = true;
    w.tick(150, [&](int) { // 2.5 s: 25 degrees.
        const float turned = degreesToRadians(w.at("Turntable").worldTransform().rotationDegrees);
        const Vec2 expected = center + rotated(startOffset, turned);
        worstError = std::max(worstError, distance(w.position("Rider"), expected));
        grounded = grounded && w.at("Rider").get<PlatformerController>()->grounded();
    });
    CHECK(std::fabs(w.at("Turntable").worldTransform().rotationDegrees) > 20.0F);
    // The tilt makes the capsule sit a little differently on the surface, but it stays where the
    // platform carries it and it stays standing.
    CHECK(worstError < 0.35F);
    CHECK(grounded);
}

// Angled surfaces: a plank tilted 25 degrees is walkable up and standable, a steeper one (over the
// controller's slope limit) is not, and none of them lets the character fall through.
void angledSurfaces() {
    {
        World w;
        w.ground();
        Entity &ramp = w.box("Ramp", {4.0F, floorTop - 1.0F}, {6.0F, 0.3F});
        ramp.transform().rotationDegrees = -25.0F; // Rises to the right.
        w.character("Hero", {-2.0F, restingHeight});
        w.start();
        w.tick(20);
        w.down(Key::D);
        w.tick(70);
        w.up(Key::D);
        w.tick(30);
        // Ended up on the ramp, standing on its surface line.
        const Vec2 at = w.position("Hero");
        CHECK(at.x > 4.0F && at.y < floorTop - 1.4F);
        const Vec2 rampAt = w.position("Ramp");
        const float radians = degreesToRadians(-25.0F);
        const Vec2 normalUp = rotated({0.0F, -1.0F}, radians);
        const float distanceToSurface = dot(at - rampAt, normalUp) - 0.15F; // Above the top face.
        CHECK(distanceToSurface > 0.3F && distanceToSurface < 0.7F); // Capsule resting on it.
        CHECK(w.at("Hero").get<PlatformerController>()->grounded());
        // Standing still on the slope: it holds.
        const Vec2 held = w.position("Hero");
        w.tick(60);
        CHECK(distance(w.position("Hero"), held) < 0.02F);
    }
    {
        // A lever-sized platform tipped over to 30 degrees, dropped on from above, still supports.
        World w;
        w.ground();
        Entity &plank = w.box("Plank", {0.0F, floorTop - 2.0F}, {2.0F, 0.3F});
        plank.transform().rotationDegrees = 30.0F;
        w.character("Hero", {0.2F, floorTop - 5.0F});
        w.start();
        w.tick(120);
        CHECK(w.position("Hero").y < floorTop - 1.0F); // Not on the floor: held by the plank.
    }
}

// A plank on a hinge: a seesaw. Standing on one end tips it, the character stays on the plank
// while it tilts, and a spring brings it back level once the weight is gone.
void hingedSeesaw() {
    World w;
    w.ground();
    Entity &plank = w.body("Plank", {0.0F, floorTop - 1.2F}, {6.0F, 0.3F}, RigidBodyType::Dynamic);
    plank.get<Collider>()->friction = 1.0F;
    auto &hinge = plank.add<HingeJoint>();
    hinge.limits = true;
    hinge.lowerAngle = -25.0F;
    hinge.upperAngle = 25.0F;
    hinge.spring = true;
    hinge.springHertz = 0.5F;
    hinge.springDamping = 1.0F;
    w.character("Hero", {2.4F, floorTop - 1.2F - 0.15F - 0.475F - 0.02F});
    w.start();
    w.tick(90);
    // The right end went down (positive angle is clockwise on screen: right end down).
    const float tilted = w.at("Plank").worldTransform().rotationDegrees;
    CHECK(tilted > 3.0F);
    CHECK(w.at("Hero").get<PlatformerController>()->grounded());
    // The character stands on the plank's surface, wherever it has tilted to.
    const Vec2 plankAt = w.position("Plank");
    const float radians = degreesToRadians(tilted);
    const Vec2 normalUp = rotated({0.0F, -1.0F}, radians);
    const float above = dot(w.position("Hero") - plankAt, normalUp) - 0.15F;
    CHECK(above > 0.3F && above < 0.7F);
    // Step off: it swings back to level.
    w.runtime->teleport(w.at("Hero"), {-9.0F, restingHeight});
    w.tick(240);
    CHECK_NEAR(w.at("Plank").worldTransform().rotationDegrees, 0.0, 1.5);
    // The plank is still pinned where it was.
    CHECK_NEAR(w.position("Plank").x, 0.0, 0.05);
    CHECK_NEAR(w.position("Plank").y, floorTop - 1.2F, 0.05);
}

// A hinged door swings open about its edge and closes again, carrying nothing through the floor.
void hingedDoor() {
    World w;
    w.ground();
    // The door's origin is its hinge; the leaf hangs to the right of it.
    Entity &leaf = w.body("Leaf", {2.0F, floorTop - 0.2F}, {0.3F, 3.6F}, RigidBodyType::Kinematic);
    leaf.get<Collider>()->offset = {0.0F, -1.8F};
    auto &door = leaf.add<Door>();
    door.openOffset = {0.0F, 0.0F};
    door.openRotation = -90.0F; // Swings up and away, counter-clockwise on screen.
    door.rotationSpeed = 90.0F;
    Entity &lever = w.box("Switch", {-3.0F, floorTop - 0.4F}, {0.5F, 0.8F}, layers::sensor);
    lever.get<Collider>()->isTrigger = true;
    lever.add<Lever>().targets = {leaf.id()};
    w.character("Hero", {-7.0F, restingHeight});
    w.start();
    w.tick(30);
    CHECK_NEAR(w.at("Leaf").worldTransform().rotationDegrees, 0.0, 0.5); // Shut until signalled.
    w.runtime->teleport(w.at("Hero"), {-3.0F, restingHeight});
    w.tick(60);
    CHECK(w.at("Switch").get<Lever>()->on()); // The hero stood in it.
    w.tick(90);
    CHECK_NEAR(w.at("Leaf").worldTransform().rotationDegrees, -90.0, 1.0);
    CHECK_NEAR(w.position("Leaf").x, 2.0, 0.02); // It turned about its hinge, not away from it.
    CHECK(w.at("Leaf").get<Door>()->openAmount() > 0.99F);
}

// Props are physical objects like everything else: a stack of crates stands where it was built,
// and a crate dropped on a character does not push it through the floor.
void propsAreSolidObjects() {
    {
        World w;
        w.ground();
        Entity &crate = w.box("Crate", {2.5F, floorTop - 0.5F}, {1.0F, 1.0F}, layers::prop);
        crate.add<RigidBody>();
        Entity &second = w.box("Second", {2.5F, floorTop - 1.6F}, {1.0F, 1.0F}, layers::prop);
        second.add<RigidBody>();
        w.start();
        w.tick(120);
        CHECK_NEAR(w.position("Crate").y, floorTop - 0.5F, 0.02);
        CHECK_NEAR(w.position("Second").y, floorTop - 1.5F, 0.03);
        CHECK_NEAR(w.position("Second").x, 2.5F, 0.02);
        const Vec2 held = w.position("Second");
        w.tick(120);
        CHECK(distance(w.position("Second"), held) < 0.003F);
    }
    {
        World w;
        w.ground();
        w.character("Hero", {0.0F, restingHeight});
        Entity &falling = w.box("Falling", {0.0F, floorTop - 9.0F}, {0.8F, 0.8F}, layers::prop);
        falling.add<RigidBody>();
        w.start();
        w.tick(240);
        CHECK_NEAR(feet(w, "Hero"), floorTop, 0.03); // Still standing on the floor.
        CHECK(w.at("Hero").get<Killable>()->alive());
        CHECK(w.position("Falling").y + 0.4F <= floorTop + 0.03F); // The crate did not sink.
    }
}

// Fast things do not tunnel: a character falling from a great height and a crate fired at a wall.
void fastThingsDoNotTunnel() {
    {
        World w;
        w.box("Thin Floor", {0.0F, floorTop + 0.1F}, {40.0F, 0.2F});
        w.character("Hero", {0.0F, floorTop - 60.0F});
        w.start();
        float lowest = -1e9F;
        w.tick(400, [&](int) { lowest = std::max(lowest, feet(w, "Hero")); });
        CHECK(lowest < floorTop + 0.05F); // Never below the floor's top.
        CHECK(w.at("Hero").get<PlatformerController>()->grounded());
    }
    {
        World w;
        w.ground();
        w.body("Wall", {6.0F, floorTop - 3.0F}, {0.2F, 6.0F}, RigidBodyType::Static);
        Entity &bullet = w.box("Bullet", {0.0F, floorTop - 3.0F}, {0.4F, 0.4F}, layers::prop);
        auto &body = bullet.add<RigidBody>();
        body.gravityScale = 0.0F;
        body.bullet = true;
        w.start();
        w.runtime->physics().setVelocity(*w.runtime->bodyOf(bullet.id()), {90.0F, 0.0F});
        float furthest = -1e9F;
        w.tick(60, [&](int) { furthest = std::max(furthest, w.position("Bullet").x); });
        CHECK(furthest < 6.0F); // Stopped by the wall.
    }
}

// Collision callbacks: a component hears when a solid contact starts and when it ends.
void collisionEnterAndExit() {
    ProbeWorld w;
    w.ground();
    Entity &hero = w.character("Hero", {0.0F, floorTop - 3.0F});
    hero.addComponent("Probe");
    w.start();
    w.tick(120);
    const auto *probe = w.at("Hero").get<Probe>();
    CHECK(probe->entered >= 1); // Landed on the floor.
    const int before = probe->exited;
    w.down(Key::W);
    w.tick(4);
    w.up(Key::W);
    w.tick(20); // In the air: the contact with the floor ended.
    CHECK(probe->exited > before);
    w.tick(120);
    CHECK(probe->entered >= 2); // And a new contact on landing.
}

// Speeds and frame rates: the same play is the same result however the frames are cut.
void frameRateDoesNotChangeTheResult() {
    const auto run = [](double frameSeconds) {
        World w;
        w.ground();
        const BuiltPlate plate = buildPlate(w, 0.0F);
        w.character("Hero", {-1.5F, restingHeight});
        w.start();
        w.down(Key::D);
        double elapsed = 0.0;
        while (elapsed < 2.0) {
            w.runtime->update(frameSeconds, w.keyboard);
            w.keyboard.beginFrame();
            elapsed += frameSeconds;
        }
        (void)plate;
        return std::pair{w.position("Hero"), padTop(w, plate)};
    };
    const auto steady = run(1.0 / 60.0);
    const auto fast = run(1.0 / 144.0);
    const auto slow = run(1.0 / 20.0);
    CHECK_NEAR(steady.first.x, fast.first.x, 0.25);
    CHECK_NEAR(steady.first.x, slow.first.x, 0.35);
    CHECK_NEAR(steady.first.y, fast.first.y, 0.05);
    CHECK_NEAR(steady.second, fast.second, 0.03);
}
} // namespace

int main() {
    playerStandsOnAPlateThatSinks();
    walkingOverAPlate();
    landingRepeatedly();
    whatPressesAPlate();
    crateIsPushedOntoAPlate();
    stackOnAPlate();
    elevatorsCarryUpAndDown();
    platformsGoingDown();
    rotatingPlatformsCarryRiders();
    angledSurfaces();
    hingedSeesaw();
    hingedDoor();
    propsAreSolidObjects();
    fastThingsDoNotTunnel();
    collisionEnterAndExit();
    frameRateDoesNotChangeTheResult();
    return yk::test::finish("mechanisms");
}
