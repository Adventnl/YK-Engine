#include "yk/gameplay/Exploration.hpp"
#include "yk/data/GameData.hpp"
#include "yk/gameplay/Character.hpp"
#include "yk/items/Crafting.hpp"
#include "yk/items/Inventory.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace yk {
namespace {
bool clearReach(GameContext &context, const Entity &actor, const Entity &target) {
    // Feet-anchored actors place their transform on the collider boundary.
    // Cast from inside the actual shape so an NPC cannot obstruct its own sight.
    const auto *collider = actor.get<Collider>();
    const Vec2 start = transformPoint(actor.worldTransform(), collider ? collider->offset : Vec2{});
    physics::QueryFilter filter;
    filter.categoryBits = context.layers().categoryBits("Player");
    filter.maskBits = context.layers().categoryBits("Solid");
    const auto hit = context.physics().rayCast(start, target.worldPosition() - start, filter);
    return hit && (!hit.value() || context.entityOfShape(hit.value()->shape) == &target);
}
} // namespace

void TopDownController::describe(TypeBuilder<TopDownController> &type) {
    type.category("Exploration")
        .description("Four-way, normalized diagonal movement on a zero-gravity body.")
        .dependsOn("PlayerInput")
        .dependsOn("RigidBody")
        .dependsOn("Collider")
        .onAdd([](Entity &entity, TopDownController &) {
            detail::configureWalker(entity, layers::player);
        });
    type.field("leftAction", &TopDownController::leftAction).inputAction();
    type.field("rightAction", &TopDownController::rightAction).inputAction();
    type.field("upAction", &TopDownController::upAction).inputAction();
    type.field("downAction", &TopDownController::downAction).inputAction();
    type.field("speed", &TopDownController::speed).range(0, 50, 0.1);
    type.field("acceleration", &TopDownController::acceleration).range(0, 300, 1);
}
void TopDownController::onFixedUpdate(GameContext &context, float seconds) {
    const auto *input = entity().get<PlayerInput>();
    const auto body = context.bodyOf(entity().id());
    if (!input || !body)
        return;
    if (context.inputLocked()) {
        direction_ = {};
        context.physics().setVelocity(*body, {});
        if (context.blackboard().number("search_ui_busy") == 0 &&
            context.blackboard().number("wire_ui_active") == 0)
            detail::animateDirection(entity(), {}, facing_, animation_);
        else
            animation_.clear();
        return;
    }
    Vec2 direction{input->axis(context, leftAction, rightAction),
                   input->axis(context, upAction, downAction)};
    if (lengthSquared(direction) > 1.0F)
        direction = normalized(direction);
    direction_ = direction;
    // Facing follows intent even when a wall prevents translation.
    if (lengthSquared(direction) > .001F)
        facing_ = std::abs(direction.x) > std::abs(direction.y)
                      ? (direction.x < 0 ? "left" : "right")
                      : (direction.y < 0 ? "up" : "down");
    const auto state = context.physics().state(*body);
    if (!state)
        return;
    const Vec2 target = direction * std::max(0.0F, speed);
    Vec2 delta = target - state.value().linearVelocity;
    const float limit = std::max(0.0F, acceleration) * seconds;
    if (length(delta) > limit)
        delta = normalized(delta) * limit;
    context.physics().setVelocity(
        *body, lengthSquared(direction) > .001F ? state.value().linearVelocity + delta : Vec2{});
}
void TopDownController::onUpdate(GameContext &context, float) {
    // Animation sees the resolved physics velocity. Holding a key into a wall
    // must not play a running cycle; searches keep control of their own clips.
    if (context.blackboard().number("search_ui_busy") != 0 ||
        context.blackboard().number("wire_ui_active") != 0)
        return;
    Vec2 motion{};
    if (!context.inputLocked() && lengthSquared(direction_) > .001F)
        if (const auto body = context.bodyOf(entity().id()))
            if (const auto state = context.physics().state(*body);
                state && lengthSquared(state.value().linearVelocity) > .01F)
                motion = state.value().linearVelocity;
    detail::animateDirection(entity(), motion, facing_, animation_);
}

