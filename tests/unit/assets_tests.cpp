// Project settings and texture import metadata: parsing, validation and round trips.
#include "support/check.hpp"
#include "yk/assets/Project.hpp"
#include "yk/assets/TextureMeta.hpp"
#include "yk/core/FileIO.hpp"
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
} // namespace

int main() {
    textureMeta();
    projectFile();
    return yk::test::finish("assets");
}
