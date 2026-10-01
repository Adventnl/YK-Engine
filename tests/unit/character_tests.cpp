// Characters in a running game: the motor honours status effects (held, slowed, no sprinting) and
// spends stamina to run, whoever steers it; a person steers it with named actions; and the look of
// a character is layers that follow its body and change with what it wears.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/core/Log.hpp"
#include "yk/gameplay/Character.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/items/Appearance.hpp"
#include "yk/items/Inventory.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/stats/Stats.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace yk;

namespace {
Json J(const char *text) {
    auto parsed = Json::parse(text);
    CHECK(parsed);
    return parsed ? parsed.value() : Json();
}
bool has(const std::string &text, const char *part) {
    return text.find(part) != std::string::npos;
}

const char *dataText = R"({
  "format": "yk.data", "version": 1,
  "stats": [
    {"id": "stamina", "min": 0, "max": 20, "start": "max", "regen": 5, "regenDelay": 1},
    {"id": "health", "max": 100}
  ],
  "effects": [
    {"id": "stunned", "duration": 1, "flags": ["no_move"]},
    {"id": "slowed", "factors": {"move.speed": 0.5}},
    {"id": "winded", "flags": ["no_sprint"]},
    {"id": "frozen", "factors": {"move.speed": 0}}
  ],
  "items": [
    {"id": "guard_outfit", "equip": {"slot": "Outfit", "appearance": {"outfit": "assets/char/guard.png"}}},
    {"id": "inmate_outfit", "equip": {"slot": "Outfit", "appearance": {"outfit": "assets/char/inmate.png"}}},
    {"id": "hood", "equip": {"slot": "Hat", "appearance": {"hat": "assets/char/hood.png", "hair": ""}}},
    {"id": "cap", "equip": {"slot": "Hat", "appearance": {"hat": "assets/char/cap.png"}}}
  ]
})";

struct Rig {
    ComponentRegistry registry;
    MemoryAssets assets;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<GameRuntime> runtime;
    EntityId hero, guard;
    Keyboard keyboard;
    std::vector<GameEvent> heard;

    Rig() {
        registerStandardComponents(registry);
        assets.files["data/character.ykdata"] = dataText;
        scene = std::make_unique<Scene>(registry, 7);
        scene->settings.gravity = {0.0F, 0.0F};
    }
    // A person's character: input, body, motor, controller, stats and effects.
    Entity &person(const char *name, float runCost = 0.0F) {
        Entity &entity = scene->createEntity(name);
        entity.add<PlayerInput>();
        entity.add<StatSet>();
        entity.add<StatusEffects>();
        auto &motor = entity.add<CharacterMotor>();
        motor.runStaminaPerSecond = runCost;
        entity.add<PlayerCharacterController>();
        return entity;
    }
    void start() {
        RuntimeOptions options;
        options.assets = &assets;
        options.layers = layers::standard();
        options.inputMap = InputMap::standard();
        options.inputMap.sets[0].actions.push_back(
            {"Run", {InputBinding::fromKey(Key::LeftShift)}});
        auto created = GameRuntime::create(std::move(scene), options);
        CHECK(created);
        if (!created)
            return;
        runtime = std::move(created.value());
        runtime->events().subscribe("*",
                                    [this](const GameEvent &event) { heard.push_back(event); });
        step();
    }
    void step(int ticks = 1) {
        for (int i = 0; i < ticks; ++i) {
            InputFrame frame;
            frame.keyboard = keyboard;
            runtime->stepOnce(frame);
            keyboard.beginFrame();
        }
    }
    Entity &entity(EntityId id) {
        return *runtime->scene().find(id);
    }
    CharacterMotor &motor(EntityId id) {
        return *entity(id).get<CharacterMotor>();
    }
    StatSet &stats(EntityId id) {
        return *entity(id).get<StatSet>();
    }
    StatusEffects &effects(EntityId id) {
        return *entity(id).get<StatusEffects>();
    }
    int count(const char *name) const {
        return static_cast<int>(
            std::count_if(heard.begin(), heard.end(),
                          [&](const GameEvent &event) { return event.name == name; }));
    }
};