void NpcPath::describe(TypeBuilder<NpcPath> &type) {
    type.category("Exploration")
        .description("Moves an NPC between waypoint entities, or stands still.")
        .dependsOn("RigidBody")
        .dependsOn("Collider")
        .onAdd([](Entity &entity, NpcPath &) { detail::configureWalker(entity, layers::prop); });
    type.field("waypoints", &NpcPath::waypoints);
    type.field("speed", &NpcPath::speed).range(0, 30, 0.1);
    type.field("waitSeconds", &NpcPath::waitSeconds).range(0, 30, 0.1);
    type.field("loop", &NpcPath::loop);
    type.field("requiredFlag", &NpcPath::requiredFlag);
}
void NpcPath::onFixedUpdate(GameContext &context, float seconds) {
    const auto body = context.bodyOf(entity().id());
    if (!body)
        return;
    Vec2 motion{};
    const auto *dialogue = entity().get<Dialogue>();
    const bool talking = dialogue && dialogue->active();
    const bool worldMoving = !context.inputLocked() ||
                             context.blackboard().number("search_ui_busy") != 0 ||
                             context.blackboard().number("wire_ui_active") != 0 ||
                             context.blackboard().number("dialogue_active") != 0;
    if (wait_ > 0)
        wait_ = std::max(0.0F, wait_ - seconds);
    else if (!talking && worldMoving &&
             (requiredFlag.empty() || context.blackboard().number(requiredFlag) != 0.0) &&
             !waypoints.empty() && next_ < waypoints.size()) {
        if (const Entity *waypoint = context.scene().find(waypoints[next_])) {
            const Vec2 delta = waypoint->worldPosition() - entity().worldPosition();
            if (length(delta) < 0.18F) {
                ++next_;
                if (loop && next_ >= waypoints.size())
                    next_ = 0;
                wait_ = waitSeconds;
            } else
                motion = normalized(delta);
        } else
            ++next_;
    }
    context.physics().setVelocity(*body, motion * std::max(0.0F, speed));
    detail::animateDirection(entity(), motion, facing_, animation_);
}

void Interactable::describe(TypeBuilder<Interactable> &type) {
    type.category("Exploration")
        .description("Proximity action with condition, state, dialogue, gate and portal hooks.");
    type.field("prompt", &Interactable::prompt);
    type.field("range", &Interactable::range).range(0.1, 20, 0.1);
    type.field("requiredFlag", &Interactable::requiredFlag);
    type.field("setFlag", &Interactable::setFlag);
    type.field("once", &Interactable::once);
    type.field("hideWhenUsed", &Interactable::hideWhenUsed);
    type.field("event", &Interactable::event);
}
bool Interactable::available(GameContext &context) const {
    const bool alreadyUsed =
        once && (used_ || (!setFlag.empty() && context.blackboard().number(setFlag) != 0.0));
    return enabled && entity().activeInHierarchy() && !alreadyUsed &&
           (requiredFlag.empty() || context.blackboard().number(requiredFlag) != 0.0);
}
void Interactable::onStart(GameContext &context) {
    if (once && hideWhenUsed && !setFlag.empty() && context.blackboard().number(setFlag) != 0.0)
        entity().setActive(false);
}
void Interactable::activate(GameContext &context, Entity &actor) {
    if (!available(context))
        return;
    auto *gate = entity().get<StateGate>();
    if (auto *search = entity().get<SearchAction>(); search && (!gate || gate->locked(context))) {
        search->begin(context, actor);
        return;
    }
    if (auto *puzzle = entity().get<WirePuzzle>()) {
        if (context.blackboard().number(puzzle->successFlag) == 0.0) {
            puzzle->begin(context, actor.id());
            return;
        }
    }
    if (gate) {
        if (gate->locked(context)) {
            context.blackboard().set("interaction_notice", "LOCKED - " + prompt);
            context.blackboard().set("interaction_notice_until", context.time() + 3.0);
            context.emit("gate_locked", entity().id(), actor.id());
            return;
        }
        gate->toggle(context);
    }
    if (auto *dialogue = entity().get<Dialogue>())
        dialogue->begin(context, actor.id());
    if (!setFlag.empty()) {
        context.blackboard().set(setFlag, 1.0);
        context.blackboard().keep(setFlag);
    }
    if (!event.empty())
        context.emit(event, entity().id(), actor.id());
    if (auto *portal = entity().get<MapPortal>())
        portal->enter(context, actor);
    used_ = true;
    context.emit("interacted", entity().id(), actor.id());
    if (hideWhenUsed)
        entity().setActive(false);
}

void Interactor::describe(TypeBuilder<Interactor> &type) {
    type.category("Exploration")
        .description("Uses the nearest available interactive object and publishes its prompt.")
        .dependsOn("PlayerInput");
    type.field("action", &Interactor::action).inputAction();
}
void Interactor::onFixedUpdate(GameContext &context, float) {
    // A popup may release input before this actor updates. Its closing key belongs to the
    // popup, even when the interactive object's entity precedes the actor in scene order.
    const bool locked = context.inputLocked();
    const bool justUnlocked = inputWasLocked_ && !locked;
    inputWasLocked_ = locked;
    const auto *input = entity().get<PlayerInput>();
    if (!input)
        return;
    Interactable *closest = nullptr;
    float nearest = std::numeric_limits<float>::max();
    if (!context.inputLocked())
        for (const EntityId id : context.scene().orderedIds()) {
            Entity *object = context.scene().find(id);
            if (!object || object == &entity())
                continue;
            auto *candidate = object->get<Interactable>();
            if (!candidate || !candidate->available(context))
                continue;
            const float gap = distance(entity().worldPosition(), object->worldPosition());
            if (gap <= candidate->range && gap < nearest &&
                clearReach(context, entity(), *object)) {
                closest = candidate;
                nearest = gap;
            }
        }
    std::string prompt = closest ? closest->prompt : "";
    if (closest)
        if (auto *gate = closest->entity().get<StateGate>(); gate && !gate->locked(context))
            prompt = std::string("E - ") + (gate->open() ? "CLOSE " : "OPEN ") +
                     closest->entity().name();
    context.blackboard().set("interaction_prompt", prompt);
    if (closest && !justUnlocked && input->button(context, action).pressed)
        closest->activate(context, entity());
}

