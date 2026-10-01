#include "yk/gameplay/Perception.hpp"
#include "yk/core/Log.hpp"
#include "yk/gameplay/Character.hpp"
#include "yk/gameplay/Navigation.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/sim/Factions.hpp"
#include "yk/sim/Identity.hpp"
#include "yk/sim/Zones.hpp"
#include "yk/stats/Stats.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
const char *awarenessName(AwarenessState state) {
    switch (state) {
    case AwarenessState::Unaware:
        return "unaware";
    case AwarenessState::Suspicious:
        return "suspicious";
    case AwarenessState::Aware:
        return "aware";
    }
    return "unaware";
}

namespace {
constexpr float stepInterval = 0.45F; // Seconds between footsteps.
constexpr float degreesToRadians = 3.14159265F / 180.0F;

NavigationService *navigation(GameContext &context) {
    auto *service = &context.services().get<NavigationService>();
    service->ensureBuilt(context);
    return service;
}

// How much of its range a subject's surroundings leave the eyes: dark zones shorten sight.
float lightFactor(GameContext &context, Vec2 at, int level) {
    if (auto *zones = context.services().find<ZoneService>())
        if (const Zone *zone = zones->zoneAt(context, at, level))
            return std::clamp(1.0F - 0.6F * static_cast<float>(zone->environmentValue("dark", 0.0)),
                              0.1F, 1.0F);
    return 1.0F;
}

float statusFactor(const Entity &entity, const char *key) {
    const auto *effects = entity.get<StatusEffects>();
    return effects ? static_cast<float>(effects->factor(key)) : 1.0F;
}

bool interesting(GameContext &context, const Perceiver &perceiver, const Entity &subject) {
    if (perceiver.interest == "any")
        return true;
    const Relation relation =
        relationBetween(context, perceiver.entity(), subject, !perceiver.seeThroughDisguise);
    if (perceiver.interest == "hostile")
        return relation == Relation::Hostile;
    return relation != Relation::Friendly;
}

struct SightResult {
    bool seen{false};
    float closeness{0.0F}; // 0 at the edge of its range, 1 right in front of it.
};

SightResult sight(GameContext &context, const Perceiver &perceiver, const Entity &subject) {
    const Entity &self = perceiver.entity();
    if (perceiver.blind || self.id() == subject.id())
        return {};
    const int level = levelOf(self);
    if (levelOf(subject) != level)
        return {};
    const Vec2 from = self.worldPosition(), to = subject.worldPosition();
    const Vec2 delta = to - from;
    const float distance = length(delta);
    float range = perceiver.sightRange * lightFactor(context, to, level);
    float visibility = 1.0F;
    bool hidden = false;
    float hiddenRange = 0.0F;
    if (const auto *tracks = subject.get<Perceivable>()) {
        visibility = tracks->visibility;
        hidden = tracks->hidden;
        hiddenRange = tracks->hiddenRange;
    }
    visibility *= statusFactor(subject, "visibility");
    if (const auto *effects = subject.get<StatusEffects>())
        if (effects->hasFlag("invisible"))
            visibility = 0.0F;
    range *= visibility;
    if (hidden)
        range = std::min(range, hiddenRange);
    const bool peripheral = distance <= std::min(perceiver.peripheralRange, std::max(range, 0.0F));
    if (distance > range)
        return {};
    if (!peripheral && distance > 1.0e-3F) {
        const float half = perceiver.fieldOfView * 0.5F * degreesToRadians;
        const float cosine = dot(normalized(delta), perceiver.facing());
        if (cosine < std::cos(half))
            return {};
    }
    if (auto *nav = navigation(context))
        if (nav->world().grid().configured() &&
            !nav->world().grid().lineOfSight(level, from, to).clear)
            return {};
    return {true, range > 0.0F ? 1.0F - distance / range : 1.0F};
}
} // namespace

// ---- Perceivable
void Perceivable::describe(TypeBuilder<Perceivable> &type) {
    type.category("Simulation")
        .description("How a character shows up to the others: how far it can be seen from, how "
                     "loud its footsteps are, whether it is hidden. Without it a character is "
                     "seen and heard at normal strength.");
    type.field("visibility", &Perceivable::visibility)
        .range(0, 4, 0.05)
        .tooltip("Multiplies the distance it can be seen from (a dark outfit, a crouch).");
    type.field("walkNoise", &Perceivable::walkNoise)
        .range(0, 50, 0.5)
        .tooltip("Meters its footsteps carry when it walks; 0 is silent.");
    type.field("runNoise", &Perceivable::runNoise).range(0, 50, 0.5);
    type.field("hidden", &Perceivable::hidden)
        .tooltip("Hidden (in a locker, under a bed): seen only from very close.");
    type.field("hiddenRange", &Perceivable::hiddenRange).range(0, 10, 0.1);
}

