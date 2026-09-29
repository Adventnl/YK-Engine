// Plays the gameplay components headlessly: scripted keyboard input against real physics.
#include "support/check.hpp"
#include "yk/components/Effects.hpp"
#include "yk/core/Log.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <cmath>

using namespace yk;

namespace {
constexpr float floorTop = 10.0F;

LayerConfig testLayers() {
    LayerConfig layers = LayerConfig::defaults();
    for (const char *name : {layers::solid, layers::player, layers::sensor, layers::prop})
        layers.addLayer(name);
    const auto index = [&](const char *name) {
        return static_cast<std::size_t>(layers.indexOf(name));
    };
    layers.setInteraction(index(layers::player), index(layers::solid), true);
    layers.setInteraction(index(layers::player), index(layers::player), true);
    layers.setInteraction(index(layers::player), index(layers::sensor), true);
    layers.setInteraction(index(layers::prop), index(layers::solid), true);
    layers.setInteraction(index(layers::prop), index(layers::player), true);
    layers.setInteraction(index(layers::prop), index(layers::sensor), true);
    layers.setInteraction(index(layers::prop), index(layers::prop), true);
    return layers;
}

ComponentRegistry makeRegistry() {
    ComponentRegistry registry;
    registerEngineComponents(registry);
    registerGameplayComponents(registry);
    return registry;
}

// A scene under construction plus the runtime once started.
struct World {
    ComponentRegistry registry = makeRegistry();
    std::unique_ptr<Scene> scene = std::make_unique<Scene>(registry, 11);
    std::unique_ptr<GameRuntime> runtime;
    RecordingAudio audio;
    MemoryAssets assets;
    Keyboard keyboard;
    std::vector<std::string> events;

    Entity &box(const char *name, Vec2 center, Vec2 size, const char *layer = layers::solid) {
        Entity &entity = scene->createEntity(name);
        entity.transform().position = center;
        auto &collider = entity.add<Collider>();
        collider.size = size;
        collider.layer = layer;
        return entity;
    }
    Entity &ground(float left = -40.0F, float right = 40.0F) {
        return box("Ground", {(left + right) / 2, floorTop + 0.5F}, {right - left, 1.0F});
    }
    // `actionSet` names the input-map set that drives it ("Player1" is A/D/W, "Player2" the arrow
    // keys); a set that does not exist leaves the character uncontrolled.
    Entity &character(const char *name, Vec2 at, const char *actionSet = "Player1",
                      const char *tag = "") {
        Entity &entity = scene->createEntity(name);
        entity.transform().position = at;
        entity.add<PlatformerController>();
        entity.get<PlayerInput>()->actionSet = actionSet;
        entity.add<Killable>();
        entity.add<SpriteRenderer>().size = {0.6F, 0.95F};
        if (*tag)
            entity.addTag(tag);
        return entity;
    }
    GameRuntime &start() {
        RuntimeOptions options;
        options.layers = testLayers();
        options.audio = &audio;
        options.assets = &assets;
        auto created = GameRuntime::create(std::move(scene), options);
        CHECK(created);
        runtime = std::move(created.value());
        runtime->events().subscribe(
            "*", [this](const GameEvent &event) { events.push_back(event.name); });
        return *runtime;
    }
    // Input driver: keys stay down until released; press/release edges last one tick.
    void down(Key key) {
        keyboard.set(key, true);
    }
    void up(Key key) {
        keyboard.set(key, false);
    }
    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) {
            runtime->stepOnce(keyboard);
            keyboard.beginFrame();
        }
    }
    Entity &at(const char *name) {
        return *runtime->scene().findByName(name);
    }
    Vec2 position(const char *name) {
        return at(name).worldPosition();
    }
    bool happened(const std::string &name) const {
        return std::find(events.begin(), events.end(), name) != events.end();
    }
    int count(const std::string &name) const {
        return static_cast<int>(std::count(events.begin(), events.end(), name));
    }
};

constexpr float restingHeight = floorTop - 0.475F; // Capsule center when standing on the floor.


// The whole animation chain on real physics: the controller publishes generic parameters, the
// controller asset picks clips, the AnimatedSprite shows frames and mirrors the sprite. No code
// here or in the controller names a clip.
constexpr const char *heroAnimation = R"({"format":"yk.animation","version":2,
    "texture":"assets/hero.png","columns":8,"rows":1,"clips":[
    {"name":"idle","first":0,"count":2,"fps":4},
    {"name":"run","first":2,"count":2,"fps":10},
    {"name":"jump","first":4,"count":1,"fps":10,"loop":false},
    {"name":"fall","first":5,"count":1,"fps":10},
    {"name":"land","first":6,"count":1,"fps":20,"loop":false}]})";
constexpr const char *heroController = R"({"format":"yk.animator","version":1,
    "parameters":[
      {"name":"speed","type":"float"},{"name":"velocityY","type":"float"},
      {"name":"grounded","type":"bool","default":true},
      {"name":"jumped","type":"trigger"},{"name":"landed","type":"trigger"}],
    "entry":"Idle",
    "states":[{"name":"Idle","clip":"idle"},{"name":"Run","clip":"run"},{"name":"Jump","clip":"jump"},
              {"name":"Fall","clip":"fall"},{"name":"Land","clip":"land"}],
    "transitions":[
      {"from":"*","to":"Jump","when":[{"parameter":"jumped"}]},
      {"from":"Jump","to":"Fall","when":[{"parameter":"velocityY","op":">","value":0.5}]},
      {"from":"Run","to":"Fall","when":[{"parameter":"grounded","value":false}]},
      {"from":"Idle","to":"Fall","when":[{"parameter":"grounded","value":false}]},
      {"from":"Fall","to":"Land","when":[{"parameter":"landed"}]},
      {"from":"Fall","to":"Idle","when":[{"parameter":"grounded"}]},
      {"from":"Land","to":"Idle","exitTime":1.0},
      {"from":"Idle","to":"Run","when":[{"parameter":"speed","op":">","value":0.5}]},
      {"from":"Run","to":"Idle","when":[{"parameter":"speed","op":"<=","value":0.5}]}]})";

void characterAnimation() {
    World w;
    w.assets.files["anim/hero.ykanim"] = heroAnimation;
    w.assets.files["anim/hero.ykctl"] = heroController;
    w.ground();
    Entity &hero = w.character("Hero", {0, restingHeight - 1.0F});
    auto &animated = hero.add<AnimatedSprite>();
    animated.animation.path = "anim/hero.ykanim";
    animated.controller.path = "anim/hero.ykctl";
    w.start();
    const auto state = [&]() -> std::string { return w.at("Hero").get<AnimatedSprite>()->state(); };
    const auto sprite = [&]() -> SpriteRenderer & { return *w.at("Hero").get<SpriteRenderer>(); };

    // The asset's sheet layout and texture are applied to the sprite.
    w.tick(1);
    CHECK(sprite().columns == 8 && sprite().texture.path == "assets/hero.png");

    // Falling onto the floor from a small height: Fall, then Idle (too slow for a Land).
    bool sawFall = false;
    for (int i = 0; i < 60; ++i) {
        w.tick();
        sawFall = sawFall || state() == "Fall";
    }
    CHECK(sawFall && state() == "Idle" && sprite().frame <= 1 && !sprite().flipX);

    // Running right: Run clip frames, sprite not mirrored; left: mirrored.
    w.down(Key::D);
    w.tick(30);
    CHECK(state() == "Run" && (sprite().frame == 2 || sprite().frame == 3) && !sprite().flipX);
    w.up(Key::D);
    w.tick(40);
    CHECK(state() == "Idle");
    w.down(Key::A);
    w.tick(20);
    CHECK(state() == "Run" && sprite().flipX);
    w.up(Key::A);
    w.tick(40);
    CHECK(state() == "Idle" && sprite().flipX); // Keeps facing left when standing.

    // A jump (starting from Idle): Jump -> Fall -> Land -> Idle, in that order (repeats collapsed).
    std::vector<std::string> sequence;
    w.down(Key::W);
    for (int i = 0; i < 140; ++i) {
        if (i == 3)
            w.up(Key::W);
        w.tick();
        if (sequence.empty() || sequence.back() != state())
            sequence.push_back(state());
    }
    const std::vector<std::string> expected{"Jump", "Fall", "Land", "Idle"};
    if (sequence != expected) {
        std::string seen;
        for (const std::string &name : sequence)
            seen += name + " ";
        std::fprintf(stderr, "jump sequence was: %s\n", seen.c_str());
    }
    CHECK(sequence == expected);
    CHECK(sprite().frame <= 1);

    // A character without an AnimatedSprite still mirrors its plain sprite (the old behaviour).
    World plain;
    plain.ground();
    plain.character("Plain", {0, restingHeight});
    plain.start();
    plain.down(Key::A);
    plain.tick(10);
    CHECK(plain.at("Plain").get<SpriteRenderer>()->flipX);
}