void StateGate::describe(TypeBuilder<StateGate> &type) {
    type.category("Exploration")
        .description("Openable solid barrier with optional state lock and persistence.")
        .dependsOn("Collider")
        .onAdd([](Entity &entity, StateGate &) {
            if (auto *collider = entity.get<Collider>()) {
                collider->isTrigger = false;
                if (collider->layer == "Default")
                    collider->layer = layers::solid;
                if (collider->size == Vec2{1.0F, 1.0F})
                    if (auto *sprite = entity.get<SpriteRenderer>())
                        collider->size = sprite->size;
            }
        });
    type.field("startsOpen", &StateGate::startsOpen);
    type.field("startsLocked", &StateGate::startsLocked);
    type.field("unlockFlag", &StateGate::unlockFlag);
    type.field("persistFlag", &StateGate::persistFlag);
    type.field("animationSeconds", &StateGate::animationSeconds).range(0, 10, 0.05);
    type.field("openOffset", &StateGate::openOffset)
        .tooltip("Distance the visible door slides; zero uses its collider's long axis.");
    type.field("stopWhenBlocked", &StateGate::stopWhenBlocked);
    type.field("proximityRange", &StateGate::proximityRange)
        .range(0, 20, 0.1)
        .tooltip("Zero uses manual operation. Positive opens for nearby players and NPC paths.");
    type.field("closeDelay", &StateGate::closeDelay)
        .range(0, 30, 0.1)
        .tooltip("Seconds an automatic door stays open after the last actor leaves.");
    type.field("openFrames", &StateGate::openFrames)
        .range(0, 256)
        .tooltip("Optional horizontal strip from closed to open; frames follow the collision "
                 "animation and reverse when closing.");
    type.field("logic", &StateGate::logic).options(signalLogicNames());
    type.field("invert", &StateGate::invert);
}
void StateGate::onStart(GameContext &context) {
    open_ = !persistFlag.empty() && context.blackboard().has(persistFlag)
                ? context.blackboard().number(persistFlag) != 0
                : startsOpen;
    amount_ = open_ ? 1.0F : 0.0F;
    for (Collider *collider : entity().getAll<Collider>())
        if (!collider->isTrigger)
            collider->enabled = !open_;
    if (auto *sprite = entity().get<SpriteRenderer>()) {
        baseOffset_ = sprite->offset;
        slide_ = openOffset;
        if (slide_ == Vec2{}) {
            const auto *collider = entity().get<Collider>();
            const Vec2 size = collider ? collider->size : sprite->size;
            slide_ = size.x >= size.y ? Vec2{size.x + .15F, 0} : Vec2{0, -size.y - .15F};
        }
        sprite->offset = baseOffset_ + slide_ * amount_;
        sprite->visible = true;
    }
    updateVisual();
}
bool StateGate::locked(GameContext &context) const {
    return startsLocked && (unlockFlag.empty() || context.blackboard().number(unlockFlag) == 0.0);
}
void StateGate::toggle(GameContext &context) {
    if (locked(context))
        return;
    setOpen(context, !open_);
}
void StateGate::setOpen(GameContext &context, bool value) {
    if (open_ == value)
        return;
    open_ = value;
    if (!persistFlag.empty()) {
        context.blackboard().set(persistFlag, open_ ? 1.0 : 0.0);
        context.blackboard().keep(persistFlag);
    }
    context.emit(open_ ? "gate_opened" : "gate_closed", entity().id());
}
void StateGate::onFixedUpdate(GameContext &context, float seconds) {
    if (proximityRange > 0.0F && !locked(context)) {
        bool nearby = false;
        for (EntityId id : context.scene().orderedIds()) {
            const auto *actor = context.scene().find(id);
            if (actor && actor->activeInHierarchy() &&
                (actor->has<PlayerInput>() || actor->has<NpcPath>()) &&
                distance(actor->worldPosition(), entity().worldPosition()) <= proximityRange) {
                nearby = true;
                break;
            }
        }
        if (nearby) {
            closeTimer_ = std::max(0.0F, closeDelay);
            setOpen(context, true);
        } else {
            closeTimer_ = std::max(0.0F, closeTimer_ - seconds);
            if (closeTimer_ <= 0.0F)
                setOpen(context, false);
        }
    }
    const bool signal = signalActive() && !locked(context);
    if (signal && !signalWasActive_ && !open_) {
        open_ = true;
        if (!persistFlag.empty()) {
            context.blackboard().set(persistFlag, 1.0);
            context.blackboard().keep(persistFlag);
        }
        context.emit("gate_opened", entity().id());
    }
    signalWasActive_ = signal;
    // Never materialize closed collision over someone in the doorway.
    if (!open_ && amount_ > 0 && stopWhenBlocked) {
        for (const auto *collider : entity().getAll<Collider>()) {
            if (collider->isTrigger)
                continue;
            const auto transform = entity().worldTransform();
            const Vec2 size = hadamard(collider->size,
                                       {std::abs(transform.scale.x), std::abs(transform.scale.y)});
            const float angle = degreesToRadians(transform.rotationDegrees);
            const Vec2 extent{
                std::abs(std::cos(angle)) * size.x + std::abs(std::sin(angle)) * size.y,
                std::abs(std::sin(angle)) * size.x + std::abs(std::cos(angle)) * size.y};
            physics::QueryFilter filter;
            filter.categoryBits = UINT64_MAX;
            filter.maskBits =
                context.layers().categoryBits("Player") | context.layers().categoryBits("Prop");
            auto hits = context.physics().queryAabb(
                {transformPoint(transform, collider->offset) - extent * .5F, extent}, filter);
            if (hits)
                for (auto shape : hits.value())
                    if (auto *occupant = context.entityOfShape(shape);
                        occupant && occupant != &entity())
                        return;
        }
    }
    const float target = open_ ? 1.0F : 0.0F;
    const float step = animationSeconds > 0 ? seconds / animationSeconds : 1.0F;
    if (amount_ < target)
        amount_ = std::min(target, amount_ + step);
    else if (amount_ > target)
        amount_ = std::max(target, amount_ - step);
    if (auto *animation = entity().get<AnimatedSprite>())
        animation->setBool("open", open_);
    updateVisual();
    for (Collider *collider : entity().getAll<Collider>())
        if (!collider->isTrigger)
            collider->enabled = amount_ < 0.95F;
}
void StateGate::updateVisual() {
    if (auto *sprite = entity().get<SpriteRenderer>()) {
        const float eased = amount_ * amount_ * (3.0F - 2.0F * amount_);
        sprite->visible = true;
        if (openFrames > 1) {
            sprite->offset = baseOffset_;
            sprite->columns = openFrames;
            sprite->rows = 1;
            sprite->frame =
                static_cast<int>(std::lround(eased * static_cast<float>(openFrames - 1)));
        } else
            sprite->offset = baseOffset_ + slide_ * eased;
    }
}