void Perceivable::onFixedUpdate(GameContext &context, float seconds) {
    const Vec2 now = entity().worldPosition();
    if (!primed_) {
        last_ = now;
        primed_ = true;
        return;
    }
    const float speed = seconds > 0.0F ? length(now - last_) / seconds : 0.0F;
    last_ = now;
    if (speed < 0.3F || hidden) {
        stepTimer_ = 0.0F;
        return;
    }
    stepTimer_ -= seconds;
    if (stepTimer_ > 0.0F)
        return;
    stepTimer_ = stepInterval;
    bool running = false;
    if (const auto *motor = entity().get<CharacterMotor>())
        running = motor->running();
    const float loud = (running ? runNoise : walkNoise) * statusFactor(entity(), "noise");
    if (loud > 0.0F)
        context.services().get<PerceptionService>().makeNoise(
            context, now, levelOf(entity()), loud, running ? "run" : "footstep", entity().id());
}

// ---- Perceiver
void Perceiver::describe(TypeBuilder<Perceiver> &type) {
    type.category("Simulation")
        .description("Lets a character see and hear. Seeing needs a clear line over the "
                     "navigation grid; sound is damped by what it crosses. Awareness builds while "
                     "a subject is in view and fades after it leaves; events report suspicious, "
                     "aware and lost.");
    type.field("sightRange", &Perceiver::sightRange).range(0, 100, 0.5);
    type.field("fieldOfView", &Perceiver::fieldOfView)
        .range(10, 360, 1)
        .tooltip("The whole cone in front, in degrees.");
    type.field("peripheralRange", &Perceiver::peripheralRange)
        .range(0, 20, 0.1)
        .tooltip("Meters it notices in every direction, behind it too.");
    type.field("hearingRange", &Perceiver::hearingRange).range(0, 100, 0.5);
    type.field("hearingSensitivity", &Perceiver::hearingSensitivity).range(0, 4, 0.05);
    type.field("noticeSeconds", &Perceiver::noticeSeconds)
        .range(0.05, 30, 0.05)
        .tooltip("Seconds to become aware of something at the edge of its range.");
    type.field("loseSeconds", &Perceiver::loseSeconds)
        .range(0.1, 120, 0.1)
        .tooltip("Seconds for awareness to fall from full to nothing once the subject is gone.");
    type.field("memorySeconds", &Perceiver::memorySeconds).range(0, 600, 1);
    type.field("suspiciousAt", &Perceiver::suspiciousAt).range(0.01, 1, 0.01);
    type.field("awareAt", &Perceiver::awareAt).range(0.01, 1, 0.01);
    type.field("lookInterval", &Perceiver::lookInterval).range(0.02, 2, 0.01);
    type.field("interest", &Perceiver::interest)
        .options({"any", "unfriendly", "hostile"})
        .tooltip("Who it keeps watch on: anyone, anyone who is not a friend, or only enemies.");
    type.field("seeThroughDisguise", &Perceiver::seeThroughDisguise);
    type.field("blind", &Perceiver::blind);
    type.field("deaf", &Perceiver::deaf);
    type.field("lookDirection", &Perceiver::lookDirection)
        .tooltip("Where it faces when it has no CharacterMotor (a camera).");
}

void Perceiver::onStart(GameContext &context) {
    context.services().get<PerceptionService>().add(entity().id());
    started_ = true;
    carry_ = static_cast<float>(entity().id().value % 10) * 0.01F; // Spread the looks out.
}
void Perceiver::onDestroy(GameContext &context) {
    if (auto *service = context.services().find<PerceptionService>())
        service->remove(entity().id());
}

Vec2 Perceiver::facing() const {
    if (const auto *motor = entity().get<CharacterMotor>())
        return motor->facing();
    return lengthSquared(lookDirection) > 1.0e-6F ? normalized(lookDirection) : Vec2{0.0F, 1.0F};
}

