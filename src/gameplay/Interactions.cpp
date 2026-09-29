#include "yk/gameplay/Gameplay.hpp"
#include <algorithm>

namespace yk {
namespace {
void turnIntoTrigger(Entity &entity) {
    if (auto *collider = entity.get<Collider>())
        collider->isTrigger = true;
}
void play(GameContext &context, const AssetRef &sound) {
    if (!sound.path.empty())
        context.audio().play(sound.path);
}
bool hasAnyTag(const Entity &entity, const std::vector<std::string> &tags) {
    return std::any_of(tags.begin(), tags.end(),
                       [&](const std::string &tag) { return entity.hasTag(tag); });
}
} // namespace

// ----- Killable -----
void Killable::describe(TypeBuilder<Killable> &type) {
    type.category("Gameplay")
        .description(
            "Can be killed by hazards; plays its death animation, then comes back after a delay.");
    type.field("respawn", &Killable::respawn);
    type.field("respawnDelay", &Killable::respawnDelay).range(0, 60, 0.1);
    type.field("deathDuration", &Killable::deathDuration)
        .range(0, 60, 0.05)
        .tooltip("Seconds the sprite stays visible after dying, for a death animation.");
    type.field("spawnPoint", &Killable::spawnPoint)
        .tooltip("Return here; otherwise the last checkpoint, else the start.");
    type.field("deathSound", &Killable::deathSound).asset("sound");
    type.field("respawnSound", &Killable::respawnSound).asset("sound");
    type.field("deathEffect", &Killable::deathEffect)
        .asset("prefab")
        .tooltip("Effect prefab spawned where it dies.");
    type.field("respawnEffect", &Killable::respawnEffect)
        .asset("prefab")
        .tooltip("Effect prefab spawned where it returns.");
    type.field("alive", &Killable::alive_).readOnly();
}
void Killable::onStart(GameContext &) {
    home_ = entity().worldPosition();
    if (auto *animated = entity().get<AnimatedSprite>())
        animated->setBool("dead", false);
}
void Killable::setRespawnPoint(Vec2 worldPosition) {
    checkpoint_ = worldPosition;
    hasCheckpoint_ = true;
}
void Killable::setSolid(bool solid) {
    if (auto *body = entity().get<RigidBody>())
        body->enabled = solid;
    for (Collider *collider : entity().getAll<Collider>())
        collider->enabled = solid;
}
void Killable::setVisible(bool visible) {
    for (const EntityId id : entity().scene().subtree(entity().id()))
        if (Entity *node = entity().scene().find(id))
            for (SpriteRenderer *sprite : node->getAll<SpriteRenderer>())
                sprite->visible = visible;
}
void Killable::kill(GameContext &context, EntityId killer) {
    if (!alive_)
        return;
    alive_ = false;
    timer_ = respawnDelay;
    dying_ = deathDuration > 0.0F;
    dyingTimer_ = deathDuration;
    setSolid(false);
    setVisible(dying_); // Stays for the death animation, if there is one.
    if (auto *animated = entity().get<AnimatedSprite>())
        animated->setBool("dead", true);
    play(context, deathSound);
    spawnEffect(context, deathEffect, entity().worldPosition());
    context.blackboard().add("deaths", 1);
    context.emit("entity_died", entity().id(), killer);
}
void Killable::onFixedUpdate(GameContext &context, float seconds) {
    if (alive_)
        return;
    if (dying_) {
        dyingTimer_ -= seconds;
        if (dyingTimer_ <= 0.0F) {
            dying_ = false;
            setVisible(false);
        }
    }
    if (!respawn)
        return;
    timer_ -= seconds;
    if (timer_ > 0.0F)
        return;
    Vec2 where = home_;
    if (const Entity *marker = context.scene().find(spawnPoint))
        where = marker->worldPosition();
    else if (hasCheckpoint_)
        where = checkpoint_;
    alive_ = true;
    dying_ = false;
    setSolid(true);
    setVisible(true);
    context.teleport(entity(), where);
    if (auto *animated = entity().get<AnimatedSprite>()) {
        animated->setBool("dead", false);
        animated->trigger("respawned");
    }
    play(context, respawnSound);
    spawnEffect(context, respawnEffect, where);
    context.emit("entity_respawned", entity().id());
}

// ----- Hazard -----
void Hazard::describe(TypeBuilder<Hazard> &type) {
    type.category("Gameplay")
        .description("Kills Killable entities that touch its trigger collider.")
        .dependsOn("Collider")
        .onAdd([](Entity &entity, Hazard &) { turnIntoTrigger(entity); });
    type.field("affectsTags", &Hazard::affectsTags)
        .tooltip("Only entities with one of these tags are hurt. Empty: everything Killable.");
}
void Hazard::onTriggerEnter(GameContext &context, Entity &other) {
    Killable *killable = other.get<Killable>();
    if (!killable || !killable->alive())
        return;
    if (!affectsTags.empty() && !hasAnyTag(other, affectsTags))
        return;
    killable->kill(context, entity().id());
}

// ----- Collectible -----
void Collectible::describe(TypeBuilder<Collectible> &type) {
    type.category("Gameplay")
        .description("Picked up when touched: adds `value` to a game variable and disappears.")
        .dependsOn("Collider")
        .onAdd([](Entity &entity, Collectible &) { turnIntoTrigger(entity); });
    type.field("collectorTags", &Collectible::collectorTags)
        .tooltip("Who can collect it. Empty: any movable body.");
    type.field("variable", &Collectible::variable)
        .tooltip("Blackboard variable to increase (shown by UI text as {name}).");
    type.field("value", &Collectible::value).range(-1000, 1000, 0.5);
    type.field("sound", &Collectible::sound).asset("sound");
    type.field("collectEffect", &Collectible::collectEffect)
        .asset("prefab")
        .tooltip("Effect prefab spawned where it is picked up.");
}
void Collectible::onStart(GameContext &context) {
    if (!variable.empty())
        context.blackboard().add(variable + "_total", static_cast<double>(value));
}
void Collectible::onTriggerEnter(GameContext &context, Entity &other) {
    if (!entity().active() || !matchesActivator(other, collectorTags))
        return;
    if (const auto *killable = other.get<Killable>(); killable && !killable->alive())
        return;
    context.blackboard().add(variable, static_cast<double>(value));
    play(context, sound);
    spawnEffect(context, collectEffect, entity().worldPosition());
    context.emit("collected", entity().id(), other.id());
    entity().setActive(false);
}

// ----- Checkpoint -----
void Checkpoint::describe(TypeBuilder<Checkpoint> &type) {
    type.category("Gameplay")
        .description("Sets where a character respawns after it is touched.")
        .dependsOn("Collider")
        .onAdd([](Entity &entity, Checkpoint &) { turnIntoTrigger(entity); });
    type.field("activatorTags", &Checkpoint::activatorTags);
    type.field("respawnOffset", &Checkpoint::respawnOffset).range(-100, 100, 0.1);
    type.field("activeColor", &Checkpoint::activeColor);
    type.field("sound", &Checkpoint::sound).asset("sound");
}
void Checkpoint::onTriggerEnter(GameContext &context, Entity &other) {
    Killable *killable = other.get<Killable>();
    if (!killable || !matchesActivator(other, activatorTags))
        return;
    killable->setRespawnPoint(entity().worldPosition() + respawnOffset);
    if (auto *animated = entity().get<AnimatedSprite>())
        animated->setBool("reached", true); // Art shows the reached state through its controller.
    else if (auto *sprite = entity().get<SpriteRenderer>())
        sprite->color = activeColor;
    play(context, sound);
    context.emit("checkpoint_reached", entity().id(), other.id());
}

// ----- SpawnPoint -----
void SpawnPoint::describe(TypeBuilder<SpawnPoint> &type) {
    type.category("Gameplay").description("Where a character starts and returns to after dying.");
    type.field("character", &SpawnPoint::character)
        .tooltip("The entity placed here when the game starts.");
}
void SpawnPoint::onStart(GameContext &context) {
    Entity *who = context.scene().find(character);
    if (!who)
        return;
    context.teleport(*who, entity().worldPosition());
    if (Killable *killable = who->get<Killable>())
        killable->setRespawnPoint(entity().worldPosition());
}

// ----- Goal -----
void Goal::describe(TypeBuilder<Goal> &type) {
    type.category("Gameplay")
        .description(
            "An exit: satisfied while a living entity with the required tag stands inside.")
        .dependsOn("Collider")
        .onAdd([](Entity &entity, Goal &) { turnIntoTrigger(entity); });
    type.field("requiredTag", &Goal::requiredTag)
        .tooltip("Only entities with this tag count. Empty: any movable body.");
    type.field("targets", &Goal::targets).tooltip("Receivers driven while the goal is satisfied.");
    type.field("satisfiedColor", &Goal::satisfiedColor);
    type.field("sound", &Goal::sound).asset("sound");
    type.field("satisfied", &Goal::satisfied_).readOnly();
}
void Goal::onStart(GameContext &context) {
    if (const auto *sprite = entity().get<SpriteRenderer>())
        baseColor_ = sprite->color;
    satisfied_ = false;
    applyVisuals();
    sendSignal(context.scene(), entity().id(), targets, false);
}
void Goal::applyVisuals() {
    if (auto *animated = entity().get<AnimatedSprite>())
        animated->setBool("satisfied", satisfied_); // Art shows it through its controller.
    else if (auto *sprite = entity().get<SpriteRenderer>())
        sprite->color = satisfied_ ? satisfiedColor : baseColor_;
}
void Goal::onFixedUpdate(GameContext &context, float) {
    bool occupied = false;
    for (const EntityId id : context.overlapping(entity().id())) {
        const Entity *other = context.scene().find(id);
        if (!other)
            continue;
        const bool eligible =
            requiredTag.empty() ? matchesActivator(*other, {}) : other->hasTag(requiredTag);
        const auto *killable = other->get<Killable>();
        if (eligible && !(killable && !killable->alive())) {
            occupied = true;
            break;
        }
    }
    if (occupied != satisfied_) {
        satisfied_ = occupied;
        applyVisuals();
        if (satisfied_)
            play(context, sound);
        context.emit(satisfied_ ? "goal_reached" : "goal_left", entity().id());
    }
    sendSignal(context.scene(), entity().id(), targets, satisfied_);
}

// ----- TriggerZone -----
void TriggerZone::describe(TypeBuilder<TriggerZone> &type) {
    type.category("Gameplay")
        .description("A trigger region that raises named events and drives targets while occupied.")
        .dependsOn("Collider")
        .onAdd([](Entity &entity, TriggerZone &) { turnIntoTrigger(entity); });
    type.field("filterTags", &TriggerZone::filterTags).tooltip("Empty: any movable body.");
    type.field("enterEvent", &TriggerZone::enterEvent)
        .tooltip("Event raised when the zone becomes occupied.");
    type.field("exitEvent", &TriggerZone::exitEvent)
        .tooltip("Event raised when the zone becomes empty.");
    type.field("once", &TriggerZone::once).tooltip("Raise the events only the first time.");
    type.field("targets", &TriggerZone::targets);
    type.field("occupied", &TriggerZone::occupied_).readOnly();
}
void TriggerZone::onFixedUpdate(GameContext &context, float) {
    bool occupied = false;
    for (const EntityId id : context.overlapping(entity().id()))
        if (const Entity *other = context.scene().find(id);
            other && matchesActivator(*other, filterTags))
            occupied = true;
    if (occupied != occupied_) {
        occupied_ = occupied;
        if (!finished_) {
            const std::string &name = occupied_ ? enterEvent : exitEvent;
            if (!name.empty())
                context.emit(name, entity().id());
            if (!occupied_ && once)
                finished_ = true; // The first visit is complete: later visits raise nothing.
        }
    }
    sendSignal(context.scene(), entity().id(), targets, occupied_);
}
} // namespace yk