void MapPortal::describe(TypeBuilder<MapPortal> &type) {
    type.category("Exploration")
        .description("Transfers an actor to a named spawn in another scene.");
    type.field("destination", &MapPortal::destination).asset("scene");
    type.field("spawn", &MapPortal::spawn);
    type.field("requiredFlag", &MapPortal::requiredFlag);
    type.field("onTouch", &MapPortal::onTouch);
    type.field("activatorTags", &MapPortal::activatorTags);
}
void MapPortal::enter(GameContext &context, Entity &actor) {
    if (destination.path.empty() ||
        (!requiredFlag.empty() && context.blackboard().number(requiredFlag) == 0.0) ||
        !matchesActivator(actor, activatorTags))
        return;
    context.blackboard().set("__map_spawn", spawn);
    context.blackboard().keep("__map_spawn");
    context.emit("map_exit", entity().id(), actor.id());
    context.requestSceneChange(destination.path);
}
void MapPortal::onTriggerEnter(GameContext &context, Entity &other) {
    if (onTouch)
        enter(context, other);
}

void MapSpawn::describe(TypeBuilder<MapSpawn> &type) {
    type.category("Exploration").description("Entry point selected by a map portal.");
    type.field("name", &MapSpawn::name);
    type.field("actorTag", &MapSpawn::actorTag);
}
void MapSpawn::onStart(GameContext &context) {
    if (context.blackboard().text("__map_spawn") != name)
        return;
    for (const EntityId id : context.scene().orderedIds()) {
        Entity *actor = context.scene().find(id);
        if (actor && (actorTag.empty() || actor->hasTag(actorTag)) && actor->has<PlayerInput>()) {
            context.teleport(*actor, entity().worldPosition());
            context.emit("map_enter", entity().id(), actor->id());
            break;
        }
    }
}