// Level geometry a platformer needs beyond boxes: jump-through platforms and wedge ramps.
void oneWayAndWedge() {
    // A one-way platform hovering above the floor: the hero jumps up through it, lands on top and
    // walks off it again.
    {
        World w;
        w.ground();
        Entity &platform = w.box("Ledge", {0.0F, 8.4F}, {4.0F, 0.4F}); // Spans y 8.2 .. 8.6.
        platform.get<Collider>()->oneWay = true;
        w.character("Hero", {-1.0F, restingHeight});
        w.start();
        w.tick(30);
        auto *controller = w.at("Hero").get<PlatformerController>();
        CHECK(controller->grounded());
        // Hold jump for the whole rise (2.3 m: the feet reach y = 7.7, above the ledge's top 8.2).
        w.down(Key::W);
        float highest = 100.0F;
        for (int i = 0; i < 50; ++i) {
            w.tick();
            highest = std::min(highest, w.position("Hero").y);
        }
        w.up(Key::W);
        CHECK(highest < 8.2F - 0.475F - 0.1F); // It rose past the ledge: through it.
        w.tick(60);
        CHECK_NEAR(w.position("Hero").y, 8.2F - 0.475F, 0.06); // ...and now stands on top.
        CHECK(controller->grounded());

        // Walk off the right edge and land back on the floor.
        w.down(Key::D);
        w.tick(90);
        w.up(Key::D);
        w.tick(60);
        CHECK(w.position("Hero").x > 2.0F);
        CHECK_NEAR(w.position("Hero").y, restingHeight, 0.06);
    }
    // A platform lower than the hero is tall: a solid box would stop the hero, the one-way one is
    // simply walked through (it only blocks from above).
    {
        World w;
        w.ground();
        Entity &low = w.box("Ceiling", {0.0F, 9.35F}, {6.0F, 0.3F}); // Underside at y 9.5, feet at 10.
        low.get<Collider>()->oneWay = true;
        w.character("Hero", {-5.0F, restingHeight});
        w.start();
        w.tick(30);
        w.down(Key::D);
        w.tick(120);
        CHECK(w.position("Hero").x > 2.0F);

        World solid; // The same setup with a plain box blocks the hero, so the test can tell.
        solid.ground();
        solid.box("Ceiling", {0.0F, 9.35F}, {6.0F, 0.3F});
        solid.character("Hero", {-5.0F, restingHeight});
        solid.start();
        solid.tick(30);
        solid.down(Key::D);
        solid.tick(120);
        CHECK(solid.position("Hero").x < -2.0F);
    }
    // Wedge ramps: 4 wide, 2 high (about 27 degrees), rising to the right...
    {
        World w;
        w.ground();
        Entity &ramp = w.box("Ramp", {4.0F, floorTop - 1.0F}, {4.0F, 2.0F});
        ramp.get<Collider>()->shape = ColliderShape::Wedge;
        w.character("Hero", {-1.0F, restingHeight});
        w.start();
        w.tick(30);
        w.down(Key::D);
        w.tick(60);
        CHECK(w.position("Hero").x > 2.5F && w.position("Hero").x < 5.5F); // On the ramp.
        CHECK(w.position("Hero").y < restingHeight - 0.5F);                // Climbing.
        CHECK(w.at("Hero").get<PlatformerController>()->grounded());
        w.up(Key::D);
        w.tick(45);
        const Vec2 rest = w.position("Hero");
        w.tick(60);
        CHECK_NEAR(w.position("Hero").y, rest.y, 0.03); // Grip holds it on the slope.
        CHECK_NEAR(w.position("Hero").x, rest.x, 0.03);
    }
    // ...and mirrored (scale x = -1): rising to the left.
    {
        World w;
        w.ground();
        Entity &ramp = w.box("Ramp", {-4.0F, floorTop - 1.0F}, {4.0F, 2.0F});
        ramp.get<Collider>()->shape = ColliderShape::Wedge;
        ramp.transform().scale = {-1.0F, 1.0F};
        w.character("Hero", {1.0F, restingHeight});
        w.start();
        w.tick(30);
        w.down(Key::A);
        w.tick(60);
        CHECK(w.position("Hero").x < -2.5F && w.position("Hero").x > -5.5F);
        CHECK(w.position("Hero").y < restingHeight - 0.5F);
        CHECK(w.at("Hero").get<PlatformerController>()->grounded());
    }
    // The tall side is a wall: walking into it from the right stops the hero at x = 6 + radius.
    {
        World w;
        w.ground();
        Entity &ramp = w.box("Ramp", {4.0F, floorTop - 1.0F}, {4.0F, 2.0F});
        ramp.get<Collider>()->shape = ColliderShape::Wedge;
        w.character("Hero", {9.0F, restingHeight});
        w.start();
        w.tick(30);
        w.down(Key::A);
        w.tick(120);
        CHECK_NEAR(w.position("Hero").x, 6.3F, 0.1);
        CHECK_NEAR(w.position("Hero").y, restingHeight, 0.06);
    }
}

// A lever that needs the Interact action: touching it does nothing, pressing the action of the
// character's own PlayerInput set flips it, and the character is told to play its interact clip.
void interactLever() {
    World w;
    w.ground();
    Entity &lever = w.box("Lever", {0.0F, floorTop - 0.4F}, {0.6F, 0.8F}, layers::sensor);
    lever.get<Collider>()->isTrigger = true;
    lever.add<Lever>().interactAction = "Interact";
    w.character("One", {-1.5F, restingHeight});                // Player1: Interact is S / E.
    w.character("Two", {3.0F, restingHeight}, "Player2");      // Player2: Interact is Down.
    w.character("Nobody", {-3.0F, restingHeight}, "Nobody");   // No such set: cannot interact.
    w.start();
    w.tick(30);
    auto *plate = w.at("Lever").get<Lever>();

    // Walking through it (touching) flips nothing.
    w.down(Key::D);
    w.tick(60);
    w.up(Key::D);
    w.tick(30);
    CHECK(!plate->on() && w.count("lever_toggled") == 0);
    CHECK(w.position("One").x > 1.0F); // It walked past.

    // Stand in it and press Interact (S): it flips once per press, not while held.
    w.runtime->teleport(w.at("One"), {0.0F, restingHeight});
    w.tick(30);
    w.down(Key::S);
    w.tick(3);
    CHECK(plate->on() && w.count("lever_toggled") == 1);
    w.tick(40); // Held: no repeat flips (also inside the cooldown).
    CHECK(plate->on() && w.count("lever_toggled") == 1);
    w.up(Key::S);
    w.tick(30);
    w.down(Key::S);
    w.tick(3);
    w.up(Key::S);
    CHECK(!plate->on() && w.count("lever_toggled") == 2);

    // Player2's own action works for player2, and pressing another player's action does not.
    w.runtime->teleport(w.at("One"), {-6.0F, restingHeight}); // Out of the way.
    w.runtime->teleport(w.at("Two"), {0.0F, restingHeight});
    w.tick(60);
    w.down(Key::S); // Player1's key with only Player2 in the lever.
    w.tick(3);
    w.up(Key::S);
    CHECK(!plate->on());
    w.down(Key::Down);
    w.tick(3);
    w.up(Key::Down);
    CHECK(plate->on() && w.count("lever_toggled") == 3);
    // A character with no action set never flips it, whatever is pressed.
    w.runtime->teleport(w.at("Two"), {6.0F, restingHeight});
    w.runtime->teleport(w.at("Nobody"), {0.0F, restingHeight});
    w.tick(60);
    for (const Key key : {Key::S, Key::Down, Key::E})
        w.down(key);
    w.tick(5);
    for (const Key key : {Key::S, Key::Down, Key::E})
        w.up(key);
    CHECK(plate->on() && w.count("lever_toggled") == 3);
}

