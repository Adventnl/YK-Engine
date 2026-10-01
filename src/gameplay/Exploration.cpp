#include "yk/gameplay/Exploration.hpp"
#include "yk/gameplay/Character.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace yk {

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
    Vec2 direction{input->axis(context, leftAction, rightAction),
                   input->axis(context, upAction, downAction)};
    if (lengthSquared(direction) > 1.0F)
        direction = normalized(direction);
    const auto state = context.physics().state(*body);
    if (!state)
        return;
    const Vec2 target = direction * std::max(0.0F, speed);
    Vec2 delta = target - state.value().linearVelocity;
    const float limit = std::max(0.0F, acceleration) * seconds;
    if (length(delta) > limit)
        delta = normalized(delta) * limit;
    context.physics().setVelocity(*body, state.value().linearVelocity + delta);
    detail::animateDirection(entity(), direction, facing_, animation_);
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
    if (wait_ > 0)
        wait_ = std::max(0.0F, wait_ - seconds);
    else if ((requiredFlag.empty() || context.blackboard().number(requiredFlag) != 0.0) &&
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
    if (auto *gate = entity().get<StateGate>()) {
        if (gate->locked(context)) {
            context.emit("gate_locked", entity().id(), actor.id());
            return;
        }
        gate->toggle(context);
    }
    if (auto *dialogue = entity().get<Dialogue>())
        dialogue->begin(context);
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
            if (gap <= candidate->range && gap < nearest) {
                closest = candidate;
                nearest = gap;
            }
        }
    context.blackboard().set("interaction_prompt", closest ? closest->prompt : "");
    if (closest && input->button(context, action).pressed)
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
    type.field("logic", &StateGate::logic).options(signalLogicNames());
    type.field("invert", &StateGate::invert);
}
void StateGate::onStart(GameContext &context) {
    open_ = startsOpen || (!persistFlag.empty() && context.blackboard().number(persistFlag) != 0);
    amount_ = open_ ? 1.0F : 0.0F;
    for (Collider *collider : entity().getAll<Collider>())
        if (!collider->isTrigger)
            collider->enabled = !open_;
    if (auto *sprite = entity().get<SpriteRenderer>()) {
        baseAlpha_ = sprite->color.a;
        sprite->visible = !open_;
    }
}
bool StateGate::locked(GameContext &context) const {
    return startsLocked && (unlockFlag.empty() || context.blackboard().number(unlockFlag) == 0.0);
}
void StateGate::toggle(GameContext &context) {
    if (locked(context))
        return;
    open_ = !open_;
    if (!persistFlag.empty()) {
        context.blackboard().set(persistFlag, open_ ? 1.0 : 0.0);
        context.blackboard().keep(persistFlag);
    }
    context.emit(open_ ? "gate_opened" : "gate_closed", entity().id());
}
void StateGate::onFixedUpdate(GameContext &context, float seconds) {
    if (signalActive() && !locked(context) && !open_) {
        open_ = true;
        if (!persistFlag.empty()) {
            context.blackboard().set(persistFlag, 1.0);
            context.blackboard().keep(persistFlag);
        }
        context.emit("gate_opened", entity().id());
    }
    const float target = open_ ? 1.0F : 0.0F;
    const float step = animationSeconds > 0 ? seconds / animationSeconds : 1.0F;
    amount_ = std::clamp(amount_ + (target > amount_ ? step : -step), 0.0F, 1.0F);
    if (auto *animation = entity().get<AnimatedSprite>())
        animation->setBool("open", open_);
    if (auto *sprite = entity().get<SpriteRenderer>()) {
        sprite->visible = amount_ < 0.99F;
        sprite->color.a =
            static_cast<std::uint8_t>(static_cast<float>(baseAlpha_) * (1.0F - amount_));
    }
    for (Collider *collider : entity().getAll<Collider>())
        if (!collider->isTrigger)
            collider->enabled = amount_ < 0.95F;
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

void registerExplorationComponents(ComponentRegistry &registry) {
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