void CharacterMotion::describe(TypeBuilder<CharacterMotion> &type) {
    type.category("Exploration")
        .dependsOn("SpriteRenderer")
        .description("Breathing, step bounce and facing for single-pose pixel characters.");
    type.field("stepHeight", &CharacterMotion::stepHeight).range(0, 0.3, 0.01);
    type.field("idleHeight", &CharacterMotion::idleHeight).range(0, 0.2, 0.005);
    type.field("animateSteps", &CharacterMotion::animateSteps);
    type.field("flipWithMovement", &CharacterMotion::flipWithMovement);
}
void CharacterMotion::onStart(GameContext &) {
    phase_ = entity().worldPosition().x * 1.7F + entity().worldPosition().y * .9F;
    if (const auto *sprite = entity().get<SpriteRenderer>()) {
        offset_ = sprite->offset;
        size_ = sprite->size;
    }
}
void CharacterMotion::onFixedUpdate(GameContext &context, float seconds) {
    auto *sprite = entity().get<SpriteRenderer>();
    if (!sprite)
        return;
    Vec2 velocity{};
    if (auto body = context.bodyOf(entity().id()))
        if (auto state = context.physics().state(*body))
            velocity = state.value().linearVelocity;
    const bool walking = lengthSquared(velocity) > 0.04F;
    phase_ += seconds * (walking ? 14.0F : 2.5F);
    const float bounce = walking ? std::abs(std::sin(phase_)) * stepHeight
                                 : (std::sin(phase_) + 1.0F) * idleHeight * .5F;
    sprite->offset =
        offset_ + Vec2{walking && animateSteps ? std::sin(phase_) * 0.012F : 0.0F, -bounce};
    sprite->size = {size_.x, size_.y + bounce * 0.3F};
    sprite->stepSwing = walking && animateSteps ? std::sin(phase_) * stepHeight * 0.75F : 0.0F;
    if (flipWithMovement && std::abs(velocity.x) > 0.1F)
        sprite->flipX = velocity.x < 0;
}

void LocationArea::describe(TypeBuilder<LocationArea> &type) {
    type.category("Exploration")
        .description("Publishes the current room name inside authored bounds.");
    type.field("actor", &LocationArea::actor);
    type.field("title", &LocationArea::title);
    type.field("boundsMin", &LocationArea::boundsMin);
    type.field("boundsMax", &LocationArea::boundsMax);
}
void LocationArea::onFixedUpdate(GameContext &context, float) {
    if (const auto *person = context.scene().find(actor);
        person && contains({boundsMin, boundsMax - boundsMin}, person->worldPosition()))
        context.blackboard().set("current_room", title);
}

void ObjectiveList::describe(TypeBuilder<ObjectiveList> &type) {
    type.category("Exploration")
        .description("Publishes the next unfinished objective and progress.");
    type.field("flags", &ObjectiveList::flags);
    type.field("instructions", &ObjectiveList::instructions);
    type.field("completeText", &ObjectiveList::completeText);
}
void ObjectiveList::onFixedUpdate(GameContext &context, float) {
    std::string next = completeText;
    int done = 0;
    bool found = false;
    for (std::size_t i = 0; i < flags.size(); ++i) {
        if (context.blackboard().number(flags[i]) != 0)
            ++done;
        else if (!found) {
            if (i < instructions.size())
                next = instructions[i];
            found = true;
        }
    }
    context.blackboard().set("objective", next);
    context.blackboard().set("task_progress",
                             std::to_string(done) + "/" + std::to_string(flags.size()));
}