constexpr const char *actorAnimation = R"({"format":"yk.animation","version":2,"columns":4,"rows":1,
    "clips":[{"name":"idle","first":0,"count":1},{"name":"interact","first":1,"count":1,"fps":30,"loop":false},
             {"name":"death","first":2,"count":1,"loop":false},{"name":"back","first":3,"count":1}]})";
constexpr const char *actorController = R"({"format":"yk.animator","version":1,
    "parameters":[{"name":"dead","type":"bool"},{"name":"interact","type":"trigger"},
                  {"name":"respawned","type":"trigger"}],
    "states":[{"name":"Idle","clip":"idle"},{"name":"Interact","clip":"interact"},
              {"name":"Death","clip":"death"},{"name":"Back","clip":"back"}],
    "transitions":[
      {"from":"*","to":"Death","when":[{"parameter":"dead"}]},
      {"from":"Death","to":"Back","when":[{"parameter":"dead","value":false}]},
      {"from":"Back","to":"Idle","exitTime":0.5},
      {"from":"Idle","to":"Interact","when":[{"parameter":"interact"}]},
      {"from":"Interact","to":"Idle","exitTime":1.0}]})";

void interactAnimation() {
    World w;
    w.assets.files["a.ykanim"] = actorAnimation;
    w.assets.files["a.ykctl"] = actorController;
    w.ground();
    Entity &lever = w.box("Lever", {0.0F, floorTop - 0.4F}, {0.6F, 0.8F}, layers::sensor);
    lever.get<Collider>()->isTrigger = true;
    lever.add<Lever>().interactAction = "Interact";
    Entity &hero = w.character("Hero", {0.0F, restingHeight});
    auto &animated = hero.add<AnimatedSprite>();
    animated.animation.path = "a.ykanim";
    animated.controller.path = "a.ykctl";
    w.start();
    w.tick(30);
    const auto state = [&] { return w.at("Hero").get<AnimatedSprite>()->state(); };
    CHECK(state() == "Idle");
    w.down(Key::S);
    w.tick(2);
    w.up(Key::S);
    CHECK(state() == "Interact"); // The lever told the character to play its interact clip...
    w.tick(10);
    CHECK(state() == "Idle"); // ...which finishes and returns to Idle.
}

// Dying: the body leaves the world at once, the sprite stays for the death animation, the
// respawn brings everything back, and effect prefabs spawn where it happened.
void deathAndRespawn() {
    World w;
    w.assets.files["a.ykanim"] = actorAnimation;
    w.assets.files["a.ykctl"] = actorController;
    Scene puffScene(w.registry, 5);
    Entity &puff = puffScene.createEntity("Puff");
    puff.add<Lifetime>().seconds = 0.5F;
    w.assets.files["fx/death.ykprefab"] = subtreeToJson(puffScene, puff.id()).dump();
    Scene poofScene(w.registry, 6);
    Entity &poof = poofScene.createEntity("Poof");
    poof.add<Lifetime>().seconds = 0.5F;
    w.assets.files["fx/respawn.ykprefab"] = subtreeToJson(poofScene, poof.id()).dump();

    w.ground();
    Entity &lava = w.box("Lava", {4.0F, floorTop - 0.3F}, {2.0F, 0.6F}, layers::sensor);
    lava.get<Collider>()->isTrigger = true;
    lava.add<Hazard>();
    Entity &hero = w.character("Hero", {0.0F, restingHeight});
    auto &animated = hero.add<AnimatedSprite>();
    animated.animation.path = "a.ykanim";
    animated.controller.path = "a.ykctl";
    auto *killable = hero.get<Killable>();
    killable->deathDuration = 0.6F;
    killable->respawnDelay = 1.5F;
    killable->deathEffect.path = "fx/death.ykprefab";
    killable->respawnEffect.path = "fx/respawn.ykprefab";
    w.start();
    w.tick(30);
    CHECK(killable->alive());
    const auto visible = [&] { return w.at("Hero").get<SpriteRenderer>()->visible; };
    const auto state = [&] { return w.at("Hero").get<AnimatedSprite>()->state(); };

    // Walk into the lava.
    w.down(Key::D);
    for (int i = 0; i < 120 && killable->alive(); ++i)
        w.tick();
    w.up(Key::D);
    CHECK(!killable->alive() && w.happened("entity_died"));
    const Vec2 diedAt = w.position("Hero");
    CHECK(w.at("Hero").get<Collider>()->enabled == false); // Nothing collides with it any more.
    CHECK(visible() && state() == "Death");               // The animation plays in place.
    CHECK(w.runtime->scene().findByName("Puff") != nullptr);
    CHECK(w.runtime->scene().findByName("Puff")->worldPosition() == diedAt);
    w.tick(30); // 0.5 s: still inside the 0.6 s death animation.
    CHECK(visible());
    w.tick(10);
    CHECK(!visible() && !killable->alive()); // Hidden, waiting to come back.

    // Respawn after 1.5 s in total: back at the start, visible, animating again.
    w.tick(60);
    CHECK(killable->alive() && visible() && w.happened("entity_respawned"));
    CHECK(w.position("Hero").x < 1.0F);
    CHECK(w.at("Hero").get<Collider>()->enabled);
    CHECK(w.runtime->scene().findByName("Poof") != nullptr);
    w.tick(20);
    CHECK(state() == "Idle"); // Death -> Back -> Idle.
    w.tick(60);
    CHECK(w.runtime->scene().findByName("Puff") == nullptr &&
          w.runtime->scene().findByName("Poof") == nullptr); // The effects cleaned themselves up.

    // A missing effect prefab never breaks a death or a respawn.
    World broken;
    broken.ground();
    Entity &pit = broken.box("Lava", {0.0F, floorTop - 0.3F}, {2.0F, 0.6F}, layers::sensor);
    pit.get<Collider>()->isTrigger = true;
    pit.add<Hazard>();
    Entity &victim = broken.character("Victim", {0.0F, restingHeight - 1.0F});
    victim.get<Killable>()->deathEffect.path = "fx/missing.ykprefab";
    victim.get<Killable>()->respawnEffect.path = "fx/missing.ykprefab";
    victim.get<Killable>()->respawnDelay = 0.5F;
    setLogStderrEnabled(false);
    broken.start();
    broken.tick(30);
    CHECK(broken.happened("entity_died"));
    broken.tick(60);
    setLogStderrEnabled(true);
    CHECK(broken.happened("entity_respawned"));
}

// Collectibles count themselves for HUD text and can spawn a sparkle where they were picked up.
void collectTotalsAndEffects() {
    World w;
    Scene sparkScene(w.registry, 8);
    Entity &sparkle = sparkScene.createEntity("Sparkle");
    sparkle.add<Lifetime>().seconds = 0.3F;
    w.assets.files["fx/sparkle.ykprefab"] = subtreeToJson(sparkScene, sparkle.id()).dump();
    w.ground();
    for (int i = 0; i < 3; ++i) {
        Entity &gem = w.box(("Gem" + std::to_string(i)).c_str(),
                            {static_cast<float>(i) * 2.0F + 1.0F, floorTop - 0.4F}, {0.5F, 0.5F},
                            layers::sensor);
        gem.get<Collider>()->isTrigger = true;
        auto &item = gem.add<Collectible>();
        item.variable = "gems";
        item.value = i == 2 ? 5.0F : 1.0F; // 1 + 1 + 5.
        item.collectEffect.path = "fx/sparkle.ykprefab";
    }
    w.character("Hero", {-1.0F, restingHeight});
    w.start();
    w.tick(1); // Components start during the first tick.
    CHECK_NEAR(w.runtime->blackboard().number("gems_total"), 7.0);
    CHECK_NEAR(w.runtime->blackboard().number("gems"), 0.0);
    CHECK(w.runtime->blackboard().format("{gems:0}/{gems_total}") == "0/7");
    w.tick(30);
    w.down(Key::D);
    w.tick(75);
    w.up(Key::D);
    CHECK_NEAR(w.runtime->blackboard().number("gems"), 7.0);
    CHECK(w.runtime->blackboard().format("{gems:0}/{gems_total}") == "7/7");
    CHECK(w.runtime->scene().findByName("Sparkle") != nullptr); // Spawned at a pickup...
    w.tick(60);
    CHECK(w.runtime->scene().findByName("Sparkle") == nullptr); // ...and gone again.
    // A restart recounts from scratch instead of piling on.
    CHECK(w.runtime->restart());
    CHECK_NEAR(w.runtime->blackboard().number("gems_total"), 0.0); // Not started yet after rebuild.
    w.tick(1);
    CHECK_NEAR(w.runtime->blackboard().number("gems_total"), 7.0);
    CHECK_NEAR(w.runtime->blackboard().number("gems"), 0.0);
}

