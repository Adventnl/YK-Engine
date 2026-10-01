#include "RuntimeImpl.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
void InputTracker::feed(const InputFrame &frame) {
    for (std::size_t i = 0; i < keyCount; ++i) {
        const ButtonState state = frame.keyboard.state(static_cast<Key>(i));
        held_[i] = state.held;
        pressed_[i] = pressed_[i] || state.pressed;
        released_[i] = released_[i] || state.released;
    }
    for (std::size_t p = 0; p < maxGamepads; ++p) {
        PadTrack &track = pads_[p];
        const Gamepad &pad = frame.gamepads[p];
        track.connected = pad.connected();
        for (std::size_t b = 0; b < gamepadButtonCount; ++b) {
            const ButtonState state = pad.button(static_cast<GamepadButton>(b));
            track.held[b] = state.held;
            track.pressed[b] = track.pressed[b] || state.pressed;
            track.released[b] = track.released[b] || state.released;
        }
        for (std::size_t a = 0; a < gamepadAxisCount; ++a)
            track.axes[a] = pad.axis(static_cast<GamepadAxis>(a));
    }
}
void InputTracker::fill(InputFrame &tick) {
    for (std::size_t i = 0; i < keyCount; ++i) {
        tick.keyboard.assign(static_cast<Key>(i), {held_[i], pressed_[i], released_[i]});
        pressed_[i] = released_[i] = false;
    }
    for (std::size_t p = 0; p < maxGamepads; ++p) {
        PadTrack &track = pads_[p];
        Gamepad &pad = tick.gamepads[p];
        pad.setConnected(track.connected);
        for (std::size_t b = 0; b < gamepadButtonCount; ++b) {
            pad.assignButton(static_cast<GamepadButton>(b),
                             {track.held[b], track.pressed[b], track.released[b]});
            track.pressed[b] = track.released[b] = false;
        }
        for (std::size_t a = 0; a < gamepadAxisCount; ++a)
            pad.setAxis(static_cast<GamepadAxis>(a), track.axes[a]);
    }
}

GameRuntime::GameRuntime() : impl_(std::make_unique<Impl>(*this)) {}
GameRuntime::~GameRuntime() {
    impl_->shutdown();
}

Result<std::unique_ptr<GameRuntime>> GameRuntime::create(std::unique_ptr<Scene> scene,
                                                         RuntimeOptions options) {
    if (!scene)
        return Error{"GameRuntime needs a scene"};
    if (!(options.fixedSeconds >= 0.0001 && options.fixedSeconds <= 0.1) ||
        options.maxStepsPerFrame == 0 || !finite(options.viewportSize) ||
        options.viewportSize.x <= 0 || options.viewportSize.y <= 0 ||
        !(options.transitionSeconds >= 0.0F && options.transitionSeconds <= 30.0F))
        return Error{"Invalid runtime options"};
    if (auto status = options.layers.validate(); !status)
        return Error{status.error()};
    if (auto status = options.inputMap.validate(); !status)
        return Error{status.error()};
    auto runtime = std::unique_ptr<GameRuntime>(new GameRuntime());
    runtime->impl_->options = std::move(options);
    runtime->impl_->viewport = runtime->impl_->options.viewportSize;
    runtime->impl_->startState = sceneToJson(*scene);
    if (auto status = runtime->impl_->rebuild(std::move(scene)); !status)
        return Error{status.error()};
    if (runtime->impl_->options.startCovered && runtime->impl_->options.transitionSeconds > 0.0F) {
        runtime->impl_->phase = Impl::Phase::In; // Fades in from black.
        runtime->impl_->fade = 1.0F;
    }
    return runtime;
}

Status GameRuntime::Impl::rebuild(std::unique_ptr<Scene> fresh) {
    services.shutdown(); // Services belong to one run of a scene; the new scene starts its own.
    scene = std::move(fresh);
    schedule = ComponentSchedule{};
    alpha = 1.0F;
    blackboard.clear();
    for (const auto &[key, value] : options.variables) { // What the previous scene carried over.
        blackboard.setValue(key, value);
        blackboard.keep(key);
    }
    inputLocks.clear();
    announced = false;
    events.clear();
    started.clear();
    destroyQueue.clear();
    time = 0;
    ticks = 0;
    accumulator = 0;
    restartWanted = false;
    tickInput = InputFrame{};
    actions = ActionInput(options.inputMap);
    auto built = buildWorld();
    if (built)
        captureInterpolation(); // The first picture is the scene as it stands, not a blend.
    return built;
}