void SearchAction::describe(TypeBuilder<SearchAction> &type) {
    type.category("Exploration")
        .description("Timed, cancellable container search or recipe crafting with a result popup.");
    type.field("title", &SearchAction::title);
    type.field("verb", &SearchAction::verb);
    type.field("successText", &SearchAction::successText);
    type.field("emptyText", &SearchAction::emptyText);
    type.field("lockedText", &SearchAction::lockedText);
    type.field("successFlag", &SearchAction::successFlag);
    type.field("recipe", &SearchAction::recipe).ref("recipe");
    type.field("requiredItems", &SearchAction::requiredItems);
    type.field("duration", &SearchAction::duration).range(0.1, 30, 0.1);
}
void SearchAction::result(GameContext &context, const std::string &message) {
    busy_ = false;
    context.blackboard().set("search_ui_busy", 0.0);
    context.blackboard().set("search_ui_message", message);
}
void SearchAction::begin(GameContext &context, Entity &actor) {
    if (active_ || context.inputLocked())
        return;
    actor_ = actor.id();
    active_ = busy_ = true;
    elapsed_ = 0;
    startedTick_ = context.tick();
    auto &board = context.blackboard();
    board.set("search_ui_active", 1.0);
    board.set("search_ui_busy", 1.0);
    board.set("search_ui_title", title);
    board.set("search_ui_message", verb);
    board.set("search_ui_progress", 0.0);
    board.set("search_ui_icon", "");
    context.lockInput("search:" + toString(entity().id()), true);
    const auto *inventory = actor.get<Inventory>();
    for (const auto &item : requiredItems)
        if (!inventory || !inventory->has(ItemId(item))) {
            result(context,
                   lockedText.empty() ? "LOCKED - find the required tool first." : lockedText);
            return;
        }
    if (!successFlag.empty() && board.number(successFlag) != 0) {
        result(context, emptyText);
        return;
    }
    if (auto *container = entity().get<Container>()) {
        std::string why;
        if (!container->open(context, actor)) {
            container->canOpen(context, actor, &why);
            result(context, lockedText.empty() ? why : lockedText);
            return;
        }
    }
    if (!recipe.empty()) {
        auto *crafter = actor.get<Crafter>();
        const auto check = crafter ? crafter->check(context, recipe) : CraftResult{};
        if (!check.ok) {
            result(context, check.reason.empty() ? "You cannot craft this yet." : check.reason);
            return;
        }
    }
    if (const auto body = context.bodyOf(actor.id()))
        context.physics().setVelocity(*body, {});
    const Vec2 delta = entity().worldPosition() - actor.worldPosition();
    const std::string facing = std::abs(delta.x) > std::abs(delta.y)
                                   ? (delta.x < 0 ? "left" : "right")
                                   : (delta.y < 0 ? "up" : "down");
    if (auto *animation = actor.get<AnimatedSprite>())
        animation->play("search_" + facing);
    context.emit("search_started", entity().id(), actor_);
}
void SearchAction::close(GameContext &context) {
    if (auto *actor = context.scene().find(actor_))
        if (auto *container = entity().get<Container>())
            container->close(context, *actor);
    active_ = busy_ = false;
    context.blackboard().set("search_ui_active", 0.0);
    context.blackboard().set("search_ui_busy", 0.0);
    context.lockInput("search:" + toString(entity().id()), false);
}
void SearchAction::onFixedUpdate(GameContext &context, float seconds) {
    if (!active_ || context.tick() == startedTick_)
        return;
    Entity *actor = context.scene().find(actor_);
    if (!actor || distance(actor->worldPosition(), entity().worldPosition()) > 2.3F ||
        context.input().state("Global", "Cancel").pressed) {
        close(context);
        return;
    }
    if (!busy_) {
        if (context.input().state("Player1", "Interact").pressed)
            close(context);
        return;
    }
    elapsed_ += seconds;
    context.blackboard().set("search_ui_progress", static_cast<double>(std::min(
                                                       1.0F, elapsed_ / std::max(duration, 0.1F))));
    if (elapsed_ < duration)
        return;
    std::string icon;
    if (auto *container = entity().get<Container>()) {
        container->search(context, *actor);
        auto *contents = entity().get<Inventory>();
        bool taken = false;
        if (contents)
            for (int i = 0; i < contents->size(); ++i) {
                const auto *definition =
                    gameData(context).items.items.find(contents->slot(i).item.str());
                if (definition)
                    icon = definition->icon;
                taken = container->take(context, *actor, i) > 0 || taken;
            }
        if (!taken) {
            result(context, emptyText);
            return;
        }
    }
    if (!recipe.empty()) {
        const auto made = actor->get<Crafter>()->craft(context, recipe);
        if (!made.ok) {
            result(context, made.reason);
            return;
        }
        if (!made.outputs.empty())
            if (const auto *definition =
                    gameData(context).items.items.find(made.outputs.front().item.str()))
                icon = definition->icon;
    }
    if (!successFlag.empty()) {
        context.blackboard().set(successFlag, 1.0);
        context.blackboard().keep(successFlag);
    }
    if (auto *gate = entity().get<StateGate>(); gate && !gate->locked(context) && !gate->open())
        gate->toggle(context);
    context.blackboard().set("search_ui_icon", icon);
    result(context, successText);
    context.emit("search_completed", entity().id(), actor_);
}
void SearchAction::onDestroy(GameContext &context) {
    if (active_)
        close(context);
}