// ---- The motor and the effects on it
// ---------------------------------------------------------------
void walking() {
    Rig rig;
    rig.hero = rig.person("Hero").id();
    rig.start();
    CharacterMotor &motor = rig.motor(rig.hero);
    CHECK(motor.speed() < 0.01F && !motor.running() && !motor.suppressed());
    rig.keyboard.set(Key::D, true);
    rig.step(60);
    CHECK_NEAR(motor.speed(), motor.walkSpeed, 0.05);
    CHECK(motor.facing().x > 0.9F && motor.facingName() == "right" && !motor.running());
    CHECK(rig.entity(rig.hero).worldPosition().x > 1.5F);
    // A diagonal is not faster than a straight line.
    rig.keyboard.set(Key::W, true);
    rig.step(60);
    CHECK_NEAR(motor.speed(), motor.walkSpeed, 0.05);
    rig.keyboard.set(Key::D, false);
    rig.keyboard.set(Key::W, false);
    rig.step(30);
    CHECK(motor.speed() < 0.01F); // Letting go stops it.
    // While the game's input is locked a person's character stands.
    rig.keyboard.set(Key::D, true);
    rig.runtime->lockInput("cutscene", true);
    rig.step(30);
    CHECK(motor.speed() < 0.01F);
    rig.runtime->lockInput("cutscene", false);
    rig.step(30);
    CHECK(motor.speed() > 1.0F);
}

void running() {
    Rig rig;
    rig.hero = rig.person("Hero", 10.0F).id(); // Ten stamina a second of running.
    rig.start();
    CharacterMotor &motor = rig.motor(rig.hero);
    StatSet &stats = rig.stats(rig.hero);
    CHECK_NEAR(stats.value("stamina"), 20.0, 0.001);
    rig.keyboard.set(Key::D, true);
    rig.keyboard.set(Key::LeftShift, true);
    rig.step(30); // Half a second: five stamina spent.
    CHECK(motor.running() && motor.speed() > motor.walkSpeed + 0.5F);
    CHECK_NEAR(stats.value("stamina"), 15.0, 0.4);
    rig.step(30); // Another half second.
    CHECK(motor.running() && stats.value("stamina") < 11.0);
    CHECK_NEAR(motor.speed(), motor.runSpeed, 0.1);
    // Running does not flood the game with stat.changed events (one a tick would).
    CHECK(rig.count("stat.changed") == 0);
    // It runs out: from here on the character walks, and says it is out of breath.
    rig.step(90);
    CHECK(motor.exhausted() && !motor.running());
    CHECK_NEAR(motor.speed(), motor.walkSpeed, 0.1);
    // Holding the key does not bring it back; stopping to recover does, after the regeneration
    // delay, once it is above the resume level.
    rig.step(30);
    CHECK(motor.exhausted() && !motor.running());
    rig.keyboard.set(Key::D, false);
    rig.step(60 *
             3); // Regeneration (5 a second) after a second's delay: about 10 in three seconds.
    CHECK(stats.value("stamina") > 8.0);
    rig.keyboard.set(Key::D, true);
    rig.step(60); // Past the resume level (10): running is possible again.
    CHECK(!motor.exhausted() && motor.running());

    // Running that costs nothing is free of stamina altogether.
    Rig free;
    free.hero = free.person("Hero", 0.0F).id();
    free.start();
    free.keyboard.set(Key::D, true);
    free.keyboard.set(Key::LeftShift, true);
    free.step(120);
    CHECK(free.motor(free.hero).running() && free.stats(free.hero).value("stamina") == 20.0);
    // A character with no such stat just runs (no StatSet to pay from).
    Rig bare;
    Entity &plain = bare.scene->createEntity("Plain");
    plain.add<PlayerInput>();
    plain.add<CharacterMotor>().runStaminaPerSecond = 10.0F;
    plain.add<PlayerCharacterController>();
    bare.hero = plain.id();
    bare.start();
    bare.keyboard.set(Key::D, true);
    bare.keyboard.set(Key::LeftShift, true);
    bare.step(60);
    CHECK(bare.motor(bare.hero).running() && !bare.motor(bare.hero).exhausted());

    // The toggle: a press switches it on, stopping ends it.
    Rig toggle;
    toggle.hero = toggle.person("Hero").id();
    toggle.scene->find(toggle.hero)->get<PlayerCharacterController>()->toggleRun = true;
    toggle.start();
    toggle.keyboard.set(Key::D, true);
    toggle.keyboard.set(Key::LeftShift, true);
    toggle.step(2);
    toggle.keyboard.set(Key::LeftShift, false);
    toggle.step(30);
    CHECK(toggle.motor(toggle.hero).running() &&
          toggle.entity(toggle.hero).get<PlayerCharacterController>()->wantsToRun());
    toggle.keyboard.set(Key::D, false);
    toggle.step(30);
    CHECK(!toggle.entity(toggle.hero).get<PlayerCharacterController>()->wantsToRun());
    toggle.keyboard.set(Key::D, true);
    toggle.step(30);
    CHECK(!toggle.motor(toggle.hero).running()); // It was switched off by stopping.
}