// Mechanisms tell their AnimatedSprite what state they are in, instead of tinting the sprite, so
// art states come from the animation controller.
constexpr const char *mechanismAnimation = R"({"format":"yk.animation","version":2,"columns":2,"rows":1,
    "clips":[{"name":"off","first":0,"count":1},{"name":"on","first":1,"count":1}]})";
constexpr const char *mechanismController = R"({"format":"yk.animator","version":1,
    "parameters":[{"name":"pressed","type":"bool"},{"name":"on","type":"bool"},
                  {"name":"satisfied","type":"bool"},{"name":"open","type":"bool"}],
    "states":[{"name":"Off","clip":"off"},{"name":"On","clip":"on"}],
    "transitions":[
      {"from":"Off","to":"On","when":[{"parameter":"pressed"}]},
      {"from":"Off","to":"On","when":[{"parameter":"on"}]},
      {"from":"Off","to":"On","when":[{"parameter":"satisfied"}]},
      {"from":"Off","to":"On","when":[{"parameter":"open"}]},
      {"from":"On","to":"Off","when":[{"parameter":"pressed","value":false},{"parameter":"on","value":false},
                                       {"parameter":"satisfied","value":false},{"parameter":"open","value":false}]}]})";

void mechanismsPublishState() {
    World w;
    w.assets.files["m.ykanim"] = mechanismAnimation;
    w.assets.files["m.ykctl"] = mechanismController;
    w.ground();
    const auto animate = [&](Entity &entity) {
        auto &animated = entity.add<AnimatedSprite>();
        animated.animation.path = "m.ykanim";
        animated.controller.path = "m.ykctl";
        entity.get<SpriteRenderer>()->color = {10, 20, 30, 255};
    };
    Entity &door = w.box("Door", {8.0F, floorTop - 1.5F}, {0.6F, 3.0F});
    door.add<Door>().openOffset = {0.0F, -3.0F};
    Entity &plate = w.box("Plate", {2.0F, floorTop - 0.1F}, {1.0F, 0.2F}, layers::sensor);
    plate.get<Collider>()->isTrigger = true;
    plate.add<PressurePlate>().targets = {door.id()};
    Entity &exit = w.box("Exit", {5.0F, floorTop - 0.9F}, {1.2F, 1.8F}, layers::sensor);
    exit.get<Collider>()->isTrigger = true;
    exit.add<Goal>();
    animate(plate);
    animate(exit);
    animate(door);
    w.character("Hero", {0.0F, restingHeight});
    w.start();
    w.tick(30);
    const auto stateOf = [&](const char *name) { return w.at(name).get<AnimatedSprite>()->state(); };
    CHECK(stateOf("Plate") == "Off" && stateOf("Exit") == "Off" && stateOf("Door") == "Off");
    w.down(Key::D);
    w.tick(20);
    w.up(Key::D);
    CHECK(w.at("Plate").get<PressurePlate>()->pressed());
    CHECK(stateOf("Plate") == "On");                          // pressed
    CHECK(w.at("Plate").get<SpriteRenderer>()->color == Color{10, 20, 30, 255}); // Not tinted.
    w.tick(60);
    CHECK(stateOf("Door") == "On"); // open
    w.down(Key::D);
    w.tick(40);
    w.up(Key::D);
    CHECK(stateOf("Exit") == "On"); // satisfied
    CHECK(w.at("Exit").get<SpriteRenderer>()->color == Color{10, 20, 30, 255});
}

// Jumping and landing spawn effect prefabs at the feet and play a landing sound.
void controllerEffects() {
    World w;
    Scene dustScene(w.registry, 12);
    Entity &dust = dustScene.createEntity("Dust");
    dust.add<Lifetime>().seconds = 5.0F;
    w.assets.files["fx/jump.ykprefab"] = subtreeToJson(dustScene, dust.id()).dump();
    Scene landScene(w.registry, 13);
    Entity &thud = landScene.createEntity("Thud");
    thud.add<Lifetime>().seconds = 5.0F;
    w.assets.files["fx/land.ykprefab"] = subtreeToJson(landScene, thud.id()).dump();
    w.ground();
    Entity &hero = w.character("Hero", {0.0F, restingHeight});
    auto *controller = hero.get<PlatformerController>();
    controller->jumpEffect.path = "fx/jump.ykprefab";
    controller->landEffect.path = "fx/land.ykprefab";
    controller->landSound.path = "tone:120,0.1";
    w.start();
    w.tick(30);
    CHECK(w.runtime->scene().findByName("Dust") == nullptr);
    w.down(Key::W);
    w.tick(3);
    const Entity *jumpDust = w.runtime->scene().findByName("Dust");
    CHECK(jumpDust != nullptr);
    if (jumpDust) // At the feet: the bottom of the 0.95 m collider.
        CHECK_NEAR(jumpDust->worldPosition().y, floorTop, 0.15);
    w.tick(50);
    w.up(Key::W);
    w.tick(80);
    CHECK(w.runtime->scene().findByName("Thud") != nullptr); // A real fall: landing effect...
    CHECK(w.audio.count("tone:120,0.1") == 1);                // ...and sound, once.
    // Stepping down a small height is not a landing.
    World gentle;
    Scene ignored(gentle.registry, 14);
    Entity &quiet = ignored.createEntity("Thud");
    quiet.add<Lifetime>().seconds = 5.0F;
    gentle.assets.files["fx/land.ykprefab"] = subtreeToJson(ignored, quiet.id()).dump();
    gentle.ground();
    Entity &walker = gentle.character("Hero", {0.0F, restingHeight - 0.3F});
    walker.get<PlatformerController>()->landEffect.path = "fx/land.ykprefab";
    gentle.start();
    gentle.tick(60);
    CHECK(gentle.runtime->scene().findByName("Thud") == nullptr);
}

void controllerBasics() {
    World w;
    w.ground();
    w.character("Hero", {0, restingHeight - 1.5F});
    w.start();
    w.tick(90);
    auto *controller = w.at("Hero").get<PlatformerController>();
    CHECK(controller->grounded());
    CHECK_NEAR(w.position("Hero").y, restingHeight, 0.04);
    const Vec2 settled = w.position("Hero");
    w.tick(180);
    CHECK_NEAR(w.position("Hero").x, settled.x, 0.005); // Standing still stays still.
    CHECK_NEAR(w.position("Hero").y, settled.y, 0.005);

    w.down(Key::D);
    w.tick(60);
    const float ranX = w.position("Hero").x - settled.x;
    CHECK(ranX > 4.7F && ranX < 5.6F); // One second at 5.5 m/s minus the acceleration ramp.
    CHECK(controller->facing() == 1 && controller->grounded());
    const float speed =
        w.runtime->physics().state(*w.runtime->bodyOf(w.at("Hero").id())).value().linearVelocity.x;
    CHECK_NEAR(speed, 5.5, 0.15);
    w.up(Key::D);
    w.tick(45);
    const float stoppedX = w.position("Hero").x;
    w.tick(30);
    CHECK_NEAR(w.position("Hero").x, stoppedX, 0.01); // Came to rest and holds.
    CHECK(stoppedX - settled.x - ranX < 0.5F);        // Deceleration is quick.
    w.down(Key::A);
    w.tick(20);
    CHECK(controller->facing() == -1 && w.position("Hero").x < stoppedX - 1.0F);
    CHECK(w.at("Hero").get<SpriteRenderer>()->flipX);
}