void GameRuntime::Impl::shutdown() {
    if (!scene)
        return;
    forEachComponent([&](Component &component) {
        if (started.contains(&component))
            component.onDestroy(self);
    });
    started.clear();
    services.shutdown();
}

Status GameRuntime::restart() {
    impl_->shutdown();
    auto fresh = sceneFromJson(impl_->startState, impl_->scene->registry());
    if (!fresh)
        return Error{"Restart failed: " + fresh.error()};
    return impl_->rebuild(std::move(fresh.value()));
}

void GameRuntime::Impl::startPending() {
    // A component started here may spawn entities whose components also need starting, so repeat
    // until a pass starts nothing (bounded: each pass must make progress).
    for (int pass = 0; pass < 8; ++pass) {
        std::vector<Component *> fresh;
        forEachComponent([&](Component &component) {
            if (started.insert(&component).second)
                fresh.push_back(&component);
        });
        if (fresh.empty())
            return;
        for (Component *component : fresh)
            component->onStart(self);
    }
}

void GameRuntime::Impl::beginTransition(bool restart, std::string nextScene) {
    if (options.transitionSeconds <= 0.0F) { // Instant: what tests and tools want.
        if (restart)
            restartWanted = true;
        else
            sceneChange = std::move(nextScene);
        return;
    }
    if (phase == Phase::Out || phase == Phase::Covered)
        return; // Already on the way out; the first request wins.
    // Fading in when the request comes: turn around from where the fade is, without a jump.
    phaseTime = phase == Phase::In ? fade * options.transitionSeconds : 0.0F;
    phase = Phase::Out;
    restartAfterFade = restart;
    sceneAfterFade = std::move(nextScene);
}

void GameRuntime::Impl::advanceTransition(float seconds) {
    if (phase == Phase::Idle || phase == Phase::Covered)
        return;
    const float length = options.transitionSeconds;
    phaseTime += seconds;
    if (phase == Phase::Out) {
        fade = std::min(1.0F, phaseTime / length);
        if (fade < 1.0F)
            return;
        if (restartAfterFade) { // Covered: rebuild at the end of this frame, then show it again.
            restartWanted = true;
            phase = Phase::In;
            phaseTime = 0.0F;
        } else {
            sceneChange = sceneAfterFade; // The host swaps in the next scene while it is covered.
            phase = Phase::Covered;
        }
    } else {
        fade = std::max(0.0F, 1.0F - phaseTime / length);
        if (fade <= 0.0F)
            phase = Phase::Idle;
    }
}

const GameRuntime::Impl::ComponentSchedule &GameRuntime::Impl::componentSchedule() {
    if (schedule.revision == scene->revision())
        return schedule;
    schedule.all.clear();
    for (auto &list : schedule.byPhase)
        list.clear();
    for (Entity *entity : scene->orderedEntities())
        for (const auto &component : entity->components()) {
            schedule.all.push_back(component.get());
            schedule.byPhase[static_cast<std::size_t>(component.get()->type().phase)].push_back(
                component.get());
        }
    schedule.revision = scene->revision();
    return schedule;
}

// The services of one phase, then the components of that phase, in hierarchy order. A hook may
// spawn entities (they join the next pass) but never removes any, so the pointers stay valid.
void GameRuntime::Impl::runPhase(UpdatePhase which, float step) {
    services.tick(which, step);
    const std::vector<Component *> list =
        componentSchedule().byPhase[static_cast<std::size_t>(which)];
    for (Component *component : list)
        if (component->enabled && component->entity().activeInHierarchy())
            component->onFixedUpdate(self, step);
}

void GameRuntime::Impl::captureInterpolation() {
    for (Entity *entity : scene->orderedEntities())
        entity->captureRenderState(options.interpolationSnapDistance);
}

