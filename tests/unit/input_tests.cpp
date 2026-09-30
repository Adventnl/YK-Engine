// The action system: bindings, input maps (validation and JSON) and per-tick evaluation. Nothing
// here needs a window; gamepads are simulated by setting the state a platform layer would provide.
#include "support/check.hpp"
#include "yk/input/ActionInput.hpp"
#include "yk/input/InputMap.hpp"
#include <limits>

using namespace yk;

namespace {
InputMap oneAction() {
    InputMap map;
    ActionSet set;
    set.name = "P";
    set.actions = {{"Fire", {InputBinding::fromKey(Key::A), InputBinding::fromKey(Key::Left)}}};
    map.sets.push_back(std::move(set));
    return map;
}

void bindingText() {
    CHECK(formatBinding(InputBinding::fromKey(Key::A)) == "Key:A");
    CHECK(formatBinding(InputBinding::fromButton(GamepadButton::South)) == "Pad:South");
    CHECK(formatBinding(InputBinding::fromAxis(GamepadAxis::LeftX, false)) == "PadAxis:LeftX-");
    for (const std::string text :
         {"Key:Space", "Key:F5", "Pad:DPadLeft", "PadAxis:RightTrigger+"}) {
        const auto parsed = parseBinding(text);
        CHECK(parsed);
        CHECK(parsed && formatBinding(parsed.value()) == text);
    }
    CHECK(!parseBinding("A"));                 // No device prefix.
    CHECK(!parseBinding("Key:Nonsense"));      // Unknown key.
    CHECK(!parseBinding("Pad:Nope"));          // Unknown button.
    CHECK(!parseBinding("PadAxis:LeftX"));     // A sign is required.
    CHECK(!parseBinding("PadAxis:Sideways+")); // Unknown axis.
    CHECK(!parseBinding("Mouse:Left"));        // Unknown device.
    CHECK(!InputBinding{}.valid());
}

void mapValidationAndJson() {
    const InputMap standard = InputMap::standard();
    CHECK(standard.validate());
    CHECK(standard.findSet("Player1") && standard.findSet("Player2") && standard.findSet("Global"));
    CHECK(!standard.findSet("Player3"));
    CHECK(standard.findSet("Player1")->find("Jump") != nullptr);
    CHECK(standard.findSet("Player1")->find("Fly") == nullptr);
    const auto actions = standard.actionNames();
    CHECK(actions.size() == 6); // MoveLeft, MoveRight, Jump, Interact, Restart, Pause.
    CHECK(standard.setNames().size() == 3);

    // Round trip through text, byte for byte.
    const std::string text = standard.toJson().dump(2);
    const auto reparsed = Json::parse(text);
    CHECK(reparsed);
    const auto loaded = InputMap::fromJson(reparsed.value());
    CHECK(loaded);
    CHECK(loaded && loaded.value().toJson().dump(2) == text);

    InputMap bad = standard;
    bad.sets[1].name = "Player1";
    CHECK(!bad.validate()); // Duplicate set.
    bad = standard;
    bad.sets[0].name.clear();
    CHECK(!bad.validate());
    bad = standard;
    bad.sets[0].actions.push_back({"Jump", {}});
    CHECK(!bad.validate()); // Duplicate action in a set.
    bad = standard;
    bad.sets[0].actions[0].bindings.push_back(InputBinding{});
    CHECK(!bad.validate()); // Incomplete binding.
    bad = standard;
    bad.sets[0].gamepad = 9;
    CHECK(!bad.validate());
    CHECK(!InputMap::fromJson(
        Json::parse(R"({"sets":[{"name":"P","actions":[{"name":"X","bindings":["Key:Zzz"]}]}]})")
            .value()));
    CHECK(!InputMap::fromJson(Json::parse(R"({"sets":"nope"})").value()));
    CHECK(!InputMap::fromJson(Json::parse(R"({"sets":[{"actions":[]}]})").value()));
    const auto empty = InputMap::fromJson(Json::parse(R"({"sets":[]})").value());
    CHECK(empty && empty.value().sets.empty());
}

// Feeds one tick of input. `setup` changes the frame before it is evaluated.
template <class F> void tick(ActionInput &input, InputFrame &frame, F &&setup) {
    frame.beginFrame();
    setup(frame);
    input.update(frame);
}

void keyboardActions() {
    ActionInput input(oneAction());
    InputFrame frame;
    CHECK(input.known("P", "Fire") && !input.known("P", "Nope") && !input.known("Q", "Fire"));

    tick(input, frame, [](InputFrame &f) { f.keyboard.set(Key::A, true); });
    CHECK(input.state("P", "Fire").held && input.state("P", "Fire").pressed);
    CHECK_NEAR(input.value("P", "Fire"), 1.0);
    tick(input, frame, [](InputFrame &) {}); // Held across ticks, but the edge is gone.
    CHECK(input.state("P", "Fire").held && !input.state("P", "Fire").pressed);

    // Switching from one alias to the other is neither a release nor a fresh press.
    tick(input, frame, [](InputFrame &f) {
        f.keyboard.set(Key::Left, true);
        f.keyboard.set(Key::A, false);
    });
    CHECK(input.state("P", "Fire").held);
    CHECK(!input.state("P", "Fire").pressed && !input.state("P", "Fire").released);

    tick(input, frame, [](InputFrame &f) { f.keyboard.set(Key::Left, false); });
    CHECK(!input.state("P", "Fire").held && input.state("P", "Fire").released);
    tick(input, frame, [](InputFrame &) {});
    CHECK(!input.state("P", "Fire").pressed && !input.state("P", "Fire").released);
    CHECK_NEAR(input.value("P", "Fire"), 0.0);

    // A tap that starts and ends inside one poll still registers as press and release.
    tick(input, frame, [](InputFrame &f) {
        f.keyboard.set(Key::A, true);
        f.keyboard.set(Key::A, false);
    });
    CHECK(input.state("P", "Fire").pressed && input.state("P", "Fire").released);
    CHECK(!input.state("P", "Fire").held);

    // Unknown names read as idle instead of failing.
    CHECK(!input.state("P", "Nope").held && !input.state("Q", "Fire").pressed);
    CHECK_NEAR(input.value("Q", "Fire"), 0.0);

    // reset() releases everything, e.g. when a scene restarts.
    tick(input, frame, [](InputFrame &f) { f.keyboard.set(Key::A, true); });
    input.reset();
    CHECK(!input.state("P", "Fire").held);
}

void twoPlayersShareTheKeyboard() {
    ActionInput input(InputMap::standard());
    InputFrame frame;
    tick(input, frame, [](InputFrame &f) {
        f.keyboard.set(Key::D, true);    // Player1 right.
        f.keyboard.set(Key::Left, true); // Player2 left.
        f.keyboard.set(Key::Up, true);   // Player2 jump.
    });
    CHECK_NEAR(input.axis("Player1", "MoveLeft", "MoveRight"), 1.0);
    CHECK_NEAR(input.axis("Player2", "MoveLeft", "MoveRight"), -1.0);
    CHECK(!input.state("Player1", "Jump").held && input.state("Player2", "Jump").pressed);
    CHECK(!input.state("Global", "Restart").held);
    tick(input, frame, [](InputFrame &f) { f.keyboard.set(Key::R, true); });
    CHECK(input.state("Global", "Restart").pressed);
    // Opposite directions cancel.
    tick(input, frame, [](InputFrame &f) { f.keyboard.set(Key::A, true); });
    CHECK_NEAR(input.axis("Player1", "MoveLeft", "MoveRight"), 0.0);
}

void gamepads() {
    ActionInput input(InputMap::standard()); // Player1 listens to pad 0, Player2 to pad 1.
    InputFrame frame;

    // A pad that is not connected contributes nothing, even if buttons are set.
    tick(input, frame, [](InputFrame &f) { f.gamepads[0].setButton(GamepadButton::South, true); });
    CHECK(!input.state("Player1", "Jump").held);

    tick(input, frame, [](InputFrame &f) {
        f.gamepads[0].setConnected(true);
        f.gamepads[0].setButton(GamepadButton::South, true);
    });
    CHECK(input.state("Player1", "Jump").pressed && input.state("Player1", "Jump").held);
    CHECK(!input.state("Player2", "Jump").held); // Pad 1 is a different seat.

    // Stick: below the deadzone nothing; then analog value; then held as a button.
    tick(input, frame, [](InputFrame &f) { f.gamepads[0].setAxis(GamepadAxis::LeftX, 0.2F); });
    CHECK_NEAR(input.value("Player1", "MoveRight"), 0.0);
    CHECK(!input.state("Player1", "MoveRight").held);
    tick(input, frame, [](InputFrame &f) { f.gamepads[0].setAxis(GamepadAxis::LeftX, 0.55F); });
    CHECK_NEAR(input.value("Player1", "MoveRight"), (0.55 - 0.25) / 0.75, 1e-4);
    CHECK(!input.state("Player1", "MoveRight").held); // Still under the press threshold.
    tick(input, frame, [](InputFrame &f) { f.gamepads[0].setAxis(GamepadAxis::LeftX, 1.0F); });
    CHECK_NEAR(input.value("Player1", "MoveRight"), 1.0);
    CHECK(input.state("Player1", "MoveRight").held && input.state("Player1", "MoveRight").pressed);
    CHECK_NEAR(input.axis("Player1", "MoveLeft", "MoveRight"), 1.0);
    tick(input, frame, [](InputFrame &f) { f.gamepads[0].setAxis(GamepadAxis::LeftX, -1.0F); });
    CHECK_NEAR(input.axis("Player1", "MoveLeft", "MoveRight"), -1.0);
    CHECK(input.state("Player1", "MoveRight").released);

    // A key and a stick on one action: the stronger one wins, and the key alone still works.
    tick(input, frame, [](InputFrame &f) {
        f.gamepads[0].setAxis(GamepadAxis::LeftX, 0.0F);
        f.keyboard.set(Key::D, true);
    });
    CHECK_NEAR(input.value("Player1", "MoveRight"), 1.0);

    // Unplugging releases everything that pad held.
    tick(input, frame, [](InputFrame &f) { f.gamepads[0].setConnected(false); });
    CHECK(input.state("Player1", "Jump").released && !input.state("Player1", "Jump").held);
    CHECK(!frame.gamepads[0].button(GamepadButton::South).held);

    // Non-finite axis values are treated as zero.
    Gamepad pad;
    pad.setConnected(true);
    pad.setAxis(GamepadAxis::LeftX, std::numeric_limits<float>::quiet_NaN());
    CHECK_NEAR(pad.axis(GamepadAxis::LeftX), 0.0);
    pad.setAxis(GamepadAxis::LeftX, 7.0F);
    CHECK_NEAR(pad.axis(GamepadAxis::LeftX), 1.0);
}

void names() {
    CHECK(gamepadButtonFromName("South") == GamepadButton::South);
    CHECK(gamepadButtonFromName("nope") == GamepadButton::Count);
    CHECK(gamepadAxisFromName("RightTrigger") == GamepadAxis::RightTrigger);
    CHECK(gamepadAxisName(GamepadAxis::Count).empty());
    CHECK(gamepadButtonName(GamepadButton::Count).empty());
}
} // namespace

int main() {
    bindingText();
    mapValidationAndJson();
    keyboardActions();
    twoPlayersShareTheKeyboard();
    gamepads();
    names();
    return yk::test::finish("input");
}