void InventoryHud::describe(TypeBuilder<InventoryHud> &type) {
    type.category("Exploration").description("A configurable objective and inventory HUD.");
    type.field("actor", &InventoryHud::actor);
    type.field("title", &InventoryHud::title);
    type.field("completionTitle", &InventoryHud::completionTitle);
    type.field("completionText", &InventoryHud::completionText);
    type.field("statusText", &InventoryHud::statusText);
    type.field("alertText", &InventoryHud::alertText);
    type.field("controlsText", &InventoryHud::controlsText);
    type.field("continueText", &InventoryHud::continueText);
    type.field("restartText", &InventoryHud::restartText);
}
void InventoryHud::onFixedUpdate(GameContext &context, float) {
    auto &board = context.blackboard();
    board.set("inventory_hud", 1.0);
    board.set("inventory_title", title);
    board.set("inventory_completion_title", completionTitle);
    board.set("inventory_completion_text", completionText);
    board.set("inventory_status", statusText);
    board.set("inventory_alert", alertText);
    board.set("inventory_controls", controlsText);
    board.set("inventory_continue", continueText);
    board.set("inventory_restart", restartText);
    const Entity *person = context.scene().find(actor);
    const auto *inventory = person ? person->get<Inventory>() : nullptr;
    for (int i = 0; i < 6; ++i) {
        const std::string prefix = "inventory_slot_" + std::to_string(i);
        const auto *definition =
            inventory ? gameData(context).items.items.find(inventory->slot(i).item.str()) : nullptr;
        board.set(prefix + "_icon", definition ? definition->icon : "");
        board.set(prefix + "_name", definition ? definition->displayName : "");
    }
    if (context.time() > board.number("interaction_notice_until"))
        board.set("interaction_notice", "");
}
void PatrolWatch::describe(TypeBuilder<PatrolWatch> &type) {
    type.category("Exploration")
        .description("A guard notices trespassing in an authored restricted area and returns the "
                     "actor to an authored position.");
    type.field("actor", &PatrolWatch::actor);
    type.field("caughtText", &PatrolWatch::caughtText);
    type.field("restrictedMin", &PatrolWatch::restrictedMin);
    type.field("restrictedMax", &PatrolWatch::restrictedMax);
    type.field("returnPosition", &PatrolWatch::returnPosition);
    type.field("range", &PatrolWatch::range).range(0, 20, 0.1);
    type.field("catchSeconds", &PatrolWatch::catchSeconds).range(0.1, 20, 0.1);
}
void PatrolWatch::onFixedUpdate(GameContext &context, float seconds) {
    if (context.blackboard().number("dialogue_active") != 0)
        return;
    Entity *person = context.scene().find(actor);
    if (!person || context.blackboard().text("level_state") == "complete")
        return;
    cooldown_ = std::max(0.0F, cooldown_ - seconds);
    const Vec2 at = person->worldPosition();
    const bool seen = cooldown_ == 0 && at.x > restrictedMin.x && at.x < restrictedMax.x &&
                      at.y > restrictedMin.y && at.y < restrictedMax.y &&
                      distance(at, entity().worldPosition()) < range &&
                      clearReach(context, entity(), *person);
    suspicion_ = std::clamp(suspicion_ + seconds * (seen ? 1.0F : -2.0F), 0.0F, catchSeconds);
    context.blackboard().set("patrol_suspicion", static_cast<double>(suspicion_ / catchSeconds));
    if (suspicion_ >= catchSeconds) {
        context.teleport(*person, returnPosition);
        suspicion_ = 0;
        cooldown_ = 4;
        context.blackboard().set("interaction_notice", caughtText);
        context.blackboard().set("interaction_notice_until", context.time() + 4.0);
        context.emit("guard_caught", entity().id(), person->id());
    }
}

void WirePuzzle::describe(TypeBuilder<WirePuzzle> &type) {
    type.category("Exploration")
        .description("Four-wire matching popup; locks movement until closed.");
    type.field("requiredFlags", &WirePuzzle::requiredFlags);
    type.field("successFlag", &WirePuzzle::successFlag);
    type.field("missingText", &WirePuzzle::missingText);
    type.field("inputSet", &WirePuzzle::inputSet).inputSet();
}
void WirePuzzle::begin(GameContext &context, EntityId actor) {
    if (active_ || context.inputLocked())
        return;
    ready_ = true;
    for (const auto &flag : requiredFlags)
        ready_ = ready_ && context.blackboard().number(flag) != 0;
    active_ = true;
    source_ = target_ = connected_ = 0;
    message_ = ready_ ? "Match labels and colors. Wrong connections can be retried." : missingText;
    startedTick_ = context.tick();
    context.blackboard().set("wire_ui_active", 1.0);
    context.blackboard().set("wire_ui_ready", ready_ ? 1.0 : 0.0);
    context.blackboard().set("wire_ui_message", message_);
    context.blackboard().set("wire_ui_source", 0.0);
    context.blackboard().set("wire_ui_target", 0.0);
    context.blackboard().set("wire_ui_connected", 0.0);
    context.lockInput("wires:" + toString(entity().id()), true);
    if (auto *person = context.scene().find(actor)) {
        const Vec2 delta = entity().worldPosition() - person->worldPosition();
        const std::string facing = std::abs(delta.x) > std::abs(delta.y)
                                       ? (delta.x < 0 ? "left" : "right")
                                       : (delta.y < 0 ? "up" : "down");
        if (auto *animation = person->get<AnimatedSprite>())
            animation->play("search_" + facing);
    }
    context.emit("wire_puzzle_started", entity().id(), actor);
}
void WirePuzzle::close(GameContext &context) {
    active_ = false;
    context.blackboard().set("wire_ui_active", 0.0);
    context.lockInput("wires:" + toString(entity().id()), false);
}
void WirePuzzle::onFixedUpdate(GameContext &context, float) {
    if (!active_ || context.tick() == startedTick_)
        return;
    const auto &input = context.input();
    if (input.state("Global", "Cancel").pressed) {
        close(context);
        return;
    }
    if (!ready_) {
        if (input.state(inputSet, "Interact").pressed)
            close(context);
        return;
    }
    if (input.state(inputSet, "MoveUp").pressed)
        source_ = (source_ + 3) % 4;
    if (input.state(inputSet, "MoveDown").pressed)
        source_ = (source_ + 1) % 4;
    if (input.state(inputSet, "MoveLeft").pressed)
        target_ = (target_ + 3) % 4;
    if (input.state(inputSet, "MoveRight").pressed)
        target_ = (target_ + 1) % 4;
    context.blackboard().set("wire_ui_source", static_cast<double>(source_));
    context.blackboard().set("wire_ui_target", static_cast<double>(target_));
    if (!input.state(inputSet, "Interact").pressed)
        return;
    if (target_ != targetFor(source_)) {
        message_ = "Wrong terminal. Match the letter and color, then press E.";
        context.blackboard().set("wire_ui_message", message_);
        return;
    }
    connected_ |= 1 << source_;
    message_ = "Connection secured.";
    context.blackboard().set("wire_ui_message", message_);
    context.blackboard().set("wire_ui_connected", static_cast<double>(connected_));
    if (connected_ == 15) {
        context.blackboard().set(successFlag, 1.0);
        context.blackboard().keep(successFlag);
        context.emit("wire_puzzle_solved", entity().id());
        close(context);
    } else {
        do {
            source_ = (source_ + 1) % 4;
        } while (connected_ & (1 << source_));
        context.blackboard().set("wire_ui_source", static_cast<double>(source_));
    }
}
void WirePuzzle::onDestroy(GameContext &context) {
    if (active_)
        close(context);
}