void GameRuntime::Impl::fixedTick() {
    input.fill(tickInput);
    actions.update(tickInput);
    syncActivation();
    startPending();
    if (!announced) {
        announced = true;
        self.emit("scene_started");
    }
    const float step = static_cast<float>(options.fixedSeconds);
    events.setClock(ticks, time);
    events.advance(options.fixedSeconds);
    // Before the physics step: time, scripts, decisions, movement intent, steering.
    for (std::size_t index = 0; index < updatePhaseCount; ++index)
        if (runsBeforePhysics(static_cast<UpdatePhase>(index)))
            runPhase(static_cast<UpdatePhase>(index), step);
    if (auto advanced = world->advance(options.fixedSeconds); !advanced)
        log(LogLevel::Error, "physics", advanced.error());
    syncTransforms();
    updateTriggers();
    dispatchCollisions();
    // After it: what perceives and accounts for where everything ended up.
    for (std::size_t index = 0; index < updatePhaseCount; ++index)
        if (!runsBeforePhysics(static_cast<UpdatePhase>(index)))
            runPhase(static_cast<UpdatePhase>(index), step);
    flushDestroys();
    events.dispatch();
    ++ticks;
    time = static_cast<double>(ticks) * options.fixedSeconds;
    events.setClock(ticks, time);
    captureInterpolation();
    advanceTransition(step);
}

void GameRuntime::Impl::variableUpdate(float seconds) {
    forEachComponent([&](Component &component) { component.onUpdate(self, seconds); });
    forEachComponent([&](Component &component) { component.onLateUpdate(self, seconds); });
    events.dispatch();
}

void GameRuntime::Impl::flushDestroys() {
    while (!destroyQueue.empty()) {
        std::vector<EntityId> doomed;
        doomed.swap(destroyQueue);
        for (const EntityId root : doomed) {
            if (!scene->find(root))
                continue;
            const auto subtree = scene->subtree(root);
            for (const EntityId id : subtree) {
                Entity *entity = scene->find(id);
                for (const auto &component : entity->components())
                    if (started.erase(component.get()) > 0)
                        component->onDestroy(self);
            }
            unbindEntities(subtree);
            scene->destroy(root);
        }
    }
}

void GameRuntime::Impl::finishFrame() {
    if (restartWanted) {
        if (auto status = self.restart(); !status)
            log(LogLevel::Error, "runtime", status.error());
    }
}

void GameRuntime::update(double frameSeconds, const Keyboard &keyboard) {
    InputFrame frame;
    frame.keyboard = keyboard;
    update(frameSeconds, frame);
}

namespace {
// Frames arrive a hair off a multiple of the tick (a 60 Hz screen delivers 16.4 ms, then 16.9 ms),
// and a raw clock turns that into no tick on one frame and two on the next: a visible hitch every
// few seconds. A frame time within 0.4 ms of one, two or three ticks, or of half a tick (a 120 Hz
// screen), counts as exactly that; anything else is used as measured.
double snapFrameTime(double seconds, double step) {
    constexpr double tolerance = 0.0004;
    for (const double multiple : {0.5, 1.0, 2.0, 3.0})
        if (std::abs(seconds - multiple * step) < tolerance)
            return multiple * step;
    return seconds;
}
} // namespace

void GameRuntime::update(double frameSeconds, const InputFrame &frameInput) {
    auto &state = *impl_;
    if (state.paused)
        return;
    state.input.feed(frameInput);
    if (!std::isfinite(frameSeconds) || frameSeconds < 0)
        frameSeconds = 0;
    const double step = state.options.fixedSeconds;
    frameSeconds = snapFrameTime(frameSeconds, step);
    state.accumulator += std::min(frameSeconds, 0.25);
    unsigned steps = 0;
    while (state.accumulator + 1e-9 >= step && steps < state.options.maxStepsPerFrame) {
        state.fixedTick();
        state.accumulator -= step;
        ++steps;
    }
    if (state.accumulator + 1e-9 >= step) // Too far behind: drop the backlog instead of spiraling.
        state.accumulator = std::fmod(state.accumulator, step);
    // The picture is `accumulator` past the last tick: render interpolation blends that far.
    state.alpha = static_cast<float>(std::clamp(state.accumulator / step, 0.0, 1.0));
    state.variableUpdate(static_cast<float>(frameSeconds));
    state.finishFrame();
}

void GameRuntime::stepOnce(const Keyboard &keyboard) {
    InputFrame frame;
    frame.keyboard = keyboard;
    stepOnce(frame);
}

