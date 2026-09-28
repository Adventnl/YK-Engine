#include "yk/core/GameFoundation.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace yk;
using namespace yk::game;
namespace {
int failures;
void check(bool value, const char *name) {
    if (!value) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}
std::filesystem::path fixture(const std::string &body) {
    auto p = std::filesystem::temp_directory_path() / "yk-level-test.ykl";
    std::ofstream(p) << body;
    return p;
}
} // namespace
int main() {
    auto valid =
        loadLevel(fixture("level a 4 4 16\ncollision\n....\n....\n....\n####\nspawn start 8 40\n"));
    check(static_cast<bool>(valid), "valid level loads");
    if (valid) {
        check(valid.value().tile(1, 3) == Tile::Solid, "collision layer loads");
        check(valid.value().tileToWorld(2, 1).x == 32, "tile conversion");
    }
    auto bad =
        loadLevel(fixture("level a 4 4 16\ncollision\n....\n..X.\n....\n####\nspawn start 8 40\n"));
    check(!bad, "unsupported collision rejected");
    InputBuffer buffer;
    buffer.push({1, true});
    auto first = buffer.consumeTick(), second = buffer.consumeTick();
    check(first.jump && !second.jump && second.move == 1,
          "input edge consumed once while held input remains");
    buffer.clear();
    check(buffer.consumeTick().move == 0, "focus-style clear releases input");
    if (valid) {
        PlayerState p;
        CharacterController controller;
        controller.reset(p, {24, 48});
        for (int i = 0; i < 120; ++i)
            controller.tick(p, valid.value(), {}, 1.F / 60.F);
        check(p.grounded && std::abs(p.position.y - 48) < .1F, "flat ground stable");
        controller.tick(p, valid.value(), {0, true}, 1.F / 60.F);
        check(p.velocity.y < 0 && !p.grounded, "jump consumes support");
        controller.reset(p, {24, 48});
        controller.tick(p, valid.value(), {0, false, false, true}, 1.F / 60.F);
        check(p.drop > 0, "drop-through timer is explicit");
    }
    Animator a;
    check(static_cast<bool>(a.define({"once", 2, 3, .1F, false})), "animation definition");
    check(static_cast<bool>(a.play("once")), "animation starts");
    a.tick(.35F);
    check(a.frame() == 4 && a.takeCompletion() && !a.takeCompletion(),
          "non-loop animation completes once");
    auto save = std::filesystem::temp_directory_path() / "yk-save-test" / "progress.yks";
    Progress source;
    source.level = "a";
    source.spawn = "start";
    source.keys = 1;
    source.collected.insert("key_1");
    source.unlocked.insert("door_1");
    check(static_cast<bool>(saveProgress(save, source)), "atomic save succeeds");
    auto loaded = loadProgress(save);
    check(loaded && loaded.value().keys == 1 && loaded.value().collected.contains("key_1") &&
              loaded.value().unlocked.contains("door_1"),
          "save round trip");
    std::ofstream(save) << "version 999\n";
    check(!loadProgress(save), "future save version rejected");
    std::filesystem::remove_all(save.parent_path());
    std::filesystem::remove(std::filesystem::temp_directory_path() / "yk-level-test.ykl");
    return failures == 0 ? 0 : 1;
}
