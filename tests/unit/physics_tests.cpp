#include "yk/physics/World.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
using namespace yk;
using namespace yk::physics;
int failures{};
int checks{};
void check(bool condition, const char *description) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}
template <class T> T take(Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error());
    return std::move(result.value());
}
void ok(Status status) {
    if (!status)
        throw std::runtime_error(status.error());
}
bool near(float first, float second, float tolerance = 0.001F) {
    return std::abs(first - second) <= tolerance;
}
std::unique_ptr<World> world(Vec2 gravity = {0, 9.81F}) {
    WorldConfig config;
    config.gravity = gravity;
    return take(World::create(config));
}
BodyHandle body(World &simulation, Vec2 position, BodyType type = BodyType::Dynamic) {
    BodyDef definition;
    definition.type = type;
    definition.pose.position = position;
    return take(simulation.createBody(definition));
}
void ticks(World &simulation, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        const auto result = take(simulation.advance(1.0 / 60));
        if (result.steps != 1)
            throw std::runtime_error("Expected one fixed step");
    }
}
void timingAndIntegration() {
    auto simulation = world();
    const auto moving = body(*simulation, {});
    take(simulation->createShape(moving, Circle{}));
    const auto ground = body(*simulation, {30, 40}, BodyType::Static);
    take(simulation->createShape(ground, Box{}));
    ticks(*simulation, 60);
    const auto value = take(simulation->state(moving));
    check(near(value.linearVelocity.y, 9.81F, 0.01F), "gravity integrates velocity in seconds");
    check(near(value.pose.position.y, 4.905F, 0.06F), "free fall uses meter scale");
    check(take(simulation->state(ground)).pose.position.x == 30, "static body does not move");
    check(near(value.mass, std::numbers::pi_v<float> * 0.25F), "shape density defines mass");

    auto first = world({});
    auto second = world({});
    BodyDef definition;
    definition.linearVelocity = {2, 0};
    const auto a = take(first->createBody(definition));
    const auto b = take(second->createBody(definition));
    take(first->createShape(a, Box{}));
    take(second->createShape(b, Box{}));
    ticks(*first, 60);
    for (unsigned i = 0; i < 120; ++i)
        take(second->advance(1.0 / 120));
    check(near(take(first->state(a)).pose.position.x, take(second->state(b)).pose.position.x,
               0.000001F),
          "different render frame partitions produce identical fixed simulation");
    check(first->stats().ticks == second->stats().ticks, "partitioning preserves tick count");
    take(second->advance(1.0 / 120));
    const auto interpolated = take(second->interpolatedPose(b));
    check(near(interpolated.position.x, 2.0F - 1.0F / 60),
          "render interpolation blends previous/current physics poses");
    ok(second->setPose(b, {{7, 8}, 0}));
    check(near(take(second->interpolatedPose(b)).position.x, 7),
          "teleport resets interpolation history");
    const auto boundedFrame = take(first->advance(10));
    check(boundedFrame.steps == 8 && boundedFrame.droppedSeconds > 9.8,
          "long frame is clamped and catch-up budget reports dropped time");
    check(boundedFrame.interpolationAlpha >= 0 && boundedFrame.interpolationAlpha < 1,
          "backlog drop leaves a fractional tick only");
    const auto before = first->stats().ticks;
    check(!first->advance(-1) && !first->advance(std::numeric_limits<double>::infinity()) &&
              !first->advance(std::numeric_limits<double>::quiet_NaN()),
          "invalid elapsed times are rejected");
    check(first->stats().ticks == before, "invalid time does not mutate simulation");
    auto fractional = world({});
    const auto almostTick = take(fractional->advance(1.0 / 60 - 1e-10));
    check(almostTick.steps == 0 && almostTick.interpolationAlpha < 1,
          "float conversion cannot round a fractional interpolation alpha to one");

    auto kinematicWorld = world();
    BodyDef kinematic;
    kinematic.type = BodyType::Kinematic;
    kinematic.linearVelocity = {3, 0};
    const auto platform = take(kinematicWorld->createBody(kinematic));
    take(kinematicWorld->createShape(platform, Box{}));
    ticks(*kinematicWorld, 60);
    const auto platformState = take(kinematicWorld->state(platform));
    check(near(platformState.pose.position.x, 3) && platformState.pose.position.y == 0,
          "kinematic body follows velocity without gravity");
    check(!kinematicWorld->applyForce(platform, {1, 0}), "forces reject nondynamic bodies");
    ok(kinematicWorld->setEnabled(platform, false));
    ticks(*kinematicWorld, 60);
    check(near(take(kinematicWorld->state(platform)).pose.position.x, 3),
          "disabled body stops simulating");
    ok(kinematicWorld->setEnabled(platform, true));
    ok(kinematicWorld->setVelocity(platform, {3, 0}));
    ticks(*kinematicWorld, 60);
    check(near(take(kinematicWorld->state(platform)).pose.position.x, 6),
          "reenabled body resumes simulation with new velocity");
}
void contactsAndMaterials() {
    auto simulation = world();
    const auto ground = body(*simulation, {0, 5}, BodyType::Static);
    take(simulation->createShape(ground, Box{{10, 0.5F}}));
    const auto moving = body(*simulation, {});
    const auto shape = take(simulation->createShape(moving, Circle{}));
    bool began{}, hit{};
    for (unsigned i = 0; i < 240; ++i) {
        ticks(*simulation, 1);
        for (const auto &event : simulation->events()) {
            began |= event.type == EventType::ContactBegin;
            hit |= event.type == EventType::ContactHit;
            check(simulation->valid(event.first) && simulation->valid(event.second),
                  "contact event carries engine shape handles");
        }
    }
    const auto rested = take(simulation->state(moving));
    check(began && hit, "falling body produces contact and impact events");
    check(near(rested.pose.position.y, 4, 0.03F) && std::abs(rested.linearVelocity.y) < 0.01F,
          "circle settles on static ground");
    check(!rested.awake, "resting body sleeps");
    const auto contacts = take(simulation->contacts(moving));
    check(!contacts.empty() && !contacts.front().points.empty(),
          "persistent contact manifold is queryable");
    check(!simulation->sensorOverlaps(shape), "overlap query rejects a solid shape");
    ok(simulation->applyImpulse(moving, {0, -rested.mass * 3}));
    check(take(simulation->state(moving)).awake &&
              near(take(simulation->state(moving)).linearVelocity.y, -3),
          "impulse wakes sleeping body and respects mass");
    ticks(*simulation, 10);
    bool ended{};
    // Contact end can arrive during any of these steps, so force a known departure below.
    ok(simulation->setPose(moving, {{0, 4}, 0}));
    ok(simulation->setVelocity(moving, {}));
    ticks(*simulation, 10);
    ok(simulation->setPose(moving, {{0, -10}, 0}));
    ticks(*simulation, 1);
    for (const auto &event : simulation->events())
        ended |= event.type == EventType::ContactEnd;
    check(ended, "teleport away generates contact end");

    auto frictionWorld = world();
    const auto floor = body(*frictionWorld, {0, 2}, BodyType::Static);
    ShapeDef rough;
    rough.friction = 1;
    take(frictionWorld->createShape(floor, Box{{20, 0.5F}}, rough));
    BodyDef slider;
    slider.pose.position = {0, 1};
    slider.linearVelocity = {4, 0};
    slider.fixedRotation = true;
    const auto box = take(frictionWorld->createBody(slider));
    take(frictionWorld->createShape(box, Box{}, rough));
    ticks(*frictionWorld, 120);
    check(std::abs(take(frictionWorld->state(box)).linearVelocity.x) < 0.05F,
          "surface friction stops a sliding box");

    auto bounceWorld = world();
    const auto bounceFloor = body(*bounceWorld, {0, 3}, BodyType::Static);
    take(bounceWorld->createShape(bounceFloor, Box{{10, 0.5F}}));
    const auto ball = body(*bounceWorld, {});
    ShapeDef elastic;
    elastic.restitution = 1;
    take(bounceWorld->createShape(ball, Circle{}, elastic));
    bool bounced{};
    for (unsigned i = 0; i < 100; ++i) {
        ticks(*bounceWorld, 1);
        bounced |= take(bounceWorld->state(ball)).linearVelocity.y < -2;
    }
    check(bounced, "restitution produces a measurable bounce");
}
void sensorsAndFilters() {
    auto simulation = world({});
    const auto region = body(*simulation, {}, BodyType::Static);
    ShapeDef trigger;
    trigger.sensor = true;
    trigger.density = 0;
    const auto sensor = take(simulation->createShape(region, Box{{1, 1}}, trigger));
    const auto sensorRay = take(simulation->rayCast({-3, 0}, {6, 0}));
    check(sensorRay && sensorRay->shape == sensor, "ray queries can hit sensor geometry");
    BodyDef traveler;
    traveler.pose.position = {-3, 0};
    traveler.linearVelocity = {4, 0};
    const auto moving = take(simulation->createBody(traveler));
    const auto visitor = take(simulation->createShape(moving, Circle{}));
    bool began{}, ended{}, overlap{};
    for (unsigned i = 0; i < 120; ++i) {
        ticks(*simulation, 1);
        for (const auto &event : simulation->events()) {
            if (event.type == EventType::SensorBegin) {
                began = true;
                check(event.first == sensor && event.second == visitor,
                      "sensor event identifies sensor first");
            }
            ended |= event.type == EventType::SensorEnd;
        }
        overlap |= !take(simulation->sensorOverlaps(sensor)).empty();
    }
    check(began && ended && overlap, "sensor enter/exit and current overlap agree");
    check(near(take(simulation->state(moving)).pose.position.x, 5, 0.01F),
          "sensor never physically blocks visitor");

    ok(simulation->setPose(moving, {{0, 0}, 0}));
    ok(simulation->setVelocity(moving, {}));
    ticks(*simulation, 1);
    check(!take(simulation->sensorOverlaps(sensor)).empty(), "visitor overlaps before destruction");
    ok(simulation->setEnabled(moving, false));
    check(take(simulation->sensorOverlaps(sensor)).empty(),
          "disabled visitor is immediately excluded from current sensor overlaps");
    ok(simulation->setEnabled(moving, true));
    ok(simulation->setEnabled(region, false));
    check(take(simulation->sensorOverlaps(sensor)).empty(),
          "disabled sensor immediately reports no current overlaps");
    ok(simulation->setEnabled(region, true));
    ok(simulation->destroy(moving));
    check(take(simulation->sensorOverlaps(sensor)).empty(),
          "destroyed visitor is immediately excluded from current sensor overlaps");
    const auto replacement = body(*simulation, {0, 0});
    const auto replacementShape = take(simulation->createShape(replacement, Circle{}));
    ticks(*simulation, 1);
    bool staleEnd{};
    for (const auto &event : simulation->events()) {
        if (event.type == EventType::SensorEnd && event.second == visitor) {
            staleEnd = true;
            check(!simulation->valid(event.second),
                  "destroyed visitor end event remains safely invalid");
        }
    }
    check(staleEnd && simulation->valid(replacementShape),
          "end event preserves destroyed identity across native slot reuse");
    ok(simulation->setFilter(replacementShape, {2, 0, 0}));
    ticks(*simulation, 2);
    check(take(simulation->sensorOverlaps(sensor)).empty(),
          "collision masks filter sensor overlaps");
    ok(simulation->setFilter(replacementShape, {2, UINT64_MAX, -3}));
    ok(simulation->setFilter(sensor, {1, UINT64_MAX, -3}));
    ticks(*simulation, 2);
    check(take(simulation->sensorOverlaps(sensor)).empty(),
          "equal negative groups exclude overlaps");

    auto filtered = world();
    const auto floor = body(*filtered, {0, 2}, BodyType::Static);
    take(filtered->createShape(floor, Box{{10, 0.5F}}));
    const auto falling = body(*filtered, {});
    ShapeDef excluded;
    excluded.filter.maskBits = 0;
    take(filtered->createShape(falling, Circle{}, excluded));
    ticks(*filtered, 60);
    check(take(filtered->state(falling)).pose.position.y > 4,
          "collision mask disables solid response");
}
void queriesAndGeometry() {
    auto simulation = world({});
    const auto base = body(*simulation, {2, 0}, BodyType::Static);
    const auto box = take(simulation->createShape(base, Box{{1, 1}}));
    const auto hit = take(simulation->rayCast({-3, 0}, {10, 0}));
    check(hit && hit->shape == box && near(hit->point.x, 1) && near(hit->fraction, 0.4F),
          "ray cast returns closest shape, world point and fraction");
    check(!take(simulation->rayCast({-3, 10}, {10, 0})), "ray miss returns empty optional");
    check(!take(simulation->rayCast({}, {})), "zero-length ray is empty");
    check(!take(simulation->rayCast({-3, 0}, {10, 0}, {1, 0})), "ray respects query mask");
    check(take(simulation->queryPoint({2, 0})).size() == 1,
          "exact point query finds solid interior");
    check(take(simulation->queryPoint({4, 0})).empty(), "point outside shape is excluded");
    check(take(simulation->queryAabb({{0, -2}, {4, 4}})).size() == 1, "AABB query finds candidate");
    check(take(simulation->queryAabb({{3.02F, -1}, {0.02F, 2}})).empty(),
          "AABB query excludes broad-phase padding");
    check(!simulation->queryAabb({{}, {-1, 1}}), "negative query dimensions rejected");
    check(take(simulation->bodyOf(box)) == base, "shape resolves its owning body");
    take(simulation->createShape(base, Circle{0.4F, {4, 0}}));
    take(simulation->createShape(base, Capsule{{6, -1}, {6, 1}, 0.2F}));
    take(simulation->createShape(base, Segment{{-2, 4}, {2, 4}}));
    take(simulation->createShape(base, Polygon{{{-1, 6}, {0, 5}, {1, 6}}}));
    check(simulation->stats().shapes == 5,
          "box, circle, capsule, segment and convex polygon are supported");
    const auto lines = take(simulation->debugLines());
    check(lines.size() == 58, "debug snapshot contains actual outlines for all shape types");
    check(near(lines[0].first.x, 1) && near(lines[0].first.y, -1),
          "debug geometry includes body transform");
    check(!simulation->debugLines(2), "invalid debug tessellation rejected");
    check(!simulation->createShape(base, Circle{-1}), "negative radius rejected");
    check(!simulation->createShape(base, Box{{0, 1}}), "zero extent rejected");
    check(!simulation->createShape(base, Capsule{{}, {}, 1}), "degenerate capsule rejected");
    check(!simulation->createShape(base, Polygon{{{0, 0}, {1, 0}, {2, 0}}}),
          "collinear polygon rejected");
    check(!simulation->createShape(base, Polygon{{{0, 0}, {2, 0}, {2, 2}, {1, 1}, {0, 2}}}),
          "interior polygon vertices are rejected instead of silently changing geometry");
    check(!simulation->createShape(base, Segment{{}, {}}), "degenerate segment rejected");
    const auto dynamic = body(*simulation, {20, 0});
    check(!simulation->createShape(dynamic, Segment{{0, 0}, {1, 0}}),
          "dynamic zero-area segment rejected");
}
void lifetimeAndValidation() {
    auto first = world({});
    auto second = world({});
    const auto owner = body(*first, {});
    const auto shape = take(first->createShape(owner, Box{}));
    check(!first->valid(BodyHandle{}) && !first->valid(ShapeHandle{}) &&
              !first->valid(JointHandle{}),
          "default handles are invalid");
    check(!second->valid(owner) && !second->destroy(owner) && !second->createShape(owner, Circle{}),
          "foreign handles cannot access another world");
    check(!first->state({}) && !first->setPose({}, {}) && !first->destroy(ShapeHandle{}),
          "invalid handles return errors");
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    BodyDef invalid;
    invalid.pose.position.x = nan;
    check(!first->createBody(invalid), "nonfinite body position rejected");
    invalid = {};
    invalid.linearDamping = -1;
    check(!first->createBody(invalid), "negative damping rejected");
    ShapeDef material;
    material.restitution = 2;
    check(!first->createShape(owner, Circle{}, material), "out-of-range restitution rejected");
    check(!first->setPose(owner, {{nan, 0}, 0}) && !first->setVelocity(owner, {nan, 0}) &&
              !first->applyForce(owner, {nan, 0}) && !first->setGravity({nan, 0}),
          "nonfinite mutations rejected");
    check(!first->setFilter(shape, {1, 1, 40000}), "invalid collision group rejected");
    ok(first->destroy(owner));
    check(!first->valid(owner) && !first->valid(shape) && !first->destroy(owner),
          "body destruction invalidates all attached shapes and repeated destruction is safe");
    const auto replacement = body(*first, {});
    check(first->valid(replacement) && !first->valid(owner) && replacement != owner,
          "recycled native body slot cannot resurrect engine handle");
    first.reset();
    auto recreated = world({});
    const auto recreatedBody = body(*recreated, {});
    check(!recreated->valid(replacement) && recreated->valid(recreatedBody),
          "world recreation invalidates previous lifetime");
    WorldConfig config;
    config.fixedSeconds = 0;
    check(!World::create(config), "zero fixed step rejected");
    config = {};
    config.subSteps = 0;
    check(!World::create(config), "invalid solver substeps rejected");
    config = {};
    config.maxStepsPerAdvance = 0;
    check(!World::create(config), "zero catch-up budget rejected");

    auto forces = world({});
    const auto object = body(*forces, {});
    take(forces->createShape(object, Box{}));
    ok(forces->applyForce(object, {60, 0}));
    ticks(*forces, 1);
    check(near(take(forces->state(object)).linearVelocity.x, 1),
          "force integrates using body mass and fixed timestep");
    ticks(*forces, 1);
    check(near(take(forces->state(object)).linearVelocity.x, 1),
          "force is cleared after one solver tick");
    ok(forces->applyImpulse(object, {0, 1}, Vec2{1, 0}));
    const float angularBefore = take(forces->state(object)).angularVelocity;
    check(angularBefore > 0, "off-center impulse produces rotation");
    ok(forces->applyTorque(object, 1));
    ticks(*forces, 1);
    check(take(forces->state(object)).angularVelocity > angularBefore,
          "torque accelerates rotation");
}
void jointsAndFastBodies() {
    auto simulation = world();
    const auto anchor = body(*simulation, {}, BodyType::Static);
    const auto bob = body(*simulation, {0, 2});
    take(simulation->createShape(bob, Circle{}));
    DistanceJointDef tether;
    tether.first = anchor;
    tether.second = bob;
    tether.length = 2;
    const auto joint = take(simulation->createDistanceJoint(tether));
    ticks(*simulation, 180);
    const auto position = take(simulation->state(bob)).pose.position;
    check(near(std::hypot(position.x, position.y), 2, 0.02F),
          "distance joint maintains anchor separation under gravity");
    check(simulation->valid(joint) && simulation->stats().joints == 1,
          "joint lifetime and count exposed");
    ok(simulation->destroy(anchor));
    check(!simulation->valid(joint) && simulation->valid(bob) && simulation->stats().joints == 0,
          "destroying an attached body invalidates joint while preserving other body");
    check(!simulation->destroy(joint), "destroyed joint cannot be destroyed twice");
    const auto springAnchor = body(*simulation, {}, BodyType::Static);
    const auto springBob = body(*simulation, {0, 4});
    take(simulation->createShape(springBob, Circle{}));
    DistanceJointDef spring;
    spring.first = springAnchor;
    spring.second = springBob;
    spring.length = 2;
    spring.springHertz = 2;
    spring.dampingRatio = 1;
    take(simulation->createDistanceJoint(spring));
    ticks(*simulation, 180);
    const float springPosition = take(simulation->state(springBob)).pose.position.y;
    check(springPosition > 1.9F && springPosition < 2.2F,
          "damped distance spring converges near rest length under gravity");
    tether.first = bob;
    tether.second = bob;
    check(!simulation->createDistanceJoint(tether), "self joint rejected");

    auto motorWorld = world({});
    const auto pivot = body(*motorWorld, {}, BodyType::Static);
    const auto rotor = body(*motorWorld, {});
    take(motorWorld->createShape(rotor, Box{{1, 0.1F}}));
    RevoluteJointDef motor;
    motor.first = pivot;
    motor.second = rotor;
    motor.enableMotor = true;
    motor.motorSpeed = 2;
    motor.maxMotorTorque = 100;
    const auto hinge = take(motorWorld->createRevoluteJoint(motor));
    ticks(*motorWorld, 60);
    check(near(take(motorWorld->state(rotor)).angularVelocity, 2, 0.05F),
          "revolute motor drives angular velocity");
    ok(motorWorld->destroy(hinge));
    check(!motorWorld->valid(hinge), "explicit joint destruction invalidates handle");
    ok(motorWorld->setPose(rotor, {}));
    ok(motorWorld->setVelocity(rotor, {}));
    motor.enableLimit = true;
    motor.lowerAngle = -0.4F;
    motor.upperAngle = 0.4F;
    take(motorWorld->createRevoluteJoint(motor));
    ticks(*motorWorld, 60);
    const auto limitedAngle = take(motorWorld->state(rotor)).pose.angleRadians;
    check(limitedAngle > 0.35F && limitedAngle < 0.43F,
          "revolute angle limits constrain a powered motor");

    auto angleWorld = world({});
    const auto referencePivot = body(*angleWorld, {}, BodyType::Static);
    BodyDef referenceBody;
    referenceBody.pose.angleRadians = -std::numbers::pi_v<float> / 2;
    const auto referenceRotor = take(angleWorld->createBody(referenceBody));
    take(angleWorld->createShape(referenceRotor, Box{{1, 0.1F}}));
    RevoluteJointDef referenceHinge;
    referenceHinge.first = referencePivot;
    referenceHinge.second = referenceRotor;
    referenceHinge.referenceAngle = 3 * std::numbers::pi_v<float> / 2;
    referenceHinge.enableLimit = true;
    referenceHinge.lowerAngle = -0.05F;
    referenceHinge.upperAngle = 0.05F;
    take(angleWorld->createRevoluteJoint(referenceHinge));
    ticks(*angleWorld, 60);
    check(near(take(angleWorld->state(referenceRotor)).pose.angleRadians,
               -std::numbers::pi_v<float> / 2, 0.01F),
          "revolute reference angles preserve equivalent rotations beyond pi");

    auto fastWorld = world({});
    const auto wall = body(*fastWorld, {}, BodyType::Static);
    take(fastWorld->createShape(wall, Box{{0.02F, 10}}));
    BodyDef bullet;
    bullet.pose.position = {-2, 0};
    bullet.linearVelocity = {100, 0};
    bullet.bullet = true;
    const auto projectile = take(fastWorld->createBody(bullet));
    take(fastWorld->createShape(projectile, Circle{0.05F}));
    ticks(*fastWorld, 5);
    check(take(fastWorld->state(projectile)).pose.position.x < 0,
          "continuous collision prevents fast circle tunneling through thin static wall");
}
void stacksAndChurn() {
    auto simulation = world();
    const auto floor = body(*simulation, {0, 10}, BodyType::Static);
    take(simulation->createShape(floor, Box{{10, 0.5F}}));
    std::vector<BodyHandle> stack;
    for (unsigned i = 0; i < 12; ++i) {
        const auto box = body(*simulation, {0, 9.0F - static_cast<float>(i) * 1.01F});
        take(simulation->createShape(box, Box{}));
        stack.push_back(box);
    }
    ticks(*simulation, 600);
    float previousY = 10;
    for (const auto &handle : stack) {
        const auto value = take(simulation->state(handle));
        check(finite(value.pose.position) && value.pose.position.y < previousY - 0.8F,
              "stacked boxes preserve ordering without penetration or numerical failure");
        previousY = value.pose.position.y;
    }

    auto churn = world({});
    const auto owner = body(*churn, {}, BodyType::Static);
    const auto original = take(churn->createShape(owner, Box{}));
    ok(churn->destroy(original));
    for (unsigned i = 0; i < 70000; ++i) {
        const auto shape = take(churn->createShape(owner, Circle{}));
        ok(churn->destroy(shape));
    }
    const auto live = take(churn->createShape(owner, Box{}));
    check(!churn->valid(original) && churn->valid(live),
          "engine identity survives native 16-bit generation wrap");
    const auto beforeStep = take(churn->queryPoint({}));
    check(beforeStep.size() == 1 && beforeStep.front() == live,
          "native lookup resolves newest shape after generation wrap");
    ticks(*churn, 1);
    const auto afterStep = take(churn->queryPoint({}));
    check(afterStep.size() == 1 && afterStep.front() == live && churn->stats().shapes == 1,
          "retired metadata cleanup preserves live replacement shape");

    auto eventChurn = world({});
    const auto eventRegion = body(*eventChurn, {}, BodyType::Static);
    ShapeDef eventTrigger;
    eventTrigger.sensor = true;
    const auto eventSensor = take(eventChurn->createShape(eventRegion, Box{}, eventTrigger));
    const auto eventVisitorBody = body(*eventChurn, {});
    const auto eventVisitor = take(eventChurn->createShape(eventVisitorBody, Circle{0.1F}));
    ticks(*eventChurn, 1);
    ok(eventChurn->destroy(eventVisitor));
    for (unsigned i = 0; i < 65535; ++i) {
        const auto shape = take(eventChurn->createShape(eventVisitorBody, Circle{0.1F}));
        ok(eventChurn->destroy(shape));
    }
    const auto eventReplacement =
        take(eventChurn->createShape(eventVisitorBody, Circle{0.1F, {10, 0}}));
    check(take(eventChurn->sensorOverlaps(eventSensor)).empty(),
          "native generation wrap cannot alias a historical sensor visitor to a live shape");
    ticks(*eventChurn, 1);
    bool wrappedEnd{};
    for (const auto &event : eventChurn->events())
        wrappedEnd |= event.type == EventType::SensorEnd && event.first == eventSensor &&
                      event.second == eventVisitor;
    check(wrappedEnd && eventChurn->valid(eventReplacement) && !eventChurn->valid(eventVisitor),
          "sensor end preserves destroyed identity across native generation wrap");

    auto batch = world({});
    const auto region = body(*batch, {}, BodyType::Static);
    ShapeDef sensorDef;
    sensorDef.sensor = true;
    const auto sensor = take(batch->createShape(region, Box{{0.5F, 1}}, sensorDef));
    BodyDef moving;
    moving.pose.position = {-1, 0};
    moving.linearVelocity = {30, 0};
    const auto traveler = take(batch->createBody(moving));
    take(batch->createShape(traveler, Circle{0.1F}));
    const auto result = take(batch->advance(8.0 / 60));
    bool entered{}, exited{};
    for (const auto &event : batch->events()) {
        entered |= event.type == EventType::SensorBegin && event.first == sensor;
        exited |= event.type == EventType::SensorEnd && event.first == sensor;
    }
    check(result.steps == 8 && entered && exited,
          "catch-up advance retains sensor events from every tick");
    take(batch->advance(0));
    check(batch->events().empty(), "advance without a tick clears previous frame events");
}