const Awareness *Perceiver::about(EntityId subject) const {
    for (const Awareness &entry : known_)
        if (entry.subject == subject)
            return &entry;
    return nullptr;
}
float Perceiver::awarenessOf(EntityId subject) const {
    const Awareness *found = about(subject);
    return found ? found->level : 0.0F;
}
AwarenessState Perceiver::stateOf(EntityId subject) const {
    const Awareness *found = about(subject);
    return found ? found->state : AwarenessState::Unaware;
}
bool Perceiver::sees(EntityId subject) const {
    const Awareness *found = about(subject);
    return found && found->seeing;
}
const Awareness *Perceiver::strongest(AwarenessState atLeast) const {
    const Awareness *best = nullptr;
    for (const Awareness &entry : known_)
        if (entry.state >= atLeast && (!best || entry.level > best->level))
            best = &entry;
    return best;
}
Awareness &Perceiver::entry(EntityId subject) {
    for (Awareness &found : known_)
        if (found.subject == subject)
            return found;
    known_.push_back({});
    known_.back().subject = subject;
    return known_.back();
}

void Perceiver::forget(GameContext &context, EntityId subject) {
    for (std::size_t i = 0; i < known_.size(); ++i)
        if (known_[i].subject == subject) {
            const AwarenessState before = known_[i].state;
            known_[i].level = 0.0F;
            known_[i].seeing = false;
            known_[i].state = AwarenessState::Unaware;
            classify(context, known_[i], before);
            known_.erase(known_.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
}
void Perceiver::forgetAll(GameContext &context) {
    const auto subjects = known_;
    for (const Awareness &found : subjects)
        forget(context, found.subject);
}

// Events for the change of state: suspicious, noticed (aware) and lost.
void Perceiver::classify(GameContext &context, Awareness &found, AwarenessState before) {
    if (found.state == before)
        return;
    const char *name = found.state == AwarenessState::Aware        ? "perception.noticed"
                       : found.state == AwarenessState::Suspicious ? "perception.suspicious"
                                                                   : "perception.lost";
    Json data = Json::object();
    data.set("sense", found.lastSense);
    data.set("level", static_cast<double>(found.level));
    data.set("state", awarenessName(found.state));
    data.set("x", static_cast<double>(found.lastKnown.x));
    data.set("y", static_cast<double>(found.lastKnown.y));
    context.events().emit(GameEvent(name, entity().id(), found.subject, std::move(data)));
}

void Perceiver::look(GameContext &context, float seconds) {
    auto &service = context.services().get<PerceptionService>();
    service.countLook();
    const double now = context.time();
    const Vec2 here = entity().worldPosition();
    const auto &actors = context.services().get<ActorService>().all();
    const float cull = std::max(sightRange, peripheralRange) + 0.5F;
    for (Awareness &found : known_)
        found.seeing = false;
    for (const EntityId id : actors) {
        if (id == entity().id())
            continue;
        const Entity *subject = context.scene().find(id);
        if (!subject || !subject->active())
            continue;
        const Vec2 there = subject->worldPosition();
        if (std::abs(there.x - here.x) > cull || std::abs(there.y - here.y) > cull)
            continue;
        if (!interesting(context, *this, *subject))
            continue;
        const SightResult result = sight(context, *this, *subject);
        if (!result.seen)
            continue;
        Awareness &found = entry(id);
        if (const auto *identity = subject->get<Identity>())
            found.who = identity->id;
        const AwarenessState before = found.state;
        found.seeing = true;
        found.lastKnown = there;
        found.lastLevel = levelOf(*subject);
        found.lastSensed = now;
        found.lastSense = "sight";
        found.level = std::min(1.0F, found.level + seconds * (1.0F + result.closeness) /
                                                       std::max(noticeSeconds, 0.05F));
        if (found.level >= awareAt)
            found.state = AwarenessState::Aware;
        else if (found.state == AwarenessState::Unaware && found.level >= suspiciousAt)
            found.state = AwarenessState::Suspicious;
        classify(context, found, before);
    }
    for (std::size_t i = 0; i < known_.size();) {
        Awareness &found = known_[i];
        const AwarenessState before = found.state;
        if (!found.seeing) {
            found.level = std::max(0.0F, found.level - seconds / std::max(loseSeconds, 0.1F));
            if (found.level < suspiciousAt)
                found.state = AwarenessState::Unaware;
        }
        classify(context, found, before);
        if (found.level <= 0.0F && !found.seeing && now - found.lastSensed > memorySeconds)
            known_.erase(known_.begin() + static_cast<std::ptrdiff_t>(i));
        else
            ++i;
    }
}

void Perceiver::onFixedUpdate(GameContext &context, float seconds) {
    carry_ += seconds;
    sinceLook_ += seconds;
    if (carry_ < lookInterval)
        return;
    const float elapsed = sinceLook_;
    carry_ = 0.0F;
    sinceLook_ = 0.0F;
    look(context, elapsed);
}

void Perceiver::hear(GameContext &context, const NoiseHeard &noise, float apparent) {
    if (deaf)
        return;
    lastNoise_ = noise;
    heardAny_ = true;
    if (static_cast<bool>(noise.source)) {
        const Entity *subject = context.scene().find(noise.source);
        if (subject && interesting(context, *this, *subject)) {
            Awareness &found = entry(noise.source);
            if (const auto *identity = subject->get<Identity>())
                found.who = identity->id;
            const AwarenessState before = found.state;
            const float bump = std::min(awareAt * 0.9F,
                                        suspiciousAt + 0.5F * (awareAt - suspiciousAt) *
                                                           std::clamp(apparent - 1.0F, 0.0F, 1.0F));
            if (!found.seeing) {
                found.lastKnown = noise.position;
                found.lastLevel = noise.level;
                found.lastSensed = noise.time;
                found.lastSense = "sound";
            }
            found.level = std::max(found.level, bump);
            if (found.state == AwarenessState::Unaware && found.level >= suspiciousAt)
                found.state = AwarenessState::Suspicious;
            classify(context, found, before);
        }
    }
    Json data = Json::object();
    data.set("kind", noise.kind);
    data.set("x", static_cast<double>(noise.position.x));
    data.set("y", static_cast<double>(noise.position.y));
    data.set("loudness", static_cast<double>(noise.loudness));
    context.events().emit(
        GameEvent("perception.heard", entity().id(), noise.source, std::move(data)));
}

Json Perceiver::saveState() const {
    Json state = Json::object();
    Json list = Json::array();
    for (const Awareness &found : known_) {
        Json item = Json::object();
        item.set("who", found.who);
        item.set("level", static_cast<double>(found.level));
        item.set("state", awarenessName(found.state));
        item.set("x", static_cast<double>(found.lastKnown.x));
        item.set("y", static_cast<double>(found.lastKnown.y));
        item.set("at", found.lastSensed);
        item.set("sense", found.lastSense);
        list.push(std::move(item));
    }
    state.set("known", std::move(list));
    return state;
}

Status Perceiver::loadState(GameContext &context, const Json &state) {
    const auto &actors = context.services().get<ActorService>();
    known_.clear();
    const Json &list = state.get("known");
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Json &item = list.at(i);
        Awareness found;
        found.who = item.get("who").asString();
        found.subject = actors.idOf(found.who);
        if (!found.subject)
            continue; // Not in this scene any more.
        found.level = static_cast<float>(item.get("level").asNumber(0.0));
        const std::string name = item.get("state").asString();
        found.state = name == "aware"        ? AwarenessState::Aware
                      : name == "suspicious" ? AwarenessState::Suspicious
                                             : AwarenessState::Unaware;
        found.lastKnown = {static_cast<float>(item.get("x").asNumber(0.0)),
                           static_cast<float>(item.get("y").asNumber(0.0))};
        found.lastSensed = item.get("at").asNumber(0.0);
        found.lastSense = item.get("sense").asString();
        known_.push_back(found);
    }
    return success();
}

// ---- Service
void PerceptionService::describe(std::vector<std::pair<std::string, std::string>> &rows) const {
    rows.push_back({"Perceivers", std::to_string(perceivers_.size())});
    rows.push_back({"Looks", std::to_string(looks_)});
    rows.push_back({"Noises", std::to_string(noises_)});
}
void PerceptionService::add(EntityId perceiver) {
    if (std::find(perceivers_.begin(), perceivers_.end(), perceiver) == perceivers_.end())
        perceivers_.push_back(perceiver);
}
void PerceptionService::remove(EntityId perceiver) {
    perceivers_.erase(std::remove(perceivers_.begin(), perceivers_.end(), perceiver),
                      perceivers_.end());
}

int PerceptionService::makeNoise(GameContext &context, Vec2 position, int level, float loudness,
                                 const std::string &kind, EntityId source) {
    if (loudness <= 0.0F)
        return 0;
    ++noises_;
    const WorldGrid *grid = nullptr;
    if (auto *nav = navigation(context))
        if (nav->world().grid().configured())
            grid = &nav->world().grid();
    int heard = 0;
    const auto listeners = perceivers_;
    for (const EntityId id : listeners) {
        if (id == source)
            continue;
        Entity *listener = context.scene().find(id);
        auto *ears = listener ? listener->get<Perceiver>() : nullptr;
        if (!ears || ears->deaf || levelOf(*listener) != level)
            continue;
        const Vec2 there = listener->worldPosition();
        const float distance = length(there - position);
        if (distance > ears->hearingRange)
            continue;
        float reach = loudness * ears->hearingSensitivity;
        if (const auto *zones = context.services().find<ZoneService>())
            if (const Zone *zone = zones->zoneAt(context, there, level))
                reach /= 1.0F + static_cast<float>(zone->environmentValue("noisy", 0.0));
        if (grid)
            reach *= grid->soundTransmission(level, position, there);
        if (distance > reach)
            continue;
        NoiseHeard noise;
        noise.position = position;
        noise.level = level;
        noise.time = context.time();
        noise.kind = kind;
        noise.source = source;
        noise.loudness = loudness;
        ears->hear(context, noise, distance > 1.0e-3F ? reach / distance : 4.0F);
        ++heard;
    }
    return heard;
}

bool canSee(GameContext &context, const Entity &perceiver, const Entity &subject) {
    const auto *eyes = perceiver.get<Perceiver>();
    return eyes && sight(context, *eyes, subject).seen;
}

// ---- Rules
void registerPerceptionRules(RuleCatalog &catalog) {
    using Kind = ParamSpec::Kind;
    const auto param = [](const char *n, Kind k, bool r = false, const char *d = "") {
        return ParamSpec::make(n, k, r, d);
    };
    catalog.addPredicate(
        {"CanSee",
         "Perception",
         "True when the entity (default the actor) can see the target right now.",
         {param("target", Kind::Entity, true),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             const Entity *who = context.resolveOne(
                 args.contains("entity") ? args.get("entity").asString() : "actor");
             const Entity *what = context.resolveOne(args.get("target").asString());
             return who && what && canSee(context.game, *who, *what);
         },
         nullptr});
    catalog.addPredicate(
        {"AwareOf",
         "Perception",
         "True when the entity (default the actor) has noticed the target: at least `state` "
         "(suspicious or aware, default aware).",
         {param("target", Kind::Entity, true), param("state", Kind::String),
          param("entity", Kind::Entity)},
         [](const Json &args, RuleContext &context) {
             const Entity *who = context.resolveOne(
                 args.contains("entity") ? args.get("entity").asString() : "actor");
             const Entity *what = context.resolveOne(args.get("target").asString());
             const auto *ears = who ? who->get<Perceiver>() : nullptr;
             if (!ears || !what)
                 return false;
             const AwarenessState wanted = args.get("state").asString() == "suspicious"
                                               ? AwarenessState::Suspicious
                                               : AwarenessState::Aware;
             return ears->stateOf(what->id()) >= wanted;
         },
         nullptr});
    catalog.addAction(
        {"MakeNoise",
         "Perception",
         "A sound at the entity's position (or x, y): loudness is how many meters it carries.",
         {param("loudness", Kind::Number, true), param("kind", Kind::String),
          param("x", Kind::Number), param("y", Kind::Number),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             const Entity *who = context.resolveOne(
                 args.contains("entity") ? args.get("entity").asString() : "actor");
             Vec2 at = who ? who->worldPosition() : Vec2{};
             if (args.contains("x") && args.contains("y"))
                 at = {static_cast<float>(args.get("x").asNumber()),
                       static_cast<float>(args.get("y").asNumber())};
             else if (!who)
                 return ActionResult::Failed;
             context.game.services().get<PerceptionService>().makeNoise(
                 context.game, at, who ? levelOf(*who) : 0,
                 static_cast<float>(args.get("loudness").asNumber()),
                 args.contains("kind") ? args.get("kind").asString() : std::string("noise"),
                 who ? who->id() : EntityId{});
             return ActionResult::Done;
         },
         nullptr});
    catalog.addAction(
        {"ForgetSubject",
         "Perception",
         "The entity forgets the target (or everyone without one).",
         {param("target", Kind::Entity), param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             Entity *who = context.resolveOne(
                 args.contains("entity") ? args.get("entity").asString() : "actor");
             auto *ears = who ? who->get<Perceiver>() : nullptr;
             if (!ears)
                 return ActionResult::Failed;
             if (args.contains("target")) {
                 const Entity *what = context.resolveOne(args.get("target").asString());
                 if (!what)
                     return ActionResult::Failed;
                 ears->forget(context.game, what->id());
             } else {
                 ears->forgetAll(context.game);
             }
             return ActionResult::Done;
         },
         nullptr});
}

void registerPerceptionComponents(ComponentRegistry &registry) {
    registerPerceptionRules(registry.extend<RuleCatalog>());
    registry.add<Perceivable>("Perceivable");
    registry.add<Perceiver>("Perceiver");
}
} // namespace yk