float jumpApex(World &w, int holdTicks) {
    const float start = w.position("Hero").y;
    w.down(Key::W);
    float highest = start;
    for (int i = 0; i < 120; ++i) {
        if (i == holdTicks)
            w.up(Key::W);
        w.tick();
        highest = std::min(highest, w.position("Hero").y);
    }
    w.up(Key::W); // Each measurement starts from a released key so its press is a fresh edge.
    return start - highest;
}

void jumping() {
    World w;
    w.ground();
    w.character("Hero", {0, restingHeight});
    w.start();
    w.tick(60);
    const float full = jumpApex(w, 1000);
    CHECK(full > 2.1F && full < 2.45F); // jumpHeight is 2.3 m.
    w.tick(60);
    auto *controller = w.at("Hero").get<PlatformerController>();
    CHECK(controller->grounded() && std::fabs(w.position("Hero").y - restingHeight) < 0.05F);
    const float hop = jumpApex(w, 3);
    CHECK(hop > 0.2F && hop < full * 0.6F); // Releasing early makes a shorter hop.
    // Holding the key does not bunny-hop: a jump needs a fresh press.
    w.tick(60);
    w.down(Key::W);
    w.tick(1);
    w.tick(120);
    CHECK(w.position("Hero").y > restingHeight - 0.1F);
    w.up(Key::W);
    // Sound requests happen on jumps.
    w.tick(30);
    w.at("Hero").get<PlatformerController>()->jumpSound.path = "tone:600,0.05";
    w.down(Key::W);
    w.tick(2);
    CHECK(w.audio.count("tone:600,0.05") == 1);
}

void coyoteAndBuffer() {
    World w;
    w.ground(-40, 0);
    w.character("Hero", {-1.0F, restingHeight});
    w.start();
    w.tick(30);
    w.down(Key::D);
    int ticksAirborne = 0;
    for (int i = 0; i < 120 && ticksAirborne == 0; ++i) { // Walk off the edge.
        w.tick();
        if (!w.at("Hero").get<PlatformerController>()->grounded())
            ticksAirborne = 1;
    }
    CHECK(ticksAirborne == 1);
    w.tick(2); // Still within the 0.1 s coyote window.
    w.down(Key::W);
    w.tick(3);
    const float vy =
        w.runtime->physics().state(*w.runtime->bodyOf(w.at("Hero").id())).value().linearVelocity.y;
    CHECK(vy < -2.0F); // The late jump was honored.

    World late;
    late.ground(-40, 0);
    late.character("Hero", {-1.0F, restingHeight});
    late.start();
    late.tick(30);
    late.down(Key::D);
    for (int i = 0; i < 120 && late.at("Hero").get<PlatformerController>()->grounded(); ++i)
        late.tick();
    late.tick(14); // Well beyond the coyote window.
    late.down(Key::W);
    late.tick(3);
    CHECK(late.runtime->physics()
              .state(*late.runtime->bodyOf(late.at("Hero").id()))
              .value()
              .linearVelocity.y > 0.0F);

    World buffered;
    buffered.ground();
    buffered.character("Hero", {0, restingHeight - 1.2F});
    buffered.start();
    bool pressed = false;
    float highestAfterLanding = 1000;
    for (int i = 0; i < 90; ++i) {
        const auto state = buffered.runtime->physics()
                               .state(*buffered.runtime->bodyOf(buffered.at("Hero").id()))
                               .value();
        if (!pressed && state.linearVelocity.y > 1.0F &&
            buffered.position("Hero").y > restingHeight - 0.25F) {
            buffered.down(Key::W); // Pressed slightly before touching down.
            pressed = true;
        }
        buffered.tick();
        if (pressed)
            highestAfterLanding = std::min(highestAfterLanding, buffered.position("Hero").y);
    }
    CHECK(pressed &&
          highestAfterLanding < restingHeight - 1.0F); // The buffered jump fired on landing.
}

void wallsAndSlopes() {
    // Pressing into a wall in the air must not slow the fall (no wall sticking).
    World wall;
    wall.ground();
    wall.box("Wall", {3.0F, 0.0F}, {1.0F, 30.0F});
    wall.character("Hero", {2.0F, -4.0F});
    wall.character("Reference", {-10.0F, -4.0F});
    wall.start();
    wall.down(Key::D);
    wall.tick(40);
    CHECK_NEAR(wall.position("Hero").y, wall.position("Reference").y, 0.05);
    CHECK(wall.position("Hero").x < 2.6F); // Blocked by the wall.

    // Walking up a 20 degree ramp, then standing on it without creeping.
    World ramp;
    ramp.ground();
    // Centered so its top surface meets the floor at x = 1 (the low end is sunk into the ground).
    Entity &slope = ramp.box("Ramp", {6.81F, floorTop - 1.51F}, {12.0F, 0.5F});
    slope.transform().rotationDegrees = -17; // Rises toward +x.
    ramp.character("Hero", {-2.0F, restingHeight});
    ramp.start();
    ramp.tick(40);
    ramp.down(Key::D);
    ramp.tick(110);
    CHECK(ramp.position("Hero").y < restingHeight - 1.5F); // Climbed.
    CHECK(ramp.at("Hero").get<PlatformerController>()->grounded());
    ramp.up(Key::D);
    ramp.tick(45);
    const Vec2 rest = ramp.position("Hero");
    ramp.tick(120);
    CHECK_NEAR(ramp.position("Hero").x, rest.x, 0.03); // Grip friction holds it on the slope.
    CHECK_NEAR(ramp.position("Hero").y, rest.y, 0.03);
}

void platformsCarryRiders() {
    World w;
    w.ground();
    Entity &shuttle = w.box("Shuttle", {0.0F, floorTop - 1.0F}, {4.0F, 0.4F});
    auto &platform = shuttle.add<MovingPlatform>();
    platform.travel = {10.0F, 0.0F};
    platform.speed = 2.0F;
    platform.pause = 0.0F;
    Entity &lift = w.box("Lift", {-12.0F, floorTop - 1.0F}, {4.0F, 0.4F});
    auto &elevator = lift.add<MovingPlatform>();
    elevator.travel = {0.0F, -5.0F};
    elevator.speed = 2.0F;
    w.character("Rider", {0.0F, floorTop - 1.0F - 0.2F - 0.475F});
    w.character("Climber", {-12.0F, floorTop - 1.0F - 0.2F - 0.475F}, "Nobody");
    w.start();
    w.tick(30);
    const float riderStart = w.position("Rider").x, shuttleStart = w.position("Shuttle").x;
    const float climberOffset = w.position("Climber").y - w.position("Lift").y;
    w.tick(120); // Two seconds at 2 m/s.
    CHECK(w.position("Shuttle").x - shuttleStart > 3.5F);
    CHECK_NEAR(w.position("Rider").x - riderStart, w.position("Shuttle").x - shuttleStart, 0.25);
    CHECK(w.at("Rider").get<PlatformerController>()->grounded());
    CHECK(w.position("Lift").y < floorTop - 1.0F - 3.0F); // The lift rose meanwhile...
    CHECK_NEAR(w.position("Climber").y - w.position("Lift").y, climberOffset,
               0.08); // ...carrying its rider.
    CHECK(w.at("Climber").get<PlatformerController>()->grounded());
}

void pushingAndTwoPlayers() {
    World w;
    w.ground();
    Entity &crate = w.box("Crate", {3.0F, floorTop - 0.5F}, {1.0F, 1.0F}, layers::prop);
    crate.add<RigidBody>();
    w.character("One", {0.0F, restingHeight});
    w.character("Two", {-8.0F, restingHeight}, "Player2");
    w.start();
    w.tick(30);
    const float crateStart = w.position("Crate").x, twoStart = w.position("Two").x;
    w.down(Key::D); // Only player one is controlled by D.
    w.tick(90);
    CHECK(w.position("Crate").x > crateStart + 2.0F); // Pushed along.
    CHECK_NEAR(w.position("Two").x, twoStart, 0.01);  // The other character ignores it.
    w.up(Key::D);
    w.down(Key::Right);
    w.tick(30);
    CHECK(w.position("Two").x > twoStart + 2.0F);
}