void GameRuntime::stepOnce(const InputFrame &frameInput) {
    auto &state = *impl_;
    if (state.paused)
        return;
    state.input.feed(frameInput);
    state.fixedTick();
    state.alpha = 1.0F; // A single step shows the tick exactly.
    state.variableUpdate(static_cast<float>(state.options.fixedSeconds));
    state.finishFrame();
}

const std::string &GameRuntime::sceneChangeRequested() const {
    return impl_->sceneChange;
}
void GameRuntime::clearSceneChangeRequest() {
    impl_->sceneChange.clear();
    if (impl_->phase == Impl::Phase::Covered) { // The host will not switch: show the game again.
        impl_->phase = Impl::Phase::In;
        impl_->phaseTime = 0.0F;
    }
}
void GameRuntime::setPaused(bool paused) {
    impl_->paused = paused;
}
bool GameRuntime::paused() const {
    return impl_->paused;
}
void GameRuntime::setViewportSize(Vec2 pixels) {
    if (finite(pixels) && pixels.x > 0 && pixels.y > 0)
        impl_->viewport = pixels;
}

Scene &GameRuntime::scene() {
    return *impl_->scene;
}
physics::World &GameRuntime::physics() {
    return *impl_->world;
}
const physics::World &GameRuntime::physics() const {
    return *impl_->world;
}
const Keyboard &GameRuntime::keyboard() const {
    return impl_->tickInput.keyboard;
}
const ActionInput &GameRuntime::input() const {
    return impl_->actions;
}
AudioSink &GameRuntime::audio() {
    return impl_->options.audio ? *impl_->options.audio : impl_->nullAudio;
}
const AssetSource *GameRuntime::assets() const {
    return impl_->options.assets;
}
namespace {
// Reads and parses a JSON asset; logs and returns an error result when it cannot.
Result<Json> readJsonAsset(const AssetSource *assets, const std::string &path) {
    if (!assets)
        return Error{"there is no asset source"};
    auto text = assets->readText(path);
    if (!text)
        return Error{text.error()};
    return Json::parse(text.value());
}
} // namespace
std::shared_ptr<const AnimationSet> GameRuntime::animationSet(const std::string &path) {
    const auto cached = impl_->animationSets.find(path);
    if (cached != impl_->animationSets.end())
        return cached->second;
    std::shared_ptr<const AnimationSet> loaded;
    auto document = readJsonAsset(impl_->options.assets, path);
    auto set = document ? parseAnimationSet(document.value())
                        : Result<AnimationSet>(Error{document.error()});
    if (set)
        loaded = std::make_shared<const AnimationSet>(std::move(set.value()));
    else
        log(LogLevel::Warning, "animation", "Cannot load " + path + ": " + set.error());
    impl_->animationSets.emplace(path, loaded);
    return loaded;
}
std::shared_ptr<const AnimationController>
GameRuntime::animationController(const std::string &path) {
    const auto cached = impl_->animationControllers.find(path);
    if (cached != impl_->animationControllers.end())
        return cached->second;
    std::shared_ptr<const AnimationController> loaded;
    auto document = readJsonAsset(impl_->options.assets, path);
    auto controller = document ? AnimationController::fromJson(document.value())
                               : Result<AnimationController>(Error{document.error()});
    if (controller)
        loaded = std::make_shared<const AnimationController>(std::move(controller.value()));
    else
        log(LogLevel::Warning, "animation", "Cannot load " + path + ": " + controller.error());
    impl_->animationControllers.emplace(path, loaded);
    return loaded;
}
Blackboard &GameRuntime::blackboard() {
    return impl_->blackboard;
}
EventBus &GameRuntime::events() {
    return impl_->events;
}
Services &GameRuntime::services() {
    return impl_->services;
}
float GameRuntime::interpolationAlpha() const {
    return impl_->alpha;
}
const LayerConfig &GameRuntime::layers() const {
    return impl_->options.layers;
}
float GameRuntime::fixedDelta() const {
    return static_cast<float>(impl_->options.fixedSeconds);
}
double GameRuntime::time() const {
    return impl_->time;
}
std::uint64_t GameRuntime::tick() const {
    return impl_->ticks;
}
Vec2 GameRuntime::viewportSize() const {
    return impl_->viewport;
}
std::optional<physics::BodyHandle> GameRuntime::bodyOf(EntityId entity) const {
    const auto found = impl_->bodies.find(entity);
    if (found == impl_->bodies.end())
        return std::nullopt;
    return found->second.body;
}
Entity *GameRuntime::entityOfBody(physics::BodyHandle body) const {
    const auto found = impl_->entityByBody.find(body.serial());
    return found == impl_->entityByBody.end() ? nullptr : impl_->scene->find(found->second);
}
Entity *GameRuntime::entityOfShape(physics::ShapeHandle shape) const {
    const auto found = impl_->shapes.find(shape.serial());
    return found == impl_->shapes.end() ? nullptr : impl_->scene->find(found->second.entity);
}
std::vector<physics::ShapeHandle> GameRuntime::bodyShapes(EntityId entity) const {
    std::vector<physics::ShapeHandle> result;
    for (const auto &[serial, record] : impl_->shapes) {
        (void)serial;
        if (record.bodyEntity == entity && !record.trigger)
            result.push_back(record.handle);
    }
    return result;
}
const std::vector<EntityId> &GameRuntime::overlapping(EntityId trigger) const {
    const auto found = impl_->overlaps.find(trigger);
    return found == impl_->overlaps.end() ? impl_->noOverlaps : found->second;
}
void GameRuntime::teleport(Entity &entity, Vec2 worldPosition) {
    entity.setWorldPosition(worldPosition);
    for (const EntityId id : impl_->scene->subtree(entity.id())) // No smear across the jump.
        if (Entity *part = impl_->scene->find(id))
            part->snapRenderState();
    const auto found = impl_->bodies.find(entity.id());
    if (found == impl_->bodies.end())
        return;
    const Transform2D world = entity.worldTransform();
    impl_->world->setPose(found->second.body,
                          {world.position, degreesToRadians(world.rotationDegrees)});
    if (found->second.type != physics::BodyType::Static)
        impl_->world->setVelocity(found->second.body, {}, 0.0F);
}
void GameRuntime::destroyLater(EntityId entity) {
    if (std::find(impl_->destroyQueue.begin(), impl_->destroyQueue.end(), entity) ==
        impl_->destroyQueue.end())
        impl_->destroyQueue.push_back(entity);
}
Result<EntityId> GameRuntime::spawn(const Json &prefab, Vec2 worldPosition, EntityId parent) {
    auto created = instantiateSubtree(*impl_->scene, prefab, parent, worldPosition);
    if (!created)
        return created;
    impl_->bindEntities(impl_->scene->subtree(created.value()));
    return created;
}
Result<EntityId> GameRuntime::spawnPrefab(const std::string &path, Vec2 worldPosition,
                                          EntityId parent) {
    auto cached = impl_->prefabDocuments.find(path);
    if (cached == impl_->prefabDocuments.end()) {
        if (impl_->failedPrefabs.contains(path))
            return Error{"Prefab " + path + " could not be loaded"}; // Already reported.
        auto document = readJsonAsset(impl_->options.assets, path);
        if (!document) {
            impl_->failedPrefabs.insert(path);
            log(LogLevel::Warning, "runtime",
                "Cannot load prefab " + path + ": " + document.error());
            return Error{"Cannot load prefab " + path + ": " + document.error()};
        }
        cached = impl_->prefabDocuments.emplace(path, std::move(document.value())).first;
    }
    auto spawned = spawn(cached->second, worldPosition, parent);
    if (!spawned && impl_->failedPrefabs.insert(path).second)
        log(LogLevel::Warning, "runtime", "Cannot spawn prefab " + path + ": " + spawned.error());
    return spawned;
}
void GameRuntime::requestRestart() {
    impl_->beginTransition(true, {});
}
void GameRuntime::requestSceneChange(std::string projectRelativePath) {
    impl_->beginTransition(false, std::move(projectRelativePath));
}
void GameRuntime::lockInput(const std::string &reason, bool locked) {
    if (locked)
        impl_->inputLocks.insert(reason);
    else
        impl_->inputLocks.erase(reason);
}
bool GameRuntime::inputLocked() const {
    return !impl_->inputLocks.empty() || impl_->phase == Impl::Phase::Out ||
           impl_->phase == Impl::Phase::Covered;
}
float GameRuntime::screenFade() const {
    return impl_->fade;
}
bool GameRuntime::transitioning() const {
    return impl_->phase != Impl::Phase::Idle;
}
} // namespace yk
