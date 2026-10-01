#include "yk/sim/Sequence.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/runtime/GameContext.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
namespace {
using Kind = SequenceCue::Kind;
constexpr double tolerance = 1e-6;

bool isPoint(const Json &json) {
    return json.isArray() && json.size() == 2 && json.at(0).isNumber() && json.at(1).isNumber();
}
Vec2 pointOf(const Json &json) {
    if (!isPoint(json))
        return {};
    return {static_cast<float>(json.at(0).asNumber()), static_cast<float>(json.at(1).asNumber())};
}

float shaped(const std::string &ease, float t) {
    t = std::clamp(t, 0.0F, 1.0F);
    if (ease == "smooth")
        return t * t * (3.0F - 2.0F * t);
    if (ease == "in")
        return t * t;
    if (ease == "out")
        return 1.0F - (1.0F - t) * (1.0F - t);
    return t;
}

std::string timeText(double seconds) {
    std::string text = std::to_string(seconds);
    while (text.size() > 1 && text.back() == '0')
        text.pop_back();
    if (!text.empty() && text.back() == '.')
        text.pop_back();
    return text;
}

// Moves an entity (and its body) to a point without announcing a jump: the drawn position
// interpolates from where it was, so a walk looks like one. A body keeps no velocity from before.
void put(GameContext &context, Entity &entity, Vec2 where) {
    entity.setWorldPosition(where);
    const auto body = context.bodyOf(entity.id());
    if (!body)
        return;
    const Transform2D world = entity.worldTransform();
    context.physics().setPose(*body, {world.position, degreesToRadians(world.rotationDegrees)});
    const RigidBody *rigid = entity.get<RigidBody>();
    if (rigid && rigid->type != RigidBodyType::Static)
        context.physics().setVelocity(*body, {}, 0.0F);
}

Result<SequenceCue> parseCue(const Json &json, std::size_t position,
                             std::vector<std::string> &warnings) {
    const std::string where = "cue " + std::to_string(position);
    if (!json.isObject())
        return Error{where + " must be an object"};
    SequenceCue cue;
    cue.number = position;
    cue.args = json;
    cue.type = json.get("type").asString();
    if (cue.type.empty())
        return Error{where + " needs a 'type'"};
    const std::string label = where + " ('" + cue.type + "')";
    std::string problem;
    const auto fail = [&](const std::string &message) {
        if (problem.empty())
            problem = label + ": " + message;
    };
    // A number argument that may be absent; below `low` it counts as `low`.
    const auto numberOf = [&](const char *key, double fallback, double low = 0.0) -> double {
        const Json *value = json.find(key);
        if (!value)
            return fallback;
        if (!value->isNumber() || !std::isfinite(value->asNumber())) {
            fail(std::string("'") + key + "' must be a number");
            return fallback;
        }
        if (value->asNumber() < low) {
            warnings.push_back(label + ": '" + key + "' is below " + timeText(low) +
                               "; it counts as " + timeText(low));
            return low;
        }
        return value->asNumber();
    };
    const auto easeOk = [&] {
        if (!json.contains("ease"))
            return;
        const auto &names = sequenceEaseNames();
        if (!json.get("ease").isString() ||
            std::find(names.begin(), names.end(), json.get("ease").asString()) == names.end())
            fail("'ease' must be one of linear, smooth, in, out");
    };
    const auto needEntity = [&](const char *key) {
        if (!json.get(key).isString() || json.get(key).asString().empty())
            fail(std::string("needs '") + key + "' (name:Guard, tag:guard, self, actor...)");
    };

    cue.time = numberOf("time", 0.0);
    if (json.contains("wait") && !json.get("wait").isBool())
        fail("'wait' must be true or false");
    cue.wait = json.get("wait").asBool(false);

    if (cue.type == "Wait") {
        cue.kind = Kind::Wait;
        cue.wait = true;
        if (!json.contains("seconds"))
            fail("needs 'seconds'");
        numberOf("seconds", 0.0);
    } else if (cue.type == "WaitForEvent") {
        cue.kind = Kind::WaitForEvent;
        cue.wait = true;
        auto trigger = EventTrigger::fromJson(json);
        if (!trigger)
            return Error{label + ": " + trigger.error()};
        cue.trigger = std::move(trigger.value());
        numberOf("timeout", 0.0);
    } else if (cue.type == "Fade") {
        cue.kind = Kind::Fade;
        if (!json.get("to").isNumber())
            fail("needs 'to' (0 clear, 1 black)");
        else if (json.get("to").asNumber() < 0.0 || json.get("to").asNumber() > 1.0)
            warnings.push_back(label + ": 'to' is outside 0..1; it is clamped");
        numberOf("seconds", 0.0);
        easeOk();
    } else if (cue.type == "CameraMove") {
        cue.kind = Kind::CameraMove;
        if (json.contains("entity"))
            needEntity("entity");
        else if (!isPoint(json.get("to")))
            fail("needs 'to' ([x, y]) or an 'entity' to go to");
        numberOf("seconds", 0.0);
        numberOf("height", 0.0);
        easeOk();
    } else if (cue.type == "CameraRelease") {
        cue.kind = Kind::CameraRelease;
    } else if (cue.type == "MoveEntity") {
        cue.kind = Kind::MoveEntity;
        needEntity("entity");
        if (json.contains("toEntity"))
            needEntity("toEntity");
        else if (!isPoint(json.get("to")))
            fail("needs 'to' ([x, y]) or 'toEntity'");
        if (!json.contains("seconds") && !json.contains("speed"))
            fail("needs 'seconds' or 'speed'");
        numberOf("seconds", 0.0);
        if (json.contains("speed") &&
            !(json.get("speed").isNumber() && json.get("speed").asNumber() > 0.0))
            fail("'speed' must be above 0");
        easeOk();
    } else {
        auto action = Action::fromJson(json);
        if (!action)
            return Error{label + ": " + action.error()};
        cue.kind = Kind::Action;
        cue.action.push_back(std::move(action.value()));
    }
    if (!problem.empty())
        return Error{problem};
    if (cue.wait && ((cue.kind == Kind::Action && cue.type != "StartDialogue") ||
                     cue.kind == Kind::CameraRelease))
        warnings.push_back(label + ": 'wait' has no effect here (it holds the sequence for a "
                                   "conversation, a fade, a camera move or a walk)");
    return cue;
}
} // namespace

