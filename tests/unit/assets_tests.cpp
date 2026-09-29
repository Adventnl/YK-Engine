// Project settings and texture import metadata: parsing, validation and round trips.
#include "support/check.hpp"
#include "yk/assets/Project.hpp"
#include "yk/assets/TextureMeta.hpp"
#include "yk/assets/Validation.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <algorithm>
#include <filesystem>

using namespace yk;

namespace {
Result<TextureMeta> meta(const char *text) {
    auto json = Json::parse(text);
    return json ? TextureMeta::fromJson(json.value()) : Result<TextureMeta>(Error{json.error()});
}

void textureMeta() {
    CHECK(TextureMeta::sidecarPath("assets/a.png") == "assets/a.png.ykmeta");

    const auto empty = meta(R"({"format":"yk.texture","version":1})");
    CHECK(empty && empty.value().empty());

    const auto full = meta(R"({"format":"yk.texture","version":1,"pixelsPerUnit":32,
        "filter":"nearest","columns":4,"rows":2,"border":[1,2,3,4]})");
    CHECK(full);
    if (full) {
        const TextureMeta &m = full.value();
        CHECK(m.pixelsPerUnit && *m.pixelsPerUnit == 32.0F);
        CHECK(m.filter && *m.filter == TextureFilter::Nearest);
        CHECK(m.columns && *m.columns == 4 && m.rows && *m.rows == 2);
        CHECK(m.border && (*m.border)[0] == 1 && (*m.border)[3] == 4);
        // Round trip keeps everything and is stable text.
        const auto again = TextureMeta::fromJson(m.toJson());
        CHECK(again && again.value().toJson().dump() == m.toJson().dump());
    }

    CHECK(!meta(R"({"format":"other"})"));
    CHECK(!meta(R"({"format":"yk.texture","pixelsPerUnit":0})"));
    CHECK(!meta(R"({"format":"yk.texture","pixelsPerUnit":100000})"));
    CHECK(!meta(R"({"format":"yk.texture","pixelsPerUnit":"big"})"));
    CHECK(!meta(R"({"format":"yk.texture","filter":"trilinear"})"));
    CHECK(!meta(R"({"format":"yk.texture","columns":0})"));
    CHECK(!meta(R"({"format":"yk.texture","columns":2.5})"));
    CHECK(!meta(R"({"format":"yk.texture","rows":1000})"));
    CHECK(!meta(R"({"format":"yk.texture","border":[1,2,3]})"));
    CHECK(!meta(R"({"format":"yk.texture","border":[1,2,3,-4]})"));
    CHECK(!meta(R"({"format":"yk.texture","border":"wide"})"));

    // Defaults merge with metadata: metadata wins, absent fields fall back.
    TextureDefaults defaults;
    defaults.pixelsPerUnit = 48.0F;
    defaults.filter = TextureFilter::Linear;
    const ResolvedTexture plain = resolve(defaults, {});
    CHECK(plain.pixelsPerUnit == 48.0F && plain.filter == TextureFilter::Linear);
    CHECK(plain.columns == 1 && plain.rows == 1 && !plain.hasBorder());
    const ResolvedTexture merged = resolve(defaults, full.value());
    CHECK(merged.pixelsPerUnit == 32.0F && merged.filter == TextureFilter::Nearest);
    CHECK(merged.columns == 4 && merged.rows == 2 && merged.hasBorder());

    const auto parsed = TextureDefaults::fromJson(Json::parse(R"({"pixelsPerUnit":16,"filter":"nearest"})").value());
    CHECK(parsed && parsed.value().pixelsPerUnit == 16.0F &&
          parsed.value().filter == TextureFilter::Nearest);
    CHECK(!TextureDefaults::fromJson(Json::parse(R"({"filter":"x"})").value()));
    CHECK(!TextureDefaults::fromJson(Json::parse("[]").value()));
}

void projectFile() {
    const std::filesystem::path root = std::filesystem::current_path() / "assets-test-project";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    Project project = Project::create(root, "Assets Test");
    project.textures.pixelsPerUnit = 100.0F;
    project.textures.filter = TextureFilter::Nearest;
    project.input.sets.clear();
    ActionSet set;
    set.name = "Solo";
    set.gamepad = 2;
    set.actions = {{"Go", {InputBinding::fromKey(Key::Space), InputBinding::fromButton(GamepadButton::East)}}};
    project.input.sets.push_back(set);
    CHECK(project.save());

    const auto loaded = Project::load(root);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value().textures.pixelsPerUnit == 100.0F);
        CHECK(loaded.value().textures.filter == TextureFilter::Nearest);
        CHECK(loaded.value().input.sets.size() == 1);
        CHECK(loaded.value().input.sets[0].name == "Solo" && loaded.value().input.sets[0].gamepad == 2);
        CHECK(loaded.value().input.sets[0].actions[0].bindings.size() == 2);
    }

    // A project written before these sections existed loads with the standard input map and
    // default texture settings.
    CHECK(writeTextFileAtomic(root / "project.ykproj",
                              R"({"format":"yk.project","version":1,"name":"Old"})"));
    const auto old = Project::load(root);
    CHECK(old);
    if (old) {
        CHECK(old.value().input.findSet("Player1") != nullptr);
        CHECK(old.value().textures.pixelsPerUnit == 64.0F);
    }

    // Broken sections are reported with the file name, not silently defaulted.
    CHECK(writeTextFileAtomic(root / "project.ykproj",
                              R"({"format":"yk.project","version":1,"input":{"sets":[{"name":"P","actions":[{"name":"A","bindings":["Key:Nope"]}]}]}})"));
    const auto badInput = Project::load(root);
    CHECK(!badInput && badInput.error().find("project.ykproj") != std::string::npos);
    CHECK(writeTextFileAtomic(root / "project.ykproj",
                              R"({"format":"yk.project","version":1,"textures":{"filter":"blurry"}})"));
    CHECK(!Project::load(root));
    std::filesystem::remove_all(root);
}
// A component with an input-action field, so validation of action names can be tested here.
struct Binding final : Component {
    std::string action;
    static void describe(TypeBuilder<Binding> &type) {
        type.field("action", &Binding::action).inputAction();
    }
};