void registerExplorationComponents(ComponentRegistry &registry) {
    registry.add<LocationArea>("LocationArea");
    registry.add<SearchAction>("SearchAction");
    registry.add<InventoryHud>("InventoryHud");
    registry.add<PatrolWatch>("PatrolWatch");
    registry.add<CharacterMotion>("CharacterMotion");
    registry.add<ObjectiveList>("ObjectiveList");
    registry.add<WirePuzzle>("WirePuzzle");
    registry.add<TopDownController>("TopDownController");
    registry.add<NpcPath>("NpcPath");
    registry.add<Interactor>("Interactor");
    registry.add<Interactable>("Interactable");
    registry.add<StateGate>("StateGate");
    registry.add<MapPortal>("MapPortal");
    registry.add<MapSpawn>("MapSpawn");
    registry.addTemplate({"Top-Down Player", "Exploration", [](Scene &scene, Vec2 at) {
                              Entity &e = scene.createEntity("Player");
                              e.setWorldPosition(at);
                              e.addTag("player");
                              auto &sprite = e.add<SpriteRenderer>();
                              sprite.size = {0.8F, 1.2F};
                              sprite.offset = {0, -0.25F};
                              sprite.color = {83, 170, 230, 255};
                              sprite.layer = 10;
                              sprite.ySort = true;
                              e.add<TopDownController>();
                              e.add<Interactor>();
                              return e.id();
                          }});
    registry.addTemplate({"NPC", "Exploration", [](Scene &scene, Vec2 at) {
                              Entity &e = scene.createEntity("NPC");
                              e.setWorldPosition(at);
                              auto &sprite = e.add<SpriteRenderer>();
                              sprite.size = {0.8F, 1.2F};
                              sprite.offset = {0, -0.25F};
                              sprite.color = {236, 181, 113, 255};
                              sprite.layer = 10;
                              sprite.ySort = true;
                              e.add<NpcPath>();
                              e.add<Interactable>();
                              e.add<Dialogue>();
                              return e.id();
                          }});
    registry.addTemplate({"Interactive Object", "Exploration", [](Scene &scene, Vec2 at) {
                              Entity &e = scene.createEntity("Interactive Object");
                              e.setWorldPosition(at);
                              e.add<SpriteRenderer>();
                              e.add<Interactable>();
                              return e.id();
                          }});
    registry.addTemplate({"Gate", "Exploration", [](Scene &scene, Vec2 at) {
                              Entity &e = scene.createEntity("Gate");
                              e.setWorldPosition(at);
                              auto &sprite = e.add<SpriteRenderer>();
                              sprite.size = {2.0F, 0.5F};
                              sprite.color = {174, 129, 70, 255};
                              sprite.layer = 10;
                              sprite.ySort = true;
                              auto &collider = e.add<Collider>();
                              collider.size = sprite.size;
                              collider.layer = layers::solid;
                              e.add<StateGate>();
                              e.add<Interactable>();
                              return e.id();
                          }});
    registry.addTemplate({"Map Portal", "Exploration", [](Scene &scene, Vec2 at) {
                              Entity &e = scene.createEntity("Map Portal");
                              e.setWorldPosition(at);
                              auto &collider = e.add<Collider>();
                              collider.isTrigger = true;
                              collider.size = {1, 1};
                              collider.layer = layers::sensor;
                              e.add<MapPortal>();
                              return e.id();
                          }});
    registry.addTemplate({"Map Spawn", "Exploration", [](Scene &scene, Vec2 at) {
                              Entity &e = scene.createEntity("Map Spawn");
                              e.setWorldPosition(at);
                              e.add<MapSpawn>();
                              return e.id();
                          }});
}
} // namespace yk