// A plate at x=0 with a door at x=8 that it opens.
struct PlateAndDoor {
    World w;
    Entity *plate{};
    Entity *door{};
    PlateAndDoor(bool latch = false, std::vector<std::string> tags = {}) {
        w.ground();
        Entity &p = w.box("Plate", {0.0F, floorTop - 0.1F}, {1.4F, 0.4F}, layers::sensor);
        p.get<Collider>()->isTrigger = true;
        p.add<SpriteRenderer>().size = {1.4F, 0.2F};
        auto &pressure = p.add<PressurePlate>();
        pressure.latch = latch;
        pressure.activatorTags = std::move(tags);
        Entity &d = w.box("Door", {8.0F, floorTop - 1.5F}, {0.6F, 3.0F});
        d.add<RigidBody>().type = RigidBodyType::Kinematic;
        auto &gate = d.add<Door>();
        gate.openOffset = {0.0F, -3.2F};
        gate.speed = 4.0F;
        pressure.targets = {d.id()};
        plate = &p;
        door = &d;
    }
};

void plateOpensDoor() {
    PlateAndDoor level;
    World &w = level.w;
    w.character("Hero", {-4.0F, restingHeight}, "Player1", "hero");
    w.start();
    const float closedY = w.position("Door").y;
    w.tick(60);
    CHECK_NEAR(w.at("Door").get<Door>()->openAmount(), 0.0, 1e-6);
    CHECK_NEAR(w.position("Door").y, closedY, 0.01);
    w.down(Key::D);
    w.tick(90); // Walk onto the plate (x=0) and keep going a little.
    w.up(Key::D);
    w.tick(10);
    const auto *plate = w.at("Plate").get<PressurePlate>();
    CHECK(plate->pressed() == (std::fabs(w.position("Hero").x) < 0.7F));
    // Stand exactly on the plate (forget the events from walking over it on the way).
    w.events.clear();
    w.runtime->teleport(w.at("Hero"), {0.0F, restingHeight});
    w.tick(30);
    CHECK(plate->pressed() && w.happened("plate_pressed"));
    CHECK(w.at("Plate").get<SpriteRenderer>() != nullptr);
    if (auto *sprite = w.at("Plate").get<SpriteRenderer>())
        CHECK_NEAR(sprite->offset.y, plate->pressDepth, 1e-5); // Sinks when pressed.
    w.tick(60);
    CHECK_NEAR(w.at("Door").get<Door>()->openAmount(), 1.0, 1e-6);
    CHECK_NEAR(w.position("Door").y, closedY - 3.2F, 0.05);
    CHECK(w.happened("door_opened") && !w.happened("door_closed"));
    // Leave: the door closes again.
    w.runtime->teleport(w.at("Hero"), {-4.0F, restingHeight});
    w.tick(120);
    CHECK(!plate->pressed() && w.happened("plate_released") && w.happened("door_closed"));
    CHECK_NEAR(w.position("Door").y, closedY, 0.05);
    if (auto *sprite = w.at("Plate").get<SpriteRenderer>())
        CHECK_NEAR(sprite->offset.y, 0.0, 1e-5);
}

void doorBlocksAndLatches() {
    PlateAndDoor level(true);
    World &w = level.w;
    w.character("Hero", {4.0F, restingHeight}, "Player1", "hero");
    w.start();
    w.down(Key::D);
    w.tick(150); // The closed door stops the character.
    CHECK(w.position("Hero").x < 7.5F &&
          w.position("Hero").x > 7.2F); // Stops at the door face (7.7 - radius 0.3).
    w.up(Key::D);
    w.runtime->teleport(w.at("Hero"), {0.0F, restingHeight});
    w.tick(20);
    w.runtime->teleport(w.at("Hero"),
                        {-6.0F, restingHeight}); // Step off: a latched plate stays pressed.
    w.tick(120);
    CHECK(w.at("Plate").get<PressurePlate>()->pressed());
    CHECK_NEAR(w.at("Door").get<Door>()->openAmount(), 1.0, 1e-6);
    w.down(Key::D);
    w.tick(180); // Now the character can walk through the open door.
    CHECK(w.position("Hero").x > 9.0F);
}

void plateFiltersAndCombines() {
    // A crate presses a plate that has no tag filter; a tag filter excludes it.
    for (const bool filtered : {false, true}) {
        PlateAndDoor level(false, filtered ? std::vector<std::string>{"hero"}
                                           : std::vector<std::string>{});
        World &w = level.w;
        Entity &crate = w.box("Crate", {0.0F, floorTop - 2.0F}, {0.8F, 0.8F}, layers::prop);
        crate.add<RigidBody>();
        w.start();
        w.tick(120);
        CHECK(w.at("Plate").get<PressurePlate>()->pressed() == !filtered);
        CHECK(std::fabs(w.at("Door").get<Door>()->openAmount() - (filtered ? 0.0F : 1.0F)) < 1e-4F);
    }
    // Static level geometry overlapping a plate's sensor never presses it.
    {
        PlateAndDoor level;
        level.w.box("Overlap", {0.0F, floorTop - 0.1F}, {2.0F, 0.5F},
                    layers::prop); // Movable layer, but static body.
        level.w.start();
        level.w.tick(60);
        CHECK(!level.w.at("Plate").get<PressurePlate>()->pressed());
    }
    // Two plates with All logic: both must be held.
    {
        World w;
        w.ground();
        Entity &door = w.box("Door", {0.0F, floorTop - 6.0F}, {0.6F, 3.0F});
        door.add<RigidBody>().type = RigidBodyType::Kinematic;
        auto &gate = door.add<Door>();
        gate.logic = SignalLogic::All;
        gate.speed = 8.0F;
        for (const float x : {-6.0F, 6.0F}) {
            Entity &plate =
                w.box(x < 0 ? "Left" : "Right", {x, floorTop - 0.1F}, {1.4F, 0.4F}, layers::sensor);
            plate.get<Collider>()->isTrigger = true;
            plate.add<PressurePlate>().targets = {door.id()};
        }
        w.character("A", {-6.0F, restingHeight});
        w.character("B", {6.0F, restingHeight}, "Player2");
        w.start();
        w.tick(90);
        CHECK(w.at("Left").get<PressurePlate>()->pressed() &&
              w.at("Right").get<PressurePlate>()->pressed());
        CHECK_NEAR(w.at("Door").get<Door>()->openAmount(), 1.0, 1e-6);
        w.runtime->teleport(w.at("B"), {14.0F, restingHeight});
        w.tick(90);
        CHECK(w.at("Left").get<PressurePlate>()->pressed() &&
              !w.at("Right").get<PressurePlate>()->pressed());
        CHECK_NEAR(w.at("Door").get<Door>()->openAmount(), 0.0, 1e-6); // One plate is not enough.
    }
    // Inverted receiver: open by default, closes while signalled.
    {
        PlateAndDoor level;
        level.door->get<Door>()->invert = true;
        level.w.character("Hero", {0.0F, restingHeight});
        level.w.start();
        level.w.tick(90);
        CHECK(level.w.at("Plate").get<PressurePlate>()->pressed());
        CHECK_NEAR(level.w.at("Door").get<Door>()->openAmount(), 0.0, 1e-6);
        level.w.runtime->teleport(level.w.at("Hero"), {-8.0F, restingHeight});
        level.w.tick(90);
        CHECK_NEAR(level.w.at("Door").get<Door>()->openAmount(), 1.0, 1e-6);
    }
}

void leversAndGatedPlatforms() {
    World w;
    w.ground();
    Entity &lever = w.box("Lever", {0.0F, floorTop - 0.4F}, {0.5F, 0.8F}, layers::sensor);
    lever.get<Collider>()->isTrigger = true;
    auto &switcher = lever.add<Lever>();
    Entity &platform = w.box("Mover", {6.0F, floorTop - 4.0F}, {2.0F, 0.4F});
    auto &mover = platform.add<MovingPlatform>();
    mover.travel = {6.0F, 0.0F};
    mover.speed = 2.0F;
    mover.pause = 0.0F;
    mover.requireSignal = true;
    switcher.targets = {platform.id()};
    w.character("Hero", {-3.0F, restingHeight});
    w.start();
    const float startX = w.position("Mover").x;
    w.tick(60);
    CHECK_NEAR(w.position("Mover").x, startX, 1e-3); // Gated: idle until the lever flips.
    w.down(Key::D);
    w.tick(60); // Walk through the lever.
    w.up(Key::D);
    CHECK(w.at("Lever").get<Lever>()->on() && w.count("lever_toggled") == 1);
    w.tick(60);
    CHECK(w.position("Mover").x > startX + 1.0F); // Now it moves.
    w.down(Key::A);
    w.tick(120); // Walk back through it: flips off.
    CHECK(!w.at("Lever").get<Lever>()->on() && w.count("lever_toggled") == 2);
    const float parked = w.position("Mover").x;
    w.tick(60);
    CHECK_NEAR(w.position("Mover").x, parked, 1e-3);
}