void effects() {
    Rig rig;
    rig.hero = rig.person("Hero", 10.0F).id();
    rig.guard = rig.scene->createEntity("Guard").id();
    // Something other than a person steers the guard: a script, an AI, a navigation agent.
    Entity &guard = *rig.scene->find(rig.guard);
    guard.add<StatusEffects>();
    guard.add<CharacterMotor>();
    rig.start();
    CharacterMotor &motor = rig.motor(rig.hero);
    StatusEffects &hero = rig.effects(rig.hero);
    rig.keyboard.set(Key::D, true);
    rig.step(60);
    const float walking = motor.speed();
    CHECK_NEAR(walking, motor.walkSpeed, 0.05);

    // A flag holds it where it stands, however hard it is told to go.
    CHECK(hero.apply(*rig.runtime, "stunned"));
    rig.step(10);
    CHECK(motor.suppressed() && motor.speed() < walking * 0.6F);
    rig.step(20);
    CHECK(motor.speed() < 0.01F);
    const float stoppedAt = rig.entity(rig.hero).worldPosition().x;
    rig.step(30);
    CHECK_NEAR(rig.entity(rig.hero).worldPosition().x, stoppedAt, 0.01);
    rig.step(30); // The effect lasts a second: after it, the character walks again.
    CHECK(!hero.has("stunned") && !motor.suppressed() && motor.speed() > 1.0F);

    // A factor slows it (and removing the effect gives the speed back).
    CHECK(hero.apply(*rig.runtime, "slowed"));
    rig.step(60);
    CHECK_NEAR(motor.speed(), motor.walkSpeed * 0.5F, 0.05);
    CHECK(hero.remove(*rig.runtime, "slowed"));
    rig.step(60);
    CHECK_NEAR(motor.speed(), motor.walkSpeed, 0.05);
    CHECK(hero.apply(*rig.runtime, "frozen")); // A factor of zero is the same as standing still.
    rig.step(60);
    CHECK(motor.speed() < 0.01F);
    CHECK(hero.remove(*rig.runtime, "frozen"));

    // No sprinting: the run key does nothing and costs nothing.
    CHECK(hero.apply(*rig.runtime, "winded"));
    rig.keyboard.set(Key::LeftShift, true);
    rig.step(60);
    CHECK(!motor.running() && motor.speed() < motor.walkSpeed + 0.05F);
    CHECK_NEAR(rig.stats(rig.hero).value("stamina"), 20.0, 0.001);
    CHECK(hero.remove(*rig.runtime, "winded"));
    rig.step(30);
    CHECK(motor.running());

    // The same rules bind a character something else steers.
    CharacterMotor &other = rig.motor(rig.guard);
    for (int i = 0; i < 60; ++i) {
        other.setIntent({1.0F, 0.0F}, 1.0F, true);
        rig.step();
    }
    CHECK(other.running() && other.speed() > other.walkSpeed + 0.5F);
    CHECK(rig.effects(rig.guard).apply(*rig.runtime, "stunned"));
    for (int i = 0; i < 40; ++i) {
        other.setIntent({1.0F, 0.0F}, 1.0F, true);
        rig.step();
    }
    CHECK(other.suppressed() && other.speed() < 0.01F && !other.running());
    CHECK(rig.effects(rig.guard).apply(*rig.runtime, "winded"));
    rig.step(70); // Stun over.
    for (int i = 0; i < 60; ++i) {
        other.setIntent({1.0F, 0.0F}, 1.0F, true);
        rig.step();
    }
    CHECK(!other.running() && other.speed() > 1.0F);
}

