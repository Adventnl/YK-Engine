// Phase B foundations: typed values and variables, event payloads and delayed delivery, services,
// the order of the update phases, and render interpolation.
#include "support/check.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/core/Value.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include <cmath>
#include <string>
#include <vector>

using namespace yk;

namespace {
// ---- Values and variables
// ------------------------------------------------------------------------
void values() {
    const Value none;
    const Value yes{true}, three{std::int64_t{3}}, half{0.5}, word{std::string("Hello")};
    const Value numeric{std::string("3")}, id{EntityId{0xabcdef}}, point{Vec2{1.0F, 2.0F}};
    CHECK(typeOf(none) == ValueType::None && typeOf(yes) == ValueType::Bool);
    CHECK(typeOf(three) == ValueType::Int && typeOf(half) == ValueType::Number);
    CHECK(typeOf(word) == ValueType::String && typeOf(id) == ValueType::Entity);
    CHECK(typeOf(point) == ValueType::Vec2);
    CHECK(!toBool(none) && toBool(yes) && toBool(three) && toBool(half) && toBool(word));
    CHECK(!toBool(Value{std::string("false")}) && !toBool(Value{std::string("0")}) &&
          !toBool(Value{std::string()}));
    CHECK_NEAR(toNumber(three), 3.0);
    CHECK_NEAR(toNumber(numeric), 3.0);
    CHECK_NEAR(toNumber(word, -1.0), -1.0);
    CHECK(toInt(half) == 1 && toInt(Value{2.4}) == 2 && toInt(word, 9) == 9);
    CHECK(toText(three) == "3" && toText(half) == "0.50" && toText(yes) == "true");
    CHECK(toText(Value{2.0}) == "2" && toText(none).empty() && toText(point) == "1,2");
    CHECK(toText(id) == "0000000000abcdef");
    // Equality coerces numerics and numeric text, and nothing else.
    CHECK(valuesEqual(three, Value{3.0}) && valuesEqual(three, numeric) &&
          valuesEqual(yes, three) == false);
    CHECK(valuesEqual(yes, Value{std::int64_t{1}}) &&
          valuesEqual(word, Value{std::string("Hello")}));
    CHECK(!valuesEqual(word, three) && !valuesEqual(none, Value{false}) && valuesEqual(none, none));
    CHECK(valuesEqual(id, Value{EntityId{0xabcdef}}) && !valuesEqual(id, Value{EntityId{1}}));
    CHECK(valuesEqual(point, Value{Vec2{1.0F, 2.0F}}) &&
          !valuesEqual(point, Value{Vec2{1.0F, 3.0F}}));
    int order = 0;
    CHECK(compareValues(three, half, order) && order == 1);
    CHECK(compareValues(half, three, order) && order == -1);
    CHECK(compareValues(word, Value{std::string("Help")}, order) && order < 0);
    CHECK(compareValues(numeric, half, order) && order == 1);
    CHECK(!compareValues(word, three, order) && !compareValues(id, id, order));
    // JSON keeps the kinds apart where it can.
    for (const Value &value : {none, yes, three, half, word, id, point}) {
        auto round = valueFromJson(valueToJson(value));
        CHECK(round);
        if (round)
            CHECK(valuesEqual(round.value(), value) && typeOf(round.value()) == typeOf(value));
    }
    CHECK(!valueFromJson(Json::parse("{\"x\":1}").value()));
    CHECK(!valueFromJson(Json::parse("[1,2,3]").value()));
    CHECK(!valueFromJson(Json::parse("{\"entity\":\"zz\"}").value()));
}

void blackboard() {
    Blackboard board;
    board.set("gems", 3.0);
    board.set("name", std::string("Ember"));
    board.setBool("open", true);
    board.setInt("lives", 2);
    board.setEntity("target", EntityId{7});
    board.setVec2("spot", {4.0F, 5.0F});
    CHECK_NEAR(board.number("gems"), 3.0);
    CHECK(board.number("name", -1.0) == -1.0); // Text is not a number.
    CHECK(board.flag("open") && board.integer("lives") == 2 && board.number("lives") == 2.0);
    CHECK(board.number("open") == 1.0);
    CHECK(board.entity("target") == EntityId{7} && board.vec2("spot") == Vec2{4.0F, 5.0F});
    CHECK(board.text("gems") == "3" && board.text("open") == "true" &&
          board.text("none", "x") == "x");
    CHECK(board.format("{name} has {gems} ({open}) {missing:none}") == "Ember has 3 (true) none");
    board.add("gems", 2.0);
    CHECK_NEAR(board.number("gems"), 5.0);
    // Listeners hear changes only.
    int heard = 0;
    std::string last;
    const auto id = board.subscribe([&](const std::string &key, const Value &, const Value &) {
        ++heard;
        last = key;
    });
    board.set("gems", 5.0); // Same value: not a change.
    CHECK(heard == 0);
    const auto revision = board.revision();
    board.set("gems", 6.0);
    CHECK(heard == 1 && last == "gems" && board.revision() > revision);
    board.erase("gems");
    CHECK(heard == 2 && !board.has("gems"));
    board.unsubscribe(id);
    board.set("other", 1.0);
    CHECK(heard == 2);
    // Kept variables survive the scene, typed.
    board.keep("lives");
    board.keep("target");
    const auto kept = board.kept();
    CHECK(kept.size() == 2 && std::holds_alternative<std::int64_t>(kept.at("lives")));
    // JSON round trip.
    Blackboard copy;
    CHECK(copy.loadJson(board.toJson()));
    CHECK(copy.integer("lives") == 2 && copy.entity("target") == EntityId{7} &&
          copy.text("name") == "Ember" && copy.flag("open"));
    CHECK(!copy.loadJson(Json::parse("[1]").value()));
    CHECK(!copy.loadJson(Json::parse("{\"x\":{\"bad\":1}}").value()));
}

void events() {
    EventBus bus;
    std::vector<std::string> seen;
    bus.subscribe("crime.witnessed",
                  [&](const GameEvent &e) { seen.push_back("exact:" + e.name); });
    bus.subscribe("crime.*", [&](const GameEvent &e) { seen.push_back("prefix:" + e.name); });
    bus.subscribe("*", [&](const GameEvent &e) { seen.push_back("any:" + e.name); });
    bus.subscribe("crime", [&](const GameEvent &e) { seen.push_back("plain:" + e.name); });
    GameEvent event("crime.witnessed", EntityId{1}, EntityId{2});
    Json data = Json::object();
    data.set("severity", 15);
    event.data = data;
    CHECK(event.category() == "crime" && GameEvent("alone").category() == "alone");
    bus.setClock(7, 0.5);
    bus.emit(event);
    bus.emit(GameEvent("crimes.other"));
    CHECK(bus.dispatch() == 2);
    CHECK(seen == (std::vector<std::string>{"exact:crime.witnessed", "prefix:crime.witnessed",
                                            "any:crime.witnessed", "any:crimes.other"}));
    CHECK(bus.recent().size() == 2 && bus.recent().front().tick == 7);
    CHECK(bus.recent().front().data.get("severity").asInt() == 15);
    CHECK(bus.recent().front().source == EntityId{1});
    // Delayed events wait for the clock and keep their order.
    seen.clear();
    bus.emitAfter(GameEvent("crime.second"), 1.0);
    bus.emitAfter(GameEvent("crime.first"), 0.5);
    bus.advance(0.4);
    CHECK(bus.dispatch() == 0 && bus.delayed() == 2);
    bus.advance(0.2);
    CHECK(bus.dispatch() == 1 && seen.back() == "any:crime.first");
    bus.advance(0.5);
    CHECK(bus.dispatch() == 1 && seen.back() == "any:crime.second" && bus.delayed() == 0);
    // History is bounded and adjustable; handlers that keep emitting are cut off.
    bus.setHistoryLimit(2);
    for (int i = 0; i < 5; ++i)
        bus.emit(GameEvent("tick"));
    bus.dispatch();
    CHECK(bus.recent().size() == 2);
    EventBus loop;
    loop.subscribe("ping", [&](const GameEvent &) { loop.emit(GameEvent("ping")); });
    loop.emit(GameEvent("ping"));
    setLogStderrEnabled(false);
    CHECK(loop.dispatch() > 0 && loop.pending() == 0);
    setLogStderrEnabled(true);
}

// ---- Services and phases
// -------------------------------------------------------------------------
std::vector<std::string> trace;

struct ClockService final : Service {
    int started{}, ticks{}, stopped{};
    const char *name() const override {
        return "clock";
    }
    UpdatePhase phase() const override {
        return UpdatePhase::Clock;
    }
    void onStart(GameContext &) override {
        ++started;
        trace.push_back("service:start");
    }
    void onFixedUpdate(GameContext &, float) override {
        ++ticks;
        trace.push_back("service:clock");
    }
    void onShutdown(GameContext &) override {
        ++stopped;
        trace.push_back("service:stop");
    }
    std::string saveKey() const override {
        return "clock";
    }
};
struct LateService final : Service {
    const char *name() const override {
        return "late";
    }
    void onFixedUpdate(GameContext &, float) override {
        trace.push_back("service:post");
    }
};

template <UpdatePhase P> struct PhaseProbe final : Component {
    static void describe(TypeBuilder<PhaseProbe> &type) {
        type.category("Test").updatePhase(P);
    }
    void onStart(GameContext &context) override {
        if (P == UpdatePhase::Clock)
            context.services().get<ClockService>();
        if (P == UpdatePhase::PostSimulation)
            context.services().get<LateService>();
    }
    void onFixedUpdate(GameContext &context, float) override {
        std::string line = std::string(updatePhaseName(P));
        if (const auto body = context.bodyOf(entity().id()))
            line += ":" + std::to_string(static_cast<int>(std::lround(
                              context.physics().state(*body).value().pose.position.x * 100.0)));
        trace.push_back(line);
    }
};

struct Mover final : Component {
    float speed{1.0F}; // Meters per tick.
    float jumpAt{-1}, jumpBy{0};
    int ticks{};
    static void describe(TypeBuilder<Mover> &type) {
        type.category("Test");
        type.field("speed", &Mover::speed);
    }
    void onFixedUpdate(GameContext &, float) override {
        ++ticks;
        entity().transform().position.x += speed;
        if (static_cast<float>(ticks) == jumpAt)
            entity().transform().position.x += jumpBy; // An unannounced jump.
    }
};

ComponentRegistry makeRegistry() {
    ComponentRegistry registry;
    registerEngineComponents(registry);
    registry.add<PhaseProbe<UpdatePhase::Clock>>("ClockProbe");
    registry.add<PhaseProbe<UpdatePhase::PreUpdate>>("PreProbe");
    registry.add<PhaseProbe<UpdatePhase::Decision>>("DecisionProbe");
    registry.add<PhaseProbe<UpdatePhase::Gameplay>>("GameplayProbe");
    registry.add<PhaseProbe<UpdatePhase::Steering>>("SteeringProbe");
    registry.add<PhaseProbe<UpdatePhase::Perception>>("PerceptionProbe");
    registry.add<PhaseProbe<UpdatePhase::PostSimulation>>("PostProbe");
    registry.add<Mover>("Mover");
    return registry;
}

std::unique_ptr<GameRuntime> start(std::unique_ptr<Scene> scene, RuntimeOptions options = {}) {
    auto created = GameRuntime::create(std::move(scene), options);
    CHECK(created);
    return created ? std::move(created.value()) : nullptr;
}

void phasesAndServices() {
    ComponentRegistry registry = makeRegistry();
    auto scene = std::make_unique<Scene>(registry, 3);
    // Probes are created in the reverse of the order they must run in, so a pass that simply
    // followed the hierarchy would get it wrong.
    Entity &mover = scene->createEntity("Body");
    mover.transform().position = {0, 0};
    mover.add<RigidBody>().gravityScale = 0.0F;
    mover.add<Collider>();
    mover.add<PhaseProbe<UpdatePhase::PostSimulation>>();
    mover.add<PhaseProbe<UpdatePhase::Perception>>();
    mover.add<PhaseProbe<UpdatePhase::Steering>>();
    mover.add<PhaseProbe<UpdatePhase::Gameplay>>();
    mover.add<PhaseProbe<UpdatePhase::Decision>>();
    mover.add<PhaseProbe<UpdatePhase::PreUpdate>>();
    mover.add<PhaseProbe<UpdatePhase::Clock>>();
    const EntityId id = mover.id();
    auto runtime = start(std::move(scene));
    if (!runtime)
        return;
    trace.clear();
    CHECK(!runtime->services().has<ClockService>());
    // The velocity is applied before the first step so the position visibly changes by it.
    runtime->physics().setVelocity(*runtime->bodyOf(id), {1.0F, 0.0F});
    runtime->stepOnce(Keyboard{});
    CHECK(runtime->services().has<ClockService>() && runtime->services().has<LateService>());
    // The probes ran once each, in phase order, whatever the order of the components on the entity.
    // Positions: before the physics step the body is still at 0; after it, moved by its velocity.
    std::vector<std::string> probes;
    for (const std::string &line : trace)
        if (line.find(':') != std::string::npos && line.rfind("service:", 0) != 0)
            probes.push_back(line);
    CHECK(probes == (std::vector<std::string>{"Clock:0", "PreUpdate:0", "Decision:0", "Gameplay:0",
                                              "Steering:0", "Perception:2", "PostSimulation:2"}));
    // The services of a phase run before the components of that phase.
    const auto at = [&](const std::string &line) {
        return std::find(trace.begin(), trace.end(), line) - trace.begin();
    };
    CHECK(at("service:clock") < at("Clock:0") && at("service:post") < at("PostSimulation:2"));
    CHECK(std::count(trace.begin(), trace.end(), "service:start") == 1);
    auto &clock = runtime->services().get<ClockService>();
    CHECK(clock.started == 1 && clock.ticks == 1);
    CHECK(&clock == &runtime->services().get<ClockService>()); // One instance.
    CHECK(runtime->services().byKey("clock") == &clock &&
          runtime->services().byKey("x") == nullptr);
    CHECK(runtime->services().all().size() == 2);
    runtime->stepOnce(Keyboard{});
    CHECK(clock.ticks == 2);
    // A restart ends the services of the old run and the new run makes its own.
    trace.clear();
    CHECK(runtime->restart());
    CHECK(std::count(trace.begin(), trace.end(), "service:stop") == 1);
    CHECK(!runtime->services().has<ClockService>());
    runtime->stepOnce(Keyboard{});
    CHECK(runtime->services().get<ClockService>().ticks == 1);
}

// ---- Interpolation
// ---------------------------------------------------------------------------------
struct Rig {
    ComponentRegistry registry = makeRegistry();
    std::unique_ptr<Scene> scene = std::make_unique<Scene>(registry, 11);
    std::unique_ptr<GameRuntime> runtime;
    EntityId mover, child;
    void build(float speed = 1.0F) {
        Entity &body = scene->createEntity("Mover");
        body.add<Mover>().speed = speed;
        mover = body.id();
        Entity &part = scene->createEntity("Child", mover);
        part.transform().position = {0.0F, 2.0F};
        child = part.id();
        runtime = start(std::move(scene));
    }
    float drawn(float alpha) const {
        return runtime->scene().find(mover)->renderPosition(alpha).x;
    }
};

void interpolation() {
    Rig rig;
    rig.build(1.0F);
    GameRuntime &runtime = *rig.runtime;
    // Before any tick the entity is where it was authored.
    CHECK_NEAR(rig.drawn(0.5F), 0.0);
    runtime.stepOnce(Keyboard{});
    CHECK_NEAR(runtime.interpolationAlpha(), 1.0);
    CHECK_NEAR(rig.drawn(1.0F), 1.0); // The step shows the tick exactly.
    CHECK_NEAR(rig.drawn(0.0F), 0.0); // And 0 is the tick before.
    CHECK_NEAR(rig.drawn(0.5F), 0.5);
    // A 144 Hz display on a 60 Hz simulation: the picture moves every frame, forward, in even
    // steps, and ends where the simulation is.
    const double frame = 1.0 / 144.0;
    for (int i = 0; i < 10; ++i) // Frames settle the accumulator (stepOnce does not touch it).
        runtime.update(frame, Keyboard{});
    float previous = rig.drawn(runtime.interpolationAlpha());
    float smallest = 1e9F, largest = 0.0F;
    int ticksBefore = static_cast<int>(runtime.tick());
    for (int i = 0; i < 288; ++i) { // Two seconds of frames.
        runtime.update(frame, Keyboard{});
        const float now = rig.drawn(runtime.interpolationAlpha());
        const float delta = now - previous;
        CHECK(delta >= -1e-4F);
        smallest = std::min(smallest, delta);
        largest = std::max(largest, delta);
        previous = now;
    }
    const int ticksRun = static_cast<int>(runtime.tick()) - ticksBefore;
    CHECK(ticksRun >= 119 && ticksRun <= 121);
    // One tick is 1 m; a frame is 60/144 of that. Every frame moves by about that much.
    CHECK(largest < 0.45F && smallest > 0.38F);
    // The picture lags the simulation by less than a tick and never leads it.
    const float simulated = runtime.scene().find(rig.mover)->worldPosition().x;
    CHECK(previous <= simulated + 1e-4F && simulated - previous < 1.0F + 1e-4F);
    // A child is drawn with its parent, not one tick behind or ahead of it.
    const Entity *child = runtime.scene().find(rig.child);
    for (const float alpha : {0.0F, 0.25F, 0.5F, 0.9F}) {
        CHECK_NEAR(child->renderTransform(alpha).position.x, rig.drawn(alpha), 1e-4);
        CHECK_NEAR(child->renderTransform(alpha).position.y, 2.0, 1e-4);
    }
}

void interpolationSnaps() {
    Rig rig;
    rig.build(0.1F);
    GameRuntime &runtime = *rig.runtime;
    for (int i = 0; i < 5; ++i)
        runtime.stepOnce(Keyboard{});
    // Announced: GameContext::teleport. Whatever alpha the picture has, it is at the new place.
    Entity &mover = *runtime.scene().find(rig.mover);
    runtime.teleport(mover, {50.0F, 0.0F});
    runtime.stepOnce(Keyboard{});
    for (const float alpha : {0.0F, 0.3F, 0.7F, 1.0F})
        CHECK(mover.renderPosition(alpha).x > 49.9F);
    CHECK(runtime.scene().find(rig.child)->renderPosition(0.0F).x > 49.9F);
    // Unannounced but longer than a tick of motion could be: also not blended.
    mover.get<Mover>()->jumpAt = static_cast<float>(mover.get<Mover>()->ticks + 1);
    mover.get<Mover>()->jumpBy = 30.0F;
    runtime.stepOnce(Keyboard{});
    for (const float alpha : {0.0F, 0.5F, 1.0F})
        CHECK(mover.renderPosition(alpha).x > 79.9F);
    // Something else moved the entity after the tick: it is drawn where it really is.
    mover.transform().position.x = 5.0F;
    CHECK_NEAR(mover.renderPosition(0.5F).x, 5.0);
    CHECK_NEAR(runtime.scene().find(rig.child)->renderPosition(0.5F).x, 5.0); // Its child too.
    runtime.stepOnce(Keyboard{});
    CHECK(mover.renderPosition(0.5F).x > 5.0F);
    // A scene that is not running has no state and shows the plain world transform.
    ComponentRegistry registry = makeRegistry();
    Scene idle(registry, 2);
    Entity &still = idle.createEntity("Still");
    still.transform().position = {3.0F, 4.0F};
    CHECK(!still.hasRenderState() && still.renderPosition(0.5F) == Vec2{3.0F, 4.0F});
    // A restart begins again without a smear across the reset.
    CHECK(runtime.restart());
    const Entity &fresh = *runtime.scene().find(rig.mover);
    CHECK_NEAR(fresh.renderPosition(0.0F).x, 0.0);
    CHECK_NEAR(fresh.renderPosition(1.0F).x, 0.0);
}

void rotationBlend() {
    ComponentRegistry registry = makeRegistry();
    auto scene = std::make_unique<Scene>(registry, 5);
    // A rotation that crosses the 180 degree seam goes the short way round.
    Entity &entity = scene->createEntity("Turner");
    entity.transform().rotationDegrees = 170.0F;
    const EntityId id = entity.id();
    auto runtime = start(std::move(scene));
    Entity &live = *runtime->scene().find(id);
    live.captureRenderState(5.0F);
    live.transform().rotationDegrees = -170.0F; // 20 degrees further round, not 340 back.
    live.captureRenderState(5.0F);
    CHECK_NEAR(live.renderTransform(0.5F).rotationDegrees, 180.0, 1e-3);
    CHECK_NEAR(std::fabs(live.renderTransform(0.25F).rotationDegrees), 175.0, 1e-3);
}

void cameraInterpolates() {
    Rig rig;
    auto &scene = *rig.scene;
    Entity &cameraEntity = scene.createEntity("Camera");
    auto &camera = cameraEntity.add<Camera>();
    camera.mode = CameraMode::Follow;
    camera.smoothTime = 0.0F;
    const EntityId cameraId = cameraEntity.id();
    rig.build(1.0F);
    Entity *followed = rig.runtime->scene().find(rig.mover);
    auto *live = rig.runtime->scene().find(cameraId)->get<Camera>();
    live->targets = {followed->id()};
    rig.runtime->stepOnce(Keyboard{});
    rig.runtime->stepOnce(Keyboard{});
    CHECK_NEAR(live->view().position.x, 2.0);
    CHECK_NEAR(live->viewAt(0.0F).position.x, 1.0);
    CHECK_NEAR(live->viewAt(0.5F).position.x, 1.5);
    CHECK_NEAR(live->viewAt(1.0F).position.x, 2.0);
    // The camera and what it follows are blended identically, so the target stays put on screen.
    for (const float alpha : {0.0F, 0.3F, 0.6F, 1.0F})
        CHECK_NEAR(live->viewAt(alpha).position.x, followed->renderPosition(alpha).x, 1e-4);
}
} // namespace

int main() {
    values();
    blackboard();
    events();
    phasesAndServices();
    interpolation();
    interpolationSnaps();
    rotationBlend();
    cameraInterpolates();
    return yk::test::finish("foundation");
}