const std::vector<std::string> &sequenceCueTypes() {
    static const std::vector<std::string> names = {"Wait",       "WaitForEvent",  "Fade",
                                                   "CameraMove", "CameraRelease", "MoveEntity"};
    return names;
}
const std::vector<std::string> &sequenceEaseNames() {
    static const std::vector<std::string> names = {"linear", "smooth", "in", "out"};
    return names;
}

// ---- The definition
// ------------------------------------------------------------------------------
Result<SequenceDefinition> SequenceDefinition::fromJson(const Json &json,
                                                        std::vector<std::string> &warnings) {
    if (!json.isObject())
        return Error{"a sequence is an object with a list of cues"};
    SequenceDefinition definition;
    definition.lockInput = json.get("lockInput").asBool(true);
    definition.skippable = json.get("skippable").asBool(true);
    if (!json.get("cues").isArray())
        return Error{"a sequence needs a 'cues' list"};
    const Json &list = json.get("cues");
    for (std::size_t i = 0; i < list.size(); ++i) {
        auto cue = parseCue(list.at(i), i + 1, warnings);
        if (!cue)
            return Error{cue.error()};
        definition.cues.push_back(std::move(cue.value()));
    }
    if (definition.cues.empty())
        warnings.push_back("the sequence has no cues");
    std::stable_sort(
        definition.cues.begin(), definition.cues.end(),
        [](const SequenceCue &left, const SequenceCue &right) { return left.time < right.time; });
    return definition;
}

Json SequenceDefinition::toJson() const {
    Json json = Json::object();
    json.set("lockInput", lockInput);
    json.set("skippable", skippable);
    Json list = Json::array();
    for (const SequenceCue &cue : cues) {
        Json item = cue.args;
        item.set("time", cue.time);
        list.push(std::move(item));
    }
    json.set("cues", std::move(list));
    return json;
}

double SequenceDefinition::length() const {
    return cues.empty() ? 0.0 : cues.back().time;
}