// Runs until `done` holds or `limit` ticks pass; returns whether it held.
template <class Done> bool tickUntil(World &w, int limit, Done done) {
    for (int i = 0; i < limit; ++i) {
        if (done())
            return true;
        w.tick();
    }
    return done();
}

void hazardsKillTheRightCharacters() {
    World w;
    w.ground();
    Entity &lava = w.box("Lava", {0.0F, floorTop - 0.25F}, {4.0F, 0.5F}, layers::sensor);
    lava.get<Collider>()->isTrigger = true;
    lava.add<Hazard>().affectsTags = {"water"};
    w.character("Fire", {-6.0F, restingHeight}, "Player1", "fire");
    w.character("Water", {-9.0F, restingHeight}, "Player2", "water");
    w.start();
    w.tick(30);
    w.down(Key::D);
    w.down(Key::Right);
    const auto waterDead = [&] { return !w.at("Water").get<Killable>()->alive(); };
    CHECK(tickUntil(w, 400, waterDead));
    w.up(Key::Right);
    w.tick(150); // Fire keeps walking through the lava unharmed.
    w.up(Key::D);
    CHECK(w.at("Fire").get<Killable>()->alive() && w.position("Fire").x > 3.0F);
    CHECK(w.count("entity_died") == 1 && w.runtime->blackboard().number("deaths") == 1);
    CHECK(w.happened("entity_respawned") == false || w.count("entity_respawned") <= 1);
    // The dead one is removed from play until it respawns: no sprite, no body, ignores input.
    const auto waterAlive = [&] { return w.at("Water").get<Killable>()->alive(); };
    CHECK(waterAlive()); // Respawned by now (delay is 1 s).
    CHECK(w.happened("entity_respawned"));
    CHECK_NEAR(w.position("Water").x, -9.0, 0.3); // Back at its start position (no checkpoint).
    CHECK(w.at("Water").get<SpriteRenderer>()->visible);

    // While dead: hidden, disabled, deaf to input.
    World d;
    d.ground();
    Entity &spikes = d.box("Spikes", {0.0F, floorTop - 0.25F}, {2.0F, 0.5F}, layers::sensor);
    spikes.get<Collider>()->isTrigger = true;
    spikes.add<Hazard>();
    Entity &hero = d.character("Hero", {0.0F, restingHeight - 1.0F});
    hero.get<Killable>()->respawnDelay = 5.0F;
    d.start();
    d.tick(60);
    CHECK(!d.at("Hero").get<Killable>()->alive() && !d.at("Hero").get<SpriteRenderer>()->visible);
    CHECK(!d.at("Hero").get<RigidBody>()->enabled && !d.at("Hero").get<Collider>()->enabled);
    const Vec2 corpse = d.position("Hero");
    d.down(Key::D);
    d.tick(60);
    CHECK_NEAR(d.position("Hero").x, corpse.x, 1e-4);
    CHECK(d.runtime->overlapping(d.at("Spikes").id())
              .empty()); // A dead body no longer overlaps anything.
    d.up(Key::D);
}

void checkpointsAndSpawnPoints() {
    World w;
    w.ground();
    Entity &checkpoint = w.box("Checkpoint", {2.0F, floorTop - 0.6F}, {0.6F, 1.2F}, layers::sensor);
    checkpoint.get<Collider>()->isTrigger = true;
    checkpoint.add<Checkpoint>();
    Entity &lava = w.box("Lava", {8.0F, floorTop - 0.25F}, {2.0F, 0.5F}, layers::sensor);
    lava.get<Collider>()->isTrigger = true;
    lava.add<Hazard>();
    Entity &hero = w.character("Hero", {-4.0F, restingHeight - 2.0F});
    Entity &marker = w.scene->createEntity("Start");
    marker.transform().position = {-2.0F, restingHeight};
    marker.add<SpawnPoint>().character = hero.id();
    w.start();
    w.tick(60);
    CHECK_NEAR(w.position("Hero").x, -2.0, 0.05); // The spawn point placed the character.
    w.down(Key::D);
    CHECK(tickUntil(w, 400, [&] { return !w.at("Hero").get<Killable>()->alive(); }));
    CHECK(w.happened("checkpoint_reached"));
    w.up(Key::D);
    CHECK(tickUntil(w, 200, [&] { return w.at("Hero").get<Killable>()->alive(); }));
    CHECK(w.position("Hero").x > 1.5F &&
          w.position("Hero").x < 2.5F); // Respawned at the checkpoint.
    CHECK(w.at("Checkpoint").get<SpriteRenderer>() == nullptr);

    // An explicit spawnPoint on the Killable overrides checkpoints.
    World v;
    v.ground();
    Entity &cp = v.box("Checkpoint", {2.0F, floorTop - 0.6F}, {0.6F, 1.2F}, layers::sensor);
    cp.get<Collider>()->isTrigger = true;
    cp.add<Checkpoint>();
    Entity &pit = v.box("Pit", {6.0F, floorTop - 0.25F}, {2.0F, 0.5F}, layers::sensor);
    pit.get<Collider>()->isTrigger = true;
    pit.add<Hazard>();
    Entity &home = v.scene->createEntity("Home");
    home.transform().position = {-10.0F, restingHeight};
    Entity &runner = v.character("Runner", {-2.0F, restingHeight});
    runner.get<Killable>()->spawnPoint = home.id();
    v.start();
    v.tick(30);
    v.down(Key::D);
    CHECK(tickUntil(v, 400, [&] { return !v.at("Runner").get<Killable>()->alive(); }));
    v.up(Key::D);
    CHECK(tickUntil(v, 200, [&] { return v.at("Runner").get<Killable>()->alive(); }));
    CHECK_NEAR(v.position("Runner").x, -10.0, 0.1);
}

void collectibles() {
    World w;
    w.ground();
    Entity &gem = w.box("Gem", {0.0F, floorTop - 0.6F}, {0.5F, 0.5F}, layers::sensor);
    gem.get<Collider>()->isTrigger = true;
    auto &item = gem.add<Collectible>();
    item.collectorTags = {"fire"};
    item.variable = "fire_gems";
    item.value = 2.0F;
    item.sound.path = "tone:880,0.1";
    w.character("Water", {-2.0F, restingHeight}, "Player1", "water");
    w.character("Fire", {2.0F, restingHeight}, "Player2", "fire");
    w.start();
    w.tick(30);
    w.runtime->teleport(w.at("Water"), {0.0F, restingHeight});
    w.tick(30);
    CHECK(w.at("Gem").active() && !w.runtime->blackboard().has("fire_gems")); // Wrong collector.
    w.runtime->teleport(w.at("Water"), {-6.0F, restingHeight});
    w.tick(10);
    w.runtime->teleport(w.at("Fire"), {0.0F, restingHeight});
    w.tick(10);
    CHECK(!w.at("Gem").active() && w.runtime->blackboard().number("fire_gems") == 2.0);
    CHECK(w.audio.count("tone:880,0.1") == 1 && w.count("collected") == 1);
    w.runtime->teleport(w.at("Fire"), {6.0F, restingHeight});
    w.tick(10);
    w.runtime->teleport(w.at("Fire"), {0.0F, restingHeight});
    w.tick(30);
    CHECK(w.runtime->blackboard().number("fire_gems") ==
          2.0); // Already collected: no second pickup.

    // Two collectors arriving in the same tick collect it once.
    World both;
    both.ground();
    Entity &pearl = both.box("Pearl", {0.0F, floorTop - 0.6F}, {0.6F, 0.6F}, layers::sensor);
    pearl.get<Collider>()->isTrigger = true;
    pearl.add<Collectible>();
    both.character("A", {-0.15F, restingHeight - 1.0F});
    both.character("B", {0.15F, restingHeight - 1.0F}, "Player2");
    both.start();
    both.tick(60);
    CHECK(both.runtime->blackboard().number("score") == 1.0);
}