void controller() {
    Rig rig;
    rig.hero = rig.person("Hero").id();
    // The run action named after something the input map does not have is reported.
    rig.scene->find(rig.hero)->get<PlayerCharacterController>()->runAction = "Sprint";
    rig.start();
    // A character that cannot run (no action) walks even with the key down.
    rig.entity(rig.hero).get<PlayerCharacterController>()->runAction.clear();
    rig.keyboard.set(Key::D, true);
    rig.keyboard.set(Key::LeftShift, true);
    rig.step(60);
    CHECK(!rig.motor(rig.hero).running() &&
          !rig.entity(rig.hero).get<PlayerCharacterController>()->wantsToRun());
    // The motor and the controller declare their fields for the Inspector, and what they say about
    // themselves.
    const ComponentType *type = rig.registry.find("PlayerCharacterController");
    CHECK(type && type->find("runAction") && type->find("toggleRun"));
    const ComponentType *motorType = rig.registry.find("CharacterMotor");
    CHECK(motorType && motorType->find("runStaminaPerSecond") && motorType->check);
    if (motorType && motorType->check) {
        std::vector<std::string> problems;
        CheckContext context;
        context.known = [](std::string_view kind, std::string_view id) {
            return !(kind == "stat" && id == "stamina");
        };
        auto &motor = *rig.entity(rig.hero).get<CharacterMotor>();
        motor.runStaminaPerSecond = 5.0F;
        motorType->check(rig.entity(rig.hero), motor, context, problems);
        CHECK(problems.size() == 1 && has(problems[0], "'stamina', which is not defined"));
        problems.clear();
        motor.runStaminaPerSecond = 0.0F;
        motorType->check(rig.entity(rig.hero), motor, context, problems);
        CHECK(problems.empty());
    }
}

// ---- Appearance layers
// ---------------------------------------------------------------------------
struct LookRig : Rig {
    explicit LookRig(
        const char *startItems =
            R"([{"item":"inmate_outfit","equipped":true},{"item":"guard_outfit"},{"item":"hood"},{"item":"cap"}])") {
        Entity &entity = scene->createEntity("Hero");
        entity.setWorldPosition({3.0F, 4.0F});
        auto &body = entity.add<SpriteRenderer>();
        body.size = {1.0F, 2.0F};
        body.columns = 4;
        body.rows = 2;
        body.layer = 3;
        body.order = 1.5F;
        auto &inventory = entity.add<Inventory>();
        inventory.equipmentSlots = {"Outfit", "Hat"};
        inventory.startItems = J(startItems);
        auto &look = entity.add<AppearanceLayers>();
        look.layers = {"outfit", "hair", "hat"};
        look.defaults = J(R"({"outfit":"assets/char/naked.png","hair":"assets/char/hair.png"})");
        hero = entity.id();
    }
    AppearanceLayers &look() {
        return *entity(hero).get<AppearanceLayers>();
    }
    SpriteRenderer &layer(const char *name) {
        return *entity(look().layerEntity(name)).get<SpriteRenderer>();
    }
    Inventory &inventory() {
        return *entity(hero).get<Inventory>();
    }
};