void SequenceDefinition::visitRules(const std::string &file, const RuleSourceVisitor &visit) const {
    for (std::size_t i = 0; i < cues.size(); ++i)
        if (cues[i].kind == Kind::Action)
            visit({file,
                   "cue " + std::to_string(cues[i].number) + " '" + cues[i].type + "' at " +
                       timeText(cues[i].time) + " s",
                   nullptr, &cues[i].action});
}

// ---- The player
// ----------------------------------------------------------------------------------
void SequencePlayer::describe(TypeBuilder<SequencePlayer> &type) {
    type.category("Cutscenes")
        .description("Plays a .ykseq sequence: a timeline of cues (fades, camera moves, walks, "
                     "conversations, any rule action). Start it with a rule (PlaySequence) or on "
                     "start.")
        .updatePhase(UpdatePhase::PreUpdate);
    type.field("sequence", &SequencePlayer::sequence)
        .asset("sequence")
        .tooltip("The .ykseq file to play.");
    type.field("playOnStart", &SequencePlayer::playOnStart)
        .tooltip("Play it when the scene starts (an intro).");
    type.field("skipSet", &SequencePlayer::skipSet).inputSet();
    type.field("skipAction", &SequencePlayer::skipAction)
        .inputAction()
        .tooltip("A named action that skips the sequence; empty: the player cannot skip it.");
    type.check([](const Entity &, const SequencePlayer &player, const CheckContext &,
                  std::vector<std::string> &problems) {
        if (player.playOnStart && player.sequence.path.empty())
            problems.push_back("plays on start but names no sequence");
    });
}

std::string SequencePlayer::lockName() const {
    return "sequence:" + toString(entity().id());
}

RuleContext SequencePlayer::rules(GameContext &context) const {
    RuleContext rc(context);
    rc.self = entity().id();
    rc.actor = actor_;
    rc.origin = "sequence '" + source_ + "' of '" + entity().name() + "'";
    return rc;
}

Camera *SequencePlayer::camera(GameContext &context) const {
    if (heldCamera_)
        if (Entity *holder = context.scene().find(heldCamera_))
            if (Camera *held = holder->get<Camera>())
                return held;
    Camera *fallback = nullptr;
    for (const EntityId id : context.scene().orderedIds()) {
        Entity *candidate = context.scene().find(id);
        if (!candidate || !candidate->activeInHierarchy())
            continue;
        for (Camera *cam : candidate->getAll<Camera>()) {
            if (!cam->enabled)
                continue;
            if (cam->primary)
                return cam;
            if (!fallback)
                fallback = cam;
        }
    }
    return fallback;
}

void SequencePlayer::releaseCamera(GameContext &context) {
    std::erase_if(timed_, [](const Timed &timed) { return timed.kind == Timed::Kind::CameraMove; });
    follow_ = {};
    if (!heldCamera_)
        return;
    if (Entity *holder = context.scene().find(heldCamera_))
        if (Camera *held = holder->get<Camera>())
            held->release();
    heldCamera_ = {};
}

// While nothing moves the camera, it keeps looking at what the last CameraMove named.
void SequencePlayer::trackCamera(GameContext &context) {
    if (!follow_)
        return;
    for (const Timed &timed : timed_)
        if (timed.kind == Timed::Kind::CameraMove)
            return;
    const Entity *followed = context.scene().find(follow_);
    Camera *cam = camera(context);
    if (!followed || !cam) {
        follow_ = {};
        return;
    }
    cam->hold(followed->worldPosition(), followHeight_);
}

void SequencePlayer::onStart(GameContext &context) {
    if (playOnStart)
        play(context);
}

void SequencePlayer::onDestroy(GameContext &context) {
    stop(context);
}

void SequencePlayer::onFixedUpdate(GameContext &context, float seconds) {
    if (!playing_)
        return;
    if (definition_.skippable && !skipAction.empty() &&
        context.input().state(skipSet, skipAction).pressed) {
        skip(context);
        return;
    }
    step(context, seconds);
}

