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
        options.viewportSize.x <= 0 || options.viewportSize.y <= 0)
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
    return runtime;
}

Status GameRuntime::Impl::rebuild(std::unique_ptr<Scene> fresh) {
    scene = std::move(fresh);
    blackboard.clear();
    events.clear();
    started.clear();
    destroyQueue.clear();
    time = 0;
    ticks = 0;
    accumulator = 0;
    restartWanted = false;
    tickInput = InputFrame{};
    actions = ActionInput(options.inputMap);
    return buildWorld();
}

void GameRuntime::Impl::shutdown() {
    if (!scene)
        return;
    forEachComponent([&](Component &component) {
        if (started.contains(&component))
            component.onDestroy(self);
    });
    started.clear();
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

void GameRuntime::Impl::fixedTick() {
    input.fill(tickInput);
    actions.update(tickInput);
    syncActivation();
    startPending();
    const float step = static_cast<float>(options.fixedSeconds);
    forEachComponent([&](Component &component) { component.onFixedUpdate(self, step); });
    if (auto advanced = world->advance(options.fixedSeconds); !advanced)
        log(LogLevel::Error, "physics", advanced.error());
    syncTransforms();
    updateTriggers();
    dispatchCollisions();
    flushDestroys();
    events.dispatch();
    ++ticks;
    time = static_cast<double>(ticks) * options.fixedSeconds;
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

void GameRuntime::update(double frameSeconds, const InputFrame &frameInput) {
    auto &state = *impl_;
    if (state.paused)
        return;
    state.input.feed(frameInput);
    if (!std::isfinite(frameSeconds) || frameSeconds < 0)
        frameSeconds = 0;
    const double step = state.options.fixedSeconds;
    state.accumulator += std::min(frameSeconds, 0.25);
    unsigned steps = 0;
    while (state.accumulator + 1e-9 >= step && steps < state.options.maxStepsPerFrame) {
        state.fixedTick();
        state.accumulator -= step;
        ++steps;
    }
    if (state.accumulator + 1e-9 >= step) // Too far behind: drop the backlog instead of spiraling.
        state.accumulator = std::fmod(state.accumulator, step);
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
    state.variableUpdate(static_cast<float>(state.options.fixedSeconds));
    state.finishFrame();
}

const std::string &GameRuntime::sceneChangeRequested() const {
    return impl_->sceneChange;
}
void GameRuntime::clearSceneChangeRequest() {
    impl_->sceneChange.clear();
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
Blackboard &GameRuntime::blackboard() {
    return impl_->blackboard;
}
EventBus &GameRuntime::events() {
    return impl_->events;
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
void GameRuntime::requestRestart() {
    impl_->restartWanted = true;
}
void GameRuntime::requestSceneChange(std::string projectRelativePath) {
    impl_->sceneChange = std::move(projectRelativePath);
}
} // namespace yk