bool mentions(const std::vector<ProjectIssue> &issues, ProjectIssue::Severity severity,
              const std::string &needle) {
    return std::any_of(issues.begin(), issues.end(), [&](const ProjectIssue &issue) {
        return issue.severity == severity && (issue.message.find(needle) != std::string::npos ||
                                              issue.path.find(needle) != std::string::npos);
    });
}

void validation() {
    using Severity = ProjectIssue::Severity;
    const std::filesystem::path root = std::filesystem::current_path() / "validation-test-project";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "assets");
    std::filesystem::create_directories(root / "scenes");
    Project project = Project::create(root, "Validation");
    project.startScene = "scenes/main.ykscene";

    const auto write = [&](const char *path, const char *text) {
        CHECK(writeTextFileAtomic(root / path, text));
    };
    write("assets/good.ykanim",
          R"({"format":"yk.animation","version":2,"texture":"assets/sheet.bmp","columns":2,"rows":1,
              "clips":[{"name":"idle","first":0,"count":1}]})");
    write("assets/sheet.bmp", "not really an image, only its existence matters here");
    write("assets/broken.ykanim", R"({"format":"yk.animation","version":2,"clips":[]})");
    write("assets/missing-sheet.ykanim",
          R"({"format":"yk.animation","version":2,"texture":"assets/gone.png",
              "clips":[{"name":"idle"}]})");
    write("assets/misfit.ykctl",
          R"({"format":"yk.animator","version":1,"states":[{"name":"S","clip":"nothing"}]})");
    write("assets/fine.ykctl",
          R"({"format":"yk.animator","version":1,"states":[{"name":"S","clip":"idle"}]})");
    write("assets/bad.ykctl", R"({"format":"yk.animator","version":1,"states":[]})");
    write("assets/sheet.bmp.ykmeta", R"({"format":"yk.texture","version":1,"filter":"blurry"})");
    write("assets/orphan.png.ykmeta", R"({"format":"yk.texture","version":1})");

    ComponentRegistry registry;
    registerEngineComponents(registry);
    registry.add<Binding>("Binding");
    Scene scene(registry, 9);
    Entity &mismatched = scene.createEntity("Mismatched");
    mismatched.add<SpriteRenderer>().texture.path = "assets/other.bmp";
    auto &animated = mismatched.add<AnimatedSprite>();
    animated.animation.path = "assets/good.ykanim";
    animated.controller.path = "assets/misfit.ykctl";
    Entity &player = scene.createEntity("Ghost");
    player.add<PlayerInput>().actionSet = "Ghosts";
    player.add<Binding>().action = "Teleport";
    Entity &fine = scene.createEntity("Fine");
    fine.add<PlayerInput>().actionSet = "Player1";
    fine.add<Binding>().action = "Jump";
    auto &fineAnimated = fine.add<AnimatedSprite>();
    fineAnimated.animation.path = "assets/good.ykanim";
    fineAnimated.controller.path = "assets/fine.ykctl";
    fine.get<SpriteRenderer>()->texture.path = "assets/sheet.bmp";
    CHECK(saveScene(scene, root / "scenes/main.ykscene"));
    setLogStderrEnabled(false);
    const auto issues = validateProject(project, registry);
    setLogStderrEnabled(true);

    CHECK(hasErrors(issues));
    CHECK(mentions(issues, Severity::Error, "assets/broken.ykanim"));
    CHECK(mentions(issues, Severity::Error, "missing sheet texture 'assets/gone.png'"));
    CHECK(mentions(issues, Severity::Error, "assets/bad.ykctl"));
    CHECK(mentions(issues, Severity::Error, "does not fit the animation"));
    CHECK(mentions(issues, Severity::Error, "assets/sheet.bmp.ykmeta")); // "blurry" filter.
    CHECK(mentions(issues, Severity::Warning, "assets/orphan.png"));     // Meta without a texture.
    CHECK(mentions(issues, Severity::Warning, "the animation's sheet wins"));
    CHECK(mentions(issues, Severity::Warning, "input set 'Ghosts'"));
    CHECK(mentions(issues, Severity::Warning, "input action 'Teleport'"));
    // Nothing is said about the entity that is fine, or about known input names.
    CHECK(!mentions(issues, Severity::Warning, "input action 'Jump'"));
    CHECK(!mentions(issues, Severity::Warning, "input set 'Player1'"));
    CHECK(!mentions(issues, Severity::Error, "assets/good.ykanim"));
    CHECK(!mentions(issues, Severity::Error, "assets/fine.ykctl"));

    // A broken input map is an error in the project file.
    project.input.sets.push_back(project.input.sets.front());
    CHECK(mentions(validateProject(project, registry), Severity::Error, "defined twice"));
    std::filesystem::remove_all(root);
}
} // namespace

int main() {
    textureMeta();
    projectFile();
    validation();
    return yk::test::finish("assets");
}