// One-way platforms: land from above, pass from below and from the side, respect rotation and
// motion, and refuse nonsense definitions.
void oneWayPlatforms() {
    const auto platform = [](World &simulation, Vec2 at, BodyType type = BodyType::Static,
                             float angle = 0.0F, Vec2 solid = {0.0F, -1.0F}) {
        BodyDef definition;
        definition.type = type;
        definition.pose = {at, angle};
        const auto handle = take(simulation.createBody(definition));
        ShapeDef shape;
        shape.oneWay = true;
        shape.oneWayNormal = solid;
        take(simulation.createShape(handle, Box{{2.0F, 0.15F}}, shape));
        return handle;
    };
    const auto ball = [](World &simulation, Vec2 at, Vec2 velocity = {}) {
        BodyDef definition;
        definition.pose.position = at;
        definition.linearVelocity = velocity;
        definition.fixedRotation = true;
        const auto handle = take(simulation.createBody(definition));
        ShapeDef material;
        material.friction = 0.0F;
        take(simulation.createShape(handle, Circle{0.25F}, material));
        return handle;
    };

    // Dropped from above it lands and rests on the top surface (y = -0.15 above the body center 0).
    {
        auto simulation = world();
        platform(*simulation, {0, 0});
        const auto dropped = ball(*simulation, {0, -3});
        ticks(*simulation, 120);
        const auto state = take(simulation->state(dropped));
        check(near(state.pose.position.y, -0.15F - 0.25F, 0.03F), "a body dropped onto a one-way platform rests on it");
        check(std::abs(state.linearVelocity.y) < 0.2F, "it is at rest, not falling through");
    }
    // Thrown up from below it passes through, then comes back down and lands on top.
    {
        auto simulation = world();
        platform(*simulation, {0, 0});
        const auto thrown = ball(*simulation, {0, 2}, {0, -9});
        float highest = 100.0F;
        bool passedThrough = false;
        for (unsigned i = 0; i < 240; ++i) {
            ticks(*simulation, 1);
            const float y = take(simulation->state(thrown)).pose.position.y;
            highest = std::min(highest, y);
            passedThrough = passedThrough || y < -0.5F;
        }
        check(passedThrough && highest < -1.0F, "a body moving up passes through a one-way platform");
        const auto state = take(simulation->state(thrown));
        check(near(state.pose.position.y, -0.15F - 0.25F, 0.03F), "and then lands on top of it");
    }
    // Flying sideways through it at its own height is not blocked.
    {
        auto simulation = world({});
        platform(*simulation, {0, 0});
        const auto flying = ball(*simulation, {-4, 0}, {6, 0});
        ticks(*simulation, 120);
        check(take(simulation->state(flying)).pose.position.x > 1.5F, "a one-way platform does not block from the side");
    }
    // Falling onto it from below-left of its edge misses it entirely.
    {
        auto simulation = world();
        platform(*simulation, {0, 0});
        const auto beside = ball(*simulation, {3.5F, -3});
        ticks(*simulation, 90);
        check(take(simulation->state(beside)).pose.position.y > 1.0F, "beside the platform there is nothing to land on");
    }
    // A rotated platform: solid side turned to the right blocks a body coming from the right.
    {
        auto simulation = world({});
        platform(*simulation, {0, 0}, BodyType::Static, std::numbers::pi_v<float> / 2.0F);
        // Platform rotated 90 degrees: its local "up" (0,-1) now points to +x.
        const auto fromRight = ball(*simulation, {4, 0}, {-6, 0});
        const auto fromLeft = ball(*simulation, {-4, 1.5F}, {6, 0});
        ticks(*simulation, 90);
        check(take(simulation->state(fromRight)).pose.position.x > 0.2F, "a rotated one-way platform blocks from its solid side");
        check(take(simulation->state(fromLeft)).pose.position.x > 1.5F, "and lets bodies through from the other side");
    }
    // A kinematic platform moving up carries a rider and does not let it sink through.
    {
        auto simulation = world();
        BodyDef definition;
        definition.type = BodyType::Kinematic;
        definition.pose.position = {0, 2};
        definition.linearVelocity = {0, -1.5F};
        const auto lift = take(simulation->createBody(definition));
        ShapeDef shape;
        shape.oneWay = true;
        take(simulation->createShape(lift, Box{{2.0F, 0.15F}}, shape));
        const auto rider = ball(*simulation, {0, 1.2F});
        ticks(*simulation, 120);
        const float liftY = take(simulation->state(lift)).pose.position.y;
        const float riderY = take(simulation->state(rider)).pose.position.y;
        check(liftY < 0.5F && near(riderY, liftY - 0.15F - 0.25F, 0.06F), "a rising one-way platform carries what stands on it");
    }
    // Contacts with a disabled (passing) one-way shape are not reported as touching.
    {
        auto simulation = world();
        platform(*simulation, {0, 0});
        const auto below = ball(*simulation, {0, 1.0F}, {0, -3});
        ticks(*simulation, 10);
        const auto touching = take(simulation->contacts(below));
        check(touching.empty(), "a body passing through a one-way platform has no touching contact");
    }
    // Definitions that make no sense are rejected; ordinary shapes are unaffected.
    {
        auto simulation = world();
        const auto solid = body(*simulation, {0, 0}, BodyType::Static);
        ShapeDef sensor;
        sensor.oneWay = true;
        sensor.sensor = true;
        check(!simulation->createShape(solid, Box{}, sensor), "a one-way sensor is rejected");
        ShapeDef degenerate;
        degenerate.oneWay = true;
        degenerate.oneWayNormal = {0, 0};
        check(!simulation->createShape(solid, Box{}, degenerate), "a zero solid-side direction is rejected");
        degenerate.oneWayNormal = {std::numeric_limits<float>::quiet_NaN(), 1};
        check(!simulation->createShape(solid, Box{}, degenerate), "a non-finite direction is rejected");
        // Destroying a one-way shape and creating another must not confuse the callback.
        ShapeDef one;
        one.oneWay = true;
        const auto shape = take(simulation->createShape(solid, Box{{2, 0.15F}}, one));
        ok(simulation->destroy(shape));
        take(simulation->createShape(solid, Box{{2, 0.15F}}));
        const auto resting = ball(*simulation, {0, -1});
        ticks(*simulation, 90);
        check(take(simulation->state(resting)).pose.position.y < 0.0F, "a regular shape created after a one-way one still blocks from above");
        const auto under = ball(*simulation, {0, 1.0F}, {0, -3});
        ticks(*simulation, 30);
        check(take(simulation->state(under)).pose.position.y > 0.0F, "and blocks from below");
    }
}
} // namespace
int main() {
    try {
        timingAndIntegration();
        contactsAndMaterials();
        sensorsAndFilters();
        queriesAndGeometry();
        lifetimeAndValidation();
        jointsAndFastBodies();
        stacksAndChurn();
        oneWayPlatforms();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Unexpected error: %s\n", error.what());
        return 1;
    }
    std::printf("Physics: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