bool SequencePlayer::play(GameContext &context, EntityId actor) {
    if (playing_)
        return false;
    if (sequence.path.empty() || !context.assets()) {
        log(LogLevel::Warning, "sequence", "'" + entity().name() + "' has no sequence to play");
        return false;
    }
    auto text = context.assets()->readText(sequence.path);
    auto document = text ? Json::parse(text.value()) : Result<Json>(Error{text.error()});
    if (!document) {
        log(LogLevel::Warning, "sequence",
            "Cannot load sequence " + sequence.path + ": " + document.error());
        return false;
    }
    std::vector<std::string> warnings;
    auto parsed = SequenceDefinition::fromJson(document.value(), warnings);
    if (!parsed) {
        log(LogLevel::Warning, "sequence",
            "Cannot play sequence " + sequence.path + ": " + parsed.error());
        return false;
    }
    for (const std::string &warning : warnings)
        log(LogLevel::Warning, "sequence", sequence.path + ": " + warning);
    definition_ = std::move(parsed.value());
    source_ = sequence.path;
    actor_ = actor;
    timed_.clear();
    next_ = 0;
    clock_ = 0.0;
    follow_ = {};
    heldCamera_ = {};
    startFade_ = context.cinematicFade();
    playing_ = true;
    if (definition_.lockInput)
        context.lockInput(lockName(), true);
    const bool listens =
        std::any_of(definition_.cues.begin(), definition_.cues.end(),
                    [](const SequenceCue &cue) { return cue.kind == Kind::WaitForEvent; });
    if (listens)
        subscription_ = context.events().subscribe(
            EventBus::anyEvent, [this, &context](const GameEvent &event) {
                for (Timed &timed : timed_)
                    if (timed.kind == Timed::Kind::WaitForEvent && !timed.done &&
                        timed.trigger.matches(context, entity().id(), event))
                        timed.done = true;
            });
    Json data = Json::object();
    data.set("sequence", source_);
    context.events().emit(GameEvent("sequence.started", entity().id(), actor_, std::move(data)));
    step(context, 0.0F); // What is due at time zero happens now, not a tick later.
    return true;
}

void SequencePlayer::skip(GameContext &context) {
    if (!playing_ || ending_)
        return;
    ending_ = true;
    for (Timed &timed : timed_)
        conclude(context, timed, true);
    timed_.clear();
    while (next_ < definition_.cues.size())
        begin(context, definition_.cues[next_++], true);
    ending_ = false;
    finish(context, true, false);
}

void SequencePlayer::stop(GameContext &context) {
    if (!playing_ || ending_)
        return;
    for (Timed &timed : timed_)
        if (timed.kind == Timed::Kind::Dialogue)
            conclude(context, timed, true);
    context.setCinematicFade(startFade_);
    finish(context, false, true);
}

void SequencePlayer::finish(GameContext &context, bool skipped, bool stopped) {
    playing_ = false;
    timed_.clear();
    if (subscription_ != 0) {
        context.events().unsubscribe(subscription_);
        subscription_ = 0;
    }
    releaseCamera(context);
    if (definition_.lockInput)
        context.lockInput(lockName(), false);
    Json data = Json::object();
    data.set("sequence", source_);
    data.set("skipped", skipped);
    if (stopped)
        data.set("stopped", true);
    context.events().emit(GameEvent("sequence.finished", entity().id(), actor_, std::move(data)));
}