void goalsAndZones() {
    World w;
    w.ground();
    Entity &exit = w.box("Exit", {0.0F, floorTop - 0.9F}, {1.4F, 1.8F}, layers::sensor);
    exit.get<Collider>()->isTrigger = true;
    auto &goal = exit.add<Goal>();
    goal.requiredTag = "fire";
    Entity &gate = w.box("Gate", {6.0F, floorTop - 6.0F}, {0.6F, 3.0F});
    gate.add<RigidBody>().type = RigidBodyType::Kinematic;
    gate.add<Door>().speed = 10.0F;
    goal.targets = {gate.id()};
    Entity &zone = w.box("Zone", {-8.0F, floorTop - 1.5F}, {2.0F, 3.0F}, layers::sensor);
    zone.get<Collider>()->isTrigger = true;
    auto &region = zone.add<TriggerZone>();
    region.enterEvent = "zone_in";
    region.exitEvent = "zone_out";
    region.once = true;
    w.character("Water", {-2.0F, restingHeight}, "Player2", "water");
    w.character("Fire", {-5.0F, restingHeight}, "Player1", "fire");
    w.start();
    w.tick(30);
    w.runtime->teleport(w.at("Water"), {0.0F, restingHeight});
    w.tick(30);
    CHECK(!w.at("Exit").get<Goal>()->satisfied() && !w.happened("goal_reached")); // Wrong tag.
    w.runtime->teleport(w.at("Fire"), {0.0F, restingHeight - 0.5F});
    w.tick(30);
    CHECK(w.at("Exit").get<Goal>()->satisfied() && w.count("goal_reached") == 1);
    w.tick(60);
    CHECK_NEAR(w.at("Gate").get<Door>()->openAmount(), 1.0, 1e-6); // Goals can drive receivers too.
    w.at("Fire").get<Killable>()->kill(*w.runtime, {}); // A dead character does not count.
    w.tick(5);
    CHECK(!w.at("Exit").get<Goal>()->satisfied() && w.count("goal_left") == 1);

    // Zone events fire once.
    w.runtime->teleport(w.at("Water"), {-8.0F, restingHeight});
    w.tick(20);
    CHECK(w.count("zone_in") == 1 && w.at("Zone").get<TriggerZone>()->occupied());
    w.runtime->teleport(w.at("Water"), {-2.0F, restingHeight});
    w.tick(20);
    CHECK(w.count("zone_out") == 1 && !w.at("Zone").get<TriggerZone>()->occupied());
    w.runtime->teleport(w.at("Water"), {-8.0F, restingHeight});
    w.tick(20);
    CHECK(w.count("zone_in") == 1); // once = true.
}

void defaultsOnAdd() {
    World w;
    Entity &entity = w.scene->createEntity("Plate");
    CHECK(!entity.add<Collider>().isTrigger);
    entity.add<PressurePlate>();
    CHECK(entity.get<Collider>()->isTrigger); // Adding the plate configured its collider.
    Entity &hero = w.scene->createEntity("Hero");
    hero.add<PlatformerController>();
    CHECK(hero.get<RigidBody>()->fixedRotation &&
          hero.get<RigidBody>()->type == RigidBodyType::Dynamic &&
          hero.get<Collider>()->shape == ColliderShape::Capsule &&
          hero.get<Collider>()->friction == 0.0F);
    Entity &door = w.scene->createEntity("Door");
    door.add<Door>();
    CHECK(door.get<RigidBody>()->type == RigidBodyType::Kinematic);
    // Loading saved data never re-applies those defaults over saved values.
    entity.get<Collider>()->isTrigger = false;
    const Json saved = sceneToJson(*w.scene);
    auto loaded = sceneFromJson(saved, w.registry);
    CHECK(loaded);
    if (loaded)
        CHECK(!loaded.value()->findByName("Plate")->get<Collider>()->isTrigger);
}

void persistenceAndPrefabs() {
    PlateAndDoor level;
    level.w.character("Hero", {0.0F, restingHeight}, "Player1", "hero");
    Entity &lever = level.w.box("Lever", {-6.0F, floorTop - 0.4F}, {0.5F, 0.8F}, layers::sensor);
    lever.get<Collider>()->isTrigger = true;
    lever.add<Lever>().targets = {level.door->id()};
    const Json saved = sceneToJson(*level.w.scene);
    auto loaded = sceneFromJson(saved, level.w.registry);
    CHECK(loaded);
    if (!loaded)
        return;
    CHECK(sceneToJson(*loaded.value()) == saved);
    World w;
    w.scene = std::move(loaded.value());
    w.start();
    w.tick(90);
    CHECK(w.at("Plate").get<PressurePlate>()->pressed()); // References survived the round trip.
    CHECK_NEAR(w.at("Door").get<Door>()->openAmount(), 1.0, 1e-6);

    // A reusable puzzle: a plate and its door under one parent, instantiated twice.
    World p;
    Entity &puzzle = p.scene->createEntity("Puzzle");
    Entity &plate = p.box("Plate", {0.0F, floorTop - 0.1F}, {1.4F, 0.4F}, layers::sensor);
    plate.get<Collider>()->isTrigger = true;
    Entity &door = p.box("Door", {5.0F, floorTop - 1.5F}, {0.6F, 3.0F});
    door.add<RigidBody>().type = RigidBodyType::Kinematic;
    door.add<Door>();
    plate.add<PressurePlate>().targets = {door.id()};
    p.scene->setParent(plate.id(), puzzle.id());
    p.scene->setParent(door.id(), puzzle.id());
    const Json prefab = subtreeToJson(*p.scene, puzzle.id());
    p.scene->destroy(puzzle.id());
    p.ground();
    auto first = instantiateSubtree(*p.scene, prefab, {}, Vec2{-20.0F, 0.0F});
    auto second = instantiateSubtree(*p.scene, prefab, {}, Vec2{10.0F, 0.0F});
    CHECK(first && second);
    p.character("Hero", {10.0F, restingHeight});
    p.start();
    p.tick(120);
    int opened = 0, plates = 0;
    p.runtime->scene().forEach([&](Entity &entity) {
        if (auto *mechanism = entity.get<Door>())
            opened += mechanism->openAmount() > 0.99F ? 1 : 0;
        if (entity.get<PressurePlate>())
            ++plates;
    });
    CHECK(plates == 2 && opened == 1); // Only the copy the character stands on opens its own door.
}

void templatesWork() {
    World w;
    CHECK(w.registry.validate());
    CHECK(w.registry.templates().size() >= 15);
    Vec2 at{-30.0F, 0.0F};
    for (const EntityTemplate &entry : w.registry.templates()) {
        const EntityId id = entry.create(*w.scene, at);
        Entity *entity = w.scene->find(id);
        CHECK(entity != nullptr);
        if (entity)
            CHECK_NEAR(entity->worldPosition().x, at.x, 1e-4);
        at.x += 4.0F;
    }
    const Json saved = sceneToJson(*w.scene);
    auto loaded = sceneFromJson(saved, w.registry);
    CHECK(loaded && sceneToJson(*loaded.value()) == saved);
    w.ground();
    w.start();
    w.tick(120); // Every template survives simulation.
    CHECK(w.runtime->tick() == 120);
}
} // namespace

int main() {
    controllerBasics();
    characterAnimation();
    oneWayAndWedge();
    interactLever();
    interactAnimation();
    deathAndRespawn();
    collectTotalsAndEffects();
    mechanismsPublishState();
    controllerEffects();
    jumping();
    coyoteAndBuffer();
    wallsAndSlopes();
    platformsCarryRiders();
    pushingAndTwoPlayers();
    plateOpensDoor();
    doorBlocksAndLatches();
    plateFiltersAndCombines();
    leversAndGatedPlatforms();
    hazardsKillTheRightCharacters();
    checkpointsAndSpawnPoints();
    collectibles();
    goalsAndZones();
    defaultsOnAdd();
    persistenceAndPrefabs();
    templatesWork();
    return yk::test::finish("gameplay");
}