void appearance() {
    LookRig rig;
    rig.start();
    AppearanceLayers &look = rig.look();
    // One child entity per layer, under the character, named after it.
    CHECK(rig.entity(look.layerEntity("outfit")).parentId() == rig.hero &&
          rig.entity(look.layerEntity("hair")).name() == "Hero.hair" &&
          !look.layerEntity("nothing"));
    // The inmate outfit is worn: it shows over the default; the hair is the default; no hat.
    CHECK(look.textureOf("outfit") == "assets/char/inmate.png" &&
          look.textureOf("hair") == "assets/char/hair.png" && look.textureOf("hat").empty());
    CHECK(rig.layer("outfit").texture.path == "assets/char/inmate.png" &&
          rig.layer("hair").visible && !rig.layer("hat").visible);
    // Every layer is drawn as the body is: same size, layer, cell, flip; each a little in front.
    CHECK(rig.layer("outfit").size == (Vec2{1.0F, 2.0F}) && rig.layer("outfit").layer == 3 &&
          rig.layer("outfit").columns == 4 && rig.layer("outfit").rows == 2);
    CHECK_NEAR(rig.layer("outfit").order, 1.501, 0.0001);
    CHECK_NEAR(rig.layer("hair").order, 1.502, 0.0001);
    CHECK_NEAR(rig.layer("hat").order, 1.503, 0.0001);
    SpriteRenderer &body = *rig.entity(rig.hero).get<SpriteRenderer>();
    body.frame = 5;
    body.flipX = true;
    body.color = Color{200, 100, 50, 255};
    rig.step();
    for (const char *name : {"outfit", "hair", "hat"})
        CHECK(rig.layer(name).frame == 5 && rig.layer(name).flipX &&
              rig.layer(name).color == body.color);
    body.visible = false;
    rig.step();
    CHECK(!rig.layer("outfit").visible && !rig.layer("hair").visible);
    body.visible = true;
    // They go where the character goes.
    rig.entity(rig.hero).setWorldPosition({10.0F, 2.0F});
    CHECK(rig.entity(look.layerEntity("outfit")).worldPosition() == (Vec2{10.0F, 2.0F}));

    // Changing clothes changes the layer, and taking them off brings the default back.
    Inventory &inventory = rig.inventory();
    const int guardSlot = inventory.find(ItemId("guard_outfit")).slot;
    CHECK(guardSlot >= 0 && inventory.equip(*rig.runtime, guardSlot));
    rig.step();
    CHECK(look.textureOf("outfit") == "assets/char/guard.png" &&
          rig.layer("outfit").texture.path == "assets/char/guard.png");
    CHECK(inventory.unequip(*rig.runtime, "Outfit"));
    rig.step();
    CHECK(look.textureOf("outfit") == "assets/char/naked.png");
    // A hood shows over the head and hides the hair; a cap only adds itself.
    const int hoodSlot = inventory.find(ItemId("hood")).slot;
    CHECK(inventory.equip(*rig.runtime, hoodSlot));
    rig.step();
    CHECK(look.textureOf("hat") == "assets/char/hood.png" && look.textureOf("hair").empty() &&
          rig.layer("hat").visible && !rig.layer("hair").visible);
    CHECK(inventory.unequip(*rig.runtime, "Hat"));
    const int capSlot = inventory.find(ItemId("cap")).slot;
    CHECK(inventory.equip(*rig.runtime, capSlot));
    rig.step();
    CHECK(look.textureOf("hat") == "assets/char/cap.png" &&
          look.textureOf("hair") == "assets/char/hair.png" && rig.layer("hair").visible);

    // A character with nothing worn and no defaults shows nothing.
    LookRig bare(R"([])");
    bare.scene->find(bare.hero)->get<AppearanceLayers>()->defaults = Json::object();
    bare.start();
    CHECK(!bare.layer("outfit").visible && !bare.layer("hair").visible &&
          !bare.layer("hat").visible);
}

void appearanceChecks() {
    LookRig rig(R"([])");
    rig.start();
    const ComponentType *type = rig.registry.find("AppearanceLayers");
    CHECK(type && type->check);
    if (!type || !type->check)
        return;
    const auto problems = [&](std::vector<std::string> layers, const char *defaults) {
        AppearanceLayers look;
        look.layers = std::move(layers);
        look.defaults = J(defaults);
        std::vector<std::string> found;
        type->check(rig.entity(rig.hero), look, CheckContext{}, found);
        return found;
    };
    CHECK(problems({"outfit", "hair"}, R"({"outfit":"a.png"})").empty());
    auto twice = problems({"outfit", "outfit"}, "{}");
    CHECK(twice.size() == 1 && has(twice[0], "names the layer 'outfit' twice"));
    auto nameless = problems({"outfit", ""}, "{}");
    CHECK(nameless.size() == 1 && has(nameless[0], "layer without a name"));
    auto stray = problems({"outfit"}, R"({"hat":"a.png","outfit":3})");
    CHECK(stray.size() == 2 && has(stray[0], "default for 'hat', which is not one of its layers") &&
          has(stray[1], "default for 'outfit' must be a texture path"));
    auto wrong = problems({"outfit"}, R"([1,2])");
    CHECK(wrong.size() == 1 && has(wrong[0], "must be an object"));
}
} // namespace

int main() {
    setLogStderrEnabled(false);
    walking();
    running();
    effects();
    controller();
    appearance();
    appearanceChecks();
    return yk::test::finish("character");
}