// ---- One tick
// ------------------------------------------------------------------------------------
void SequencePlayer::step(GameContext &context, float seconds) {
    const auto holding = [this] {
        return std::any_of(timed_.begin(), timed_.end(),
                           [](const Timed &timed) { return timed.blocking; });
    };
    for (std::size_t i = 0; i < timed_.size();) {
        advance(context, timed_[i], seconds);
        if (!timed_[i].done) {
            ++i;
            continue;
        }
        conclude(context, timed_[i], false);
        timed_.erase(timed_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    trackCamera(context);
    if (!holding())
        clock_ += seconds;
    while (playing_ && next_ < definition_.cues.size() &&
           definition_.cues[next_].time <= clock_ + tolerance && !holding()) {
        begin(context, definition_.cues[next_++], false);
        if (!playing_) // A cue stopped the sequence (or skipped it).
            return;
    }
    if (playing_ && next_ >= definition_.cues.size() && timed_.empty())
        finish(context, false, false);
}

void SequencePlayer::advance(GameContext &context, Timed &timed, float seconds) {
    timed.elapsed += seconds;
    const bool over = timed.elapsed >= timed.duration - tolerance;
    const float t = timed.duration > 0.0
                        ? static_cast<float>(std::clamp(timed.elapsed / timed.duration, 0.0, 1.0))
                        : 1.0F;
    switch (timed.kind) {
    case Timed::Kind::Wait:
        timed.done = over;
        break;
    case Timed::Kind::WaitForEvent: // Done when the event is heard, or when the time is up.
        if (timed.duration > 0.0 && over)
            timed.done = true;
        break;
    case Timed::Kind::Fade:
        context.setCinematicFade(lerp(timed.fromValue, timed.toValue, shaped(timed.ease, t)));
        timed.done = over;
        break;
    case Timed::Kind::CameraMove: {
        Camera *cam = camera(context);
        if (!cam) {
            timed.done = true;
            break;
        }
        if (const Entity *goal = timed.entity ? context.scene().find(timed.entity) : nullptr)
            timed.to = goal->worldPosition();
        const float e = shaped(timed.ease, t);
        cam->hold(lerp(timed.from, timed.to, e), lerp(timed.fromValue, timed.toValue, e));
        timed.done = over;
        break;
    }
    case Timed::Kind::MoveEntity: {
        Entity *mover = context.scene().find(timed.entity);
        if (!mover) {
            timed.done = true;
            break;
        }
        put(context, *mover, lerp(timed.from, timed.to, shaped(timed.ease, t)));
        timed.done = over;
        break;
    }
    case Timed::Kind::Dialogue: {
        const Entity *talker = context.scene().find(timed.entity);
        const auto *dialogue = talker ? talker->get<Dialogue>() : nullptr;
        timed.done = !dialogue || !dialogue->active();
        break;
    }
    }
}

// What a cue leaves behind when it has finished (or is cut short): its end state.
void SequencePlayer::conclude(GameContext &context, Timed &timed, bool skipped) {
    switch (timed.kind) {
    case Timed::Kind::Fade:
        context.setCinematicFade(timed.toValue);
        break;
    case Timed::Kind::CameraMove:
        if (Camera *cam = camera(context)) {
            if (const Entity *goal = timed.entity ? context.scene().find(timed.entity) : nullptr)
                timed.to = goal->worldPosition();
            cam->hold(timed.to, timed.toValue, skipped);
        }
        break;
    case Timed::Kind::MoveEntity:
        if (skipped)
            if (Entity *mover = context.scene().find(timed.entity))
                put(context, *mover, timed.to);
        break;
    case Timed::Kind::Dialogue:
        if (skipped)
            if (Entity *talker = context.scene().find(timed.entity))
                if (auto *dialogue = talker->get<Dialogue>(); dialogue && dialogue->active())
                    dialogue->close(context);
        break;
    case Timed::Kind::Wait:
    case Timed::Kind::WaitForEvent:
        break;
    }
}

void SequencePlayer::begin(GameContext &context, const SequenceCue &cue, bool skipping) {
    RuleContext rc = rules(context);
    const Json &args = cue.args;
    const auto complain = [&](const std::string &what) {
        log(LogLevel::Warning, "sequence",
            rc.origin + ": cue " + std::to_string(cue.number) + " (" + cue.type + "): " + what);
    };
    const auto easeOf = [&](const char *fallback) {
        return args.contains("ease") ? args.get("ease").asString() : std::string(fallback);
    };
    const auto secondsOf = [&] {
        return skipping ? 0.0 : std::max(0.0, args.get("seconds").asNumber(0.0));
    };
    switch (cue.kind) {
    case Kind::Action: {
        if (skipping && cue.type == "StartDialogue")
            break; // Nobody wants a conversation after skipping the scene it belongs to.
        execute(cue.action, rc);
        if (skipping || !cue.wait || cue.type != "StartDialogue")
            break;
        Entity *talker = rc.resolveOne(args.contains("entity") ? args.get("entity").asString()
                                                               : std::string("target"));
        if (!talker)
            talker = rc.selfEntity();
        const auto *dialogue = talker ? talker->get<Dialogue>() : nullptr;
        if (dialogue && dialogue->active()) {
            Timed timed;
            timed.kind = Timed::Kind::Dialogue;
            timed.entity = talker->id();
            timed.blocking = true;
            timed_.push_back(std::move(timed));
        }
        break;
    }
    case Kind::Wait: {
        const double seconds = std::max(0.0, args.get("seconds").asNumber(0.0));
        if (skipping || seconds <= 0.0)
            break;
        Timed timed;
        timed.kind = Timed::Kind::Wait;
        timed.duration = seconds;
        timed.blocking = true;
        timed_.push_back(std::move(timed));
        break;
    }
    case Kind::WaitForEvent: {
        if (skipping)
            break;
        Timed timed;
        timed.kind = Timed::Kind::WaitForEvent;
        timed.trigger = cue.trigger;
        timed.duration = std::max(0.0, args.get("timeout").asNumber(0.0));
        timed.blocking = true;
        timed_.push_back(std::move(timed));
        break;
    }
    case Kind::Fade: {
        const float to = std::clamp(static_cast<float>(args.get("to").asNumber()), 0.0F, 1.0F);
        const double seconds = secondsOf();
        std::erase_if(timed_, [](const Timed &timed) { return timed.kind == Timed::Kind::Fade; });
        if (seconds <= 0.0) {
            context.setCinematicFade(to);
            break;
        }
        Timed timed;
        timed.kind = Timed::Kind::Fade;
        timed.fromValue = context.cinematicFade();
        timed.toValue = to;
        timed.duration = seconds;
        timed.ease = easeOf("linear");
        timed.blocking = cue.wait;
        timed_.push_back(std::move(timed));
        break;
    }
    case Kind::CameraMove: {
        Camera *cam = camera(context);
        if (!cam) {
            complain("the scene has no camera");
            break;
        }
        const Entity *goal = nullptr;
        Vec2 to = pointOf(args.get("to"));
        if (args.contains("entity")) {
            goal = rc.resolveOne(args.get("entity").asString());
            if (!goal) {
                complain("there is no entity '" + args.get("entity").asString() + "'");
                break;
            }
            to = goal->worldPosition();
        }
        const CameraView current = cam->view();
        const float asked = static_cast<float>(std::max(0.0, args.get("height").asNumber(0.0)));
        const float height = asked > 0.0F ? asked : current.visibleHeight;
        const double seconds = secondsOf();
        std::erase_if(timed_,
                      [](const Timed &timed) { return timed.kind == Timed::Kind::CameraMove; });
        heldCamera_ = cam->entity().id();
        follow_ = goal ? goal->id() : EntityId{};
        followHeight_ = height;
        if (seconds <= 0.0) {
            cam->hold(to, height, true);
            break;
        }
        cam->hold(current.position,
                  current.visibleHeight); // Stay put until the next tick moves it.
        Timed timed;
        timed.kind = Timed::Kind::CameraMove;
        timed.entity = follow_;
        timed.from = current.position;
        timed.to = to;
        timed.fromValue = current.visibleHeight;
        timed.toValue = height;
        timed.duration = seconds;
        timed.ease = easeOf("smooth");
        timed.blocking = cue.wait;
        timed_.push_back(std::move(timed));
        break;
    }
    case Kind::CameraRelease:
        releaseCamera(context);
        break;
    case Kind::MoveEntity: {
        Entity *mover = rc.resolveOne(args.get("entity").asString());
        if (!mover) {
            complain("there is no entity '" + args.get("entity").asString() + "' to move");
            break;
        }
        Vec2 to = pointOf(args.get("to"));
        if (args.contains("toEntity")) {
            const Entity *goal = rc.resolveOne(args.get("toEntity").asString());
            if (!goal) {
                complain("there is no entity '" + args.get("toEntity").asString() + "' to go to");
                break;
            }
            to = goal->worldPosition();
        }
        const Vec2 from = mover->worldPosition();
        const double seconds =
            skipping ? 0.0
            : args.contains("seconds")
                ? std::max(0.0, args.get("seconds").asNumber(0.0))
                : distance(from, to) / std::max(1e-6, args.get("speed").asNumber(1.0));
        std::erase_if(timed_, [&](const Timed &timed) {
            return timed.kind == Timed::Kind::MoveEntity && timed.entity == mover->id();
        });
        if (seconds <= 0.0) {
            put(context, *mover, to);
            break;
        }
        Timed timed;
        timed.kind = Timed::Kind::MoveEntity;
        timed.entity = mover->id();
        timed.from = from;
        timed.to = to;
        timed.duration = seconds;
        timed.ease = easeOf("linear");
        timed.blocking = cue.wait;
        timed_.push_back(std::move(timed));
        break;
    }
    }
}

// ---- Rules
// ---------------------------------------------------------------------------------------
void registerSequenceRules(RuleCatalog &catalog) {
    using ParamKind = ParamSpec::Kind;
    const auto param = [](const char *name, ParamKind kind, bool required = false,
                          const char *description = "") {
        return ParamSpec::make(name, kind, required, description);
    };
    catalog.addAction(
        {"PlaySequence",
         "Cutscenes",
         "Plays the sequence of an entity (its SequencePlayer), or another .ykseq file on it.",
         {param("entity", ParamKind::Entity, false, "default self"),
          param("sequence", ParamKind::String, false, "A .ykseq file; default the player's own")},
         [](const Json &args, RuleContext &context) {
             bool done = false;
             for (Entity *entity : context.entitiesFrom(args, "entity", "self"))
                 if (auto *player = entity->get<SequencePlayer>()) {
                     if (args.contains("sequence"))
                         player->sequence.path = args.get("sequence").asString();
                     done = player->play(context.game, context.actor) || done;
                 }
             return done ? ActionResult::Done : ActionResult::Failed;
         },
         nullptr});
    catalog.addAction({"SkipSequence",
                       "Cutscenes",
                       "Jumps a playing sequence to its end.",
                       {param("entity", ParamKind::Entity, false, "default self")},
                       [](const Json &args, RuleContext &context) {
                           bool done = false;
                           for (Entity *entity : context.entitiesFrom(args, "entity", "self"))
                               if (auto *player = entity->get<SequencePlayer>();
                                   player && player->playing()) {
                                   player->skip(context.game);
                                   done = true;
                               }
                           return done ? ActionResult::Done : ActionResult::Failed;
                       },
                       nullptr});
    catalog.addAction({"StopSequence",
                       "Cutscenes",
                       "Ends a playing sequence where it stands (the camera, input and screen "
                       "are given back).",
                       {param("entity", ParamKind::Entity, false, "default self")},
                       [](const Json &args, RuleContext &context) {
                           bool done = false;
                           for (Entity *entity : context.entitiesFrom(args, "entity", "self"))
                               if (auto *player = entity->get<SequencePlayer>();
                                   player && player->playing()) {
                                   player->stop(context.game);
                                   done = true;
                               }
                           return done ? ActionResult::Done : ActionResult::Failed;
                       },
                       nullptr});
    catalog.addPredicate({"SequencePlaying",
                          "Cutscenes",
                          "True while the entity's sequence is playing.",
                          {param("entity", ParamKind::Entity, false, "default self")},
                          [](const Json &args, RuleContext &context) {
                              for (Entity *entity : context.entitiesFrom(args, "entity", "self"))
                                  if (const auto *player = entity->get<SequencePlayer>();
                                      player && player->playing())
                                      return true;
                              return false;
                          },
                          nullptr});
    catalog.addAction(
        {"TriggerAnimation",
         "Animation",
         "Fires a trigger on an entity's animation controller (a wave, a flinch, a gesture).",
         {param("entity", ParamKind::Entity, false, "default self"),
          param("trigger", ParamKind::String, true)},
         [](const Json &args, RuleContext &context) {
             bool done = false;
             for (Entity *entity : context.entitiesFrom(args, "entity", "self"))
                 for (AnimatedSprite *animated : entity->getAll<AnimatedSprite>()) {
                     animated->trigger(args.get("trigger").asString());
                     done = true;
                 }
             return done ? ActionResult::Done : ActionResult::Failed;
         },
         nullptr});
    catalog.addAction(
        {"PlayAnimation",
         "Animation",
         "Plays a clip of an entity's animation (ignored when the clip does not exist).",
         {param("entity", ParamKind::Entity, false, "default self"),
          param("clip", ParamKind::String, true)},
         [](const Json &args, RuleContext &context) {
             bool done = false;
             for (Entity *entity : context.entitiesFrom(args, "entity", "self"))
                 for (AnimatedSprite *animated : entity->getAll<AnimatedSprite>()) {
                     animated->play(args.get("clip").asString());
                     done = true;
                 }
             return done ? ActionResult::Done : ActionResult::Failed;
         },
         nullptr});
}

void registerSequenceComponents(ComponentRegistry &registry) {
    registerSequenceRules(registry.extend<RuleCatalog>());
    registry.add<SequencePlayer>("SequencePlayer");
}
} // namespace yk
