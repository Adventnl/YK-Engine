// Packaging a project as a game: build targets and names, which files ship, the ZIP writer, and
// exportGame() for every target against a stand-in player program.
#include "support/check.hpp"
#include "yk/assets/Export.hpp"
#include "yk/assets/Zip.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <algorithm>
#include <filesystem>

using namespace yk;
namespace fs = std::filesystem;

namespace {
struct TempDir {
    fs::path path;
    explicit TempDir(const char *name) : path(fs::temp_directory_path() / name) {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

void put(const fs::path &file, const std::string &text) {
    CHECK(writeTextFileAtomic(file, text));
}
bool has(const std::vector<std::string> &list, const std::string &item) {
    return std::find(list.begin(), list.end(), item) != list.end();
}

// A tiny valid project: one scene with a camera and a sprite that uses assets/hero.png.
Project makeProject(const fs::path &root, const ComponentRegistry &registry) {
    Project project = Project::create(root, "Cinder Vale");
    project.startScene = "scenes/main.ykscene";
    Scene scene(registry, 7);
    scene.createEntity("Camera").add<Camera>();
    Entity &hero = scene.createEntity("Hero");
    hero.add<SpriteRenderer>().texture.path = "assets/hero.png";
    fs::create_directories(root / "scenes");
    CHECK(saveScene(scene, root / "scenes" / "main.ykscene"));
    put(root / "assets" / "hero.png", "png bytes");
    CHECK(project.save());
    return project;
}

void targetsAndNames() {
    CHECK(std::string(name(BuildTarget::Windows)) == "windows");
    CHECK(std::string(name(BuildTarget::MacOS)) == "macos");
    CHECK(std::string(name(BuildTarget::Linux)) == "linux");
    CHECK(std::string(displayName(BuildTarget::MacOS)) == "macOS");
    CHECK(buildTargetFromName("Windows") == BuildTarget::Windows);
    CHECK(buildTargetFromName("mac") == BuildTarget::MacOS);
    CHECK(buildTargetFromName("LINUX") == BuildTarget::Linux);
    CHECK(!buildTargetFromName("amiga"));
    CHECK(playerFileName(BuildTarget::Windows) == "yk_player.exe");
    CHECK(playerFileName(BuildTarget::Linux) == "yk_player");
    CHECK(!engineVersion().empty());

    Project project = Project::create(fs::temp_directory_path() / "yk-names", "Cinder Vale");
    CHECK(productName(project) == "Cinder Vale");
    CHECK(executableName(project) == "CinderVale");
    CHECK(bundleIdentifier(project) == "com.yk.cindervale");
    CHECK(exportFolderName(project, BuildTarget::Windows) == "Cinder-Vale-windows");
    CHECK(exportFolderName(project, BuildTarget::Linux) == "Cinder-Vale-linux");
    CHECK(exportFolderName(project, BuildTarget::MacOS) == "Cinder Vale.app");

    project.build.productName = "Ember & Tide: Origins";
    project.build.executable = "ember tide/x";
    project.build.identifier = "org.example.et";
    CHECK(productName(project) == "Ember & Tide: Origins");
    CHECK(executableName(project) == "embertide_x"); // Nothing that a file system dislikes.
    CHECK(bundleIdentifier(project) == "org.example.et");
    const std::string folder = exportFolderName(project, BuildTarget::Windows);
    CHECK(folder.find(':') == std::string::npos && folder.find('&') == std::string::npos &&
          folder.find(' ') == std::string::npos && folder.ends_with("-windows"));
    CHECK(exportFolderName(project, BuildTarget::MacOS) == "Ember & Tide_ Origins.app");

    // Nothing usable left: fall back to fixed names instead of an empty file name.
    Project odd = Project::create(fs::temp_directory_path() / "yk-names", "...");
    CHECK(executableName(odd) == "game");
    CHECK(exportFolderName(odd, BuildTarget::Linux) == "game-linux");
    Project blank = Project::create(fs::temp_directory_path() / "yk-names", "");
    CHECK(productName(blank) == "Game");
}

void buildSettingsRoundTrip() {
    BuildSettings settings;
    settings.productName = "Shiny";
    settings.executable = "shiny";
    settings.version = "2.1.0";
    settings.identifier = "org.example.shiny";
    settings.exclude = {"art/source", "notes.txt"};
    const auto again = BuildSettings::fromJson(settings.toJson());
    CHECK(again && again.value() == settings);
    CHECK(!BuildSettings::fromJson(Json("text")));
    CHECK(!BuildSettings::fromJson(Json(3)));
    CHECK(!BuildSettings::fromJson(Json::parse(R"({"exclude":[""]})").value()));
    CHECK(!BuildSettings::fromJson(Json::parse(R"({"executable":5})").value()));

    // A project that never touched the settings does not grow a "build" section.
    Project plain = Project::create(fs::temp_directory_path() / "yk-plain", "Plain");
    CHECK(!plain.toJson().contains("build"));
    plain.build = settings;
    CHECK(plain.toJson().contains("build"));
    TempDir dir("yk-export-settings");
    plain.root = dir.path;
    CHECK(plain.save());
    const auto loaded = Project::load(dir.path);
    CHECK(loaded && loaded.value().build == settings);
}

void whichFilesShip() {
    TempDir dir("yk-export-files");
    ComponentRegistry registry;
    registerStandardComponents(registry);
    Project project = makeProject(dir.path, registry);
    put(dir.path / "tools" / "gen.py", "print()");
    put(dir.path / "tools" / "data.json", "{}");
    put(dir.path / "docs" / "guide.txt", "docs");
    put(dir.path / "README.md", "readme");
    put(dir.path / "assets" / "notes.md", "notes");
    put(dir.path / "assets" / "hero.png.bak", "old");
    put(dir.path / "assets" / "scratch.tmp", "scratch");
    put(dir.path / "assets" / "backup~", "old");
    put(dir.path / ".hidden" / "x", "x");
    put(dir.path / "build" / "out.bin", "x");
    put(dir.path / "art" / "source" / "hero.psd", "x");
    put(dir.path / "art" / "keep" / "a.png", "x");
    put(dir.path / "scripts.py", "top level py");
    put(dir.path / "tools.png", "not the tools folder");

    std::vector<std::string> files = exportedFiles(project);
    CHECK(has(files, "project.ykproj") && has(files, "scenes/main.ykscene"));
    CHECK(has(files, "assets/hero.png") && has(files, "art/keep/a.png") &&
          has(files, "art/source/hero.psd") && has(files, "tools.png"));
    for (const char *left : {"tools/gen.py", "tools/data.json", "docs/guide.txt", "README.md",
                             "assets/notes.md", "assets/hero.png.bak", "assets/scratch.tmp",
                             "assets/backup~", ".hidden/x", "build/out.bin", "scripts.py"})
        CHECK(!has(files, left));
    CHECK(std::is_sorted(files.begin(), files.end()));

    // The project's own exclude list: a folder, a file, and a path written the awkward way.
    project.build.exclude = {"art/source/", ".\\assets\\hero.png.bak", "tools.png"};
    files = exportedFiles(project);
    CHECK(!has(files, "art/source/hero.psd") && !has(files, "tools.png") &&
          has(files, "art/keep/a.png"));
    // Excluding the project file itself has no effect.
    project.build.exclude = {"project.ykproj"};
    CHECK(has(exportedFiles(project), "project.ykproj"));
}

void zipFiles() {
    CHECK(crc32("123456789") == 0xCBF43926U); // The standard check value.
    CHECK(crc32("") == 0);

    TempDir dir("yk-zip");
    put(dir.path / "a.txt", "alpha");
    put(dir.path / "b.bin", std::string("\0\1\2\3\xff", 5));
    put(dir.path / "empty", "");
    const std::vector<ZipEntry> entries = {{"game/a.txt", dir.path / "a.txt", false},
                                           {"game/run", dir.path / "b.bin", true},
                                           {"game/empty", dir.path / "empty", false}};
    CHECK(writeZip(dir.path / "out.zip", entries));
    const auto read = readZip(dir.path / "out.zip");
    CHECK(read && read.value().size() == 3);
    if (read && read.value().size() == 3) {
        const auto &items = read.value();
        CHECK(items[0].name == "game/a.txt" && items[0].contents == "alpha" &&
              items[0].unixMode == 0100644);
        CHECK(items[1].name == "game/run" && items[1].contents == std::string("\0\1\2\3\xff", 5) &&
              items[1].unixMode == 0100755);
        CHECK(items[2].contents.empty() && items[2].size == 0);
    }
    // Reproducible: the same input gives the same bytes.
    CHECK(writeZip(dir.path / "again.zip", entries));
    CHECK(readTextFile(dir.path / "out.zip").value() ==
          readTextFile(dir.path / "again.zip").value());

    CHECK(!writeZip(dir.path / "x.zip", {{"../escape", dir.path / "a.txt", false}}));
    CHECK(!writeZip(dir.path / "x.zip", {{"/absolute", dir.path / "a.txt", false}}));
    CHECK(!writeZip(dir.path / "x.zip", {{"win\\path", dir.path / "a.txt", false}}));
    CHECK(!writeZip(dir.path / "x.zip",
                    {{"same", dir.path / "a.txt", false}, {"same", dir.path / "b.bin", false}}));
    CHECK(!writeZip(dir.path / "x.zip", {{"gone", dir.path / "nothing", false}}));
    CHECK(!fs::exists(dir.path / "x.zip")); // A failed write leaves nothing behind.

    // Damage is noticed: flip a byte of the stored data.
    auto bytes = readTextFile(dir.path / "out.zip").value();
    const auto at = bytes.find("alpha");
    CHECK(at != std::string::npos);
    if (at != std::string::npos) {
        bytes[at] = 'A';
        put(dir.path / "damaged.zip", bytes);
        const auto damaged = readZip(dir.path / "damaged.zip");
        CHECK(!damaged && damaged.error().find("checksum") != std::string::npos);
    }
    put(dir.path / "notzip.zip", "this is not an archive at all, not even close");
    CHECK(!readZip(dir.path / "notzip.zip"));
    CHECK(!readZip(dir.path / "missing.zip"));
}

void exportsForEveryTarget() {
    TempDir dir("yk-export-game");
    ComponentRegistry registry;
    registerStandardComponents(registry);
    const fs::path source = dir.path / "project";
    Project project = makeProject(source, registry);
    put(source / "tools" / "gen.py", "not shipped");
    put(dir.path / "player", "player program");
    put(dir.path / "notices" / "SDL3.txt", "sdl license");
    put(dir.path / "notices" / "Box2D-MIT.txt", "box2d license");

    std::vector<std::string> lines;
    ExportOptions options;
    options.player = dir.path / "player";
    options.notices = dir.path / "notices";
    options.progress = [&](const std::string &line) { lines.push_back(line); };

    // Linux: the program, data/, README, notices, metadata.
    options.target = BuildTarget::Linux;
    options.destination = dir.path / "out";
    auto linux = exportGame(project, registry, options);
    CHECK(linux);
    if (linux) {
        const ExportReport &report = linux.value();
        CHECK(report.output.filename() == "Cinder-Vale-linux");
        CHECK(report.executable == report.output / "CinderVale");
        CHECK(fs::is_regular_file(report.executable));
#ifndef _WIN32 // Windows has no execute bit.
        CHECK((fs::status(report.executable).permissions() & fs::perms::owner_exec) !=
              fs::perms::none);
#endif
        CHECK(report.dataFolder == report.output / "data");
        CHECK(fs::is_regular_file(report.dataFolder / "project.ykproj"));
        CHECK(fs::is_regular_file(report.dataFolder / "scenes" / "main.ykscene"));
        CHECK(fs::is_regular_file(report.dataFolder / "assets" / "hero.png"));
        CHECK(!fs::exists(report.dataFolder / "tools")); // Development files stay home.
        CHECK(fs::is_regular_file(report.output / "README.txt"));
        CHECK(fs::is_regular_file(report.output / "yk-export.json"));
        CHECK(fs::is_regular_file(report.output / "licenses" / "SDL3.txt"));
        CHECK(report.files == 3 && report.bytes > 0);
        // A warning names the notices that were missing (only two of the six were provided).
        CHECK(!report.warnings.empty());
        CHECK(readTextFile(report.output / "README.txt").value().find("CinderVale") !=
              std::string::npos);
        CHECK(Project::load(report.dataFolder)); // The copy is a project of its own.
        CHECK(report.archive.empty());
    }
    CHECK(!lines.empty() && lines.back().find("Exported to") == 0);

    // The same target again is refused, unless replacing is asked for; and only our own output
    // is ever replaced.
    CHECK(!exportGame(project, registry, options));
    options.overwrite = true;
    CHECK(exportGame(project, registry, options));
    fs::create_directories(dir.path / "out" / "Cinder-Vale-windows");
    put(dir.path / "out" / "Cinder-Vale-windows" / "precious.txt", "mine");
    options.target = BuildTarget::Windows;
    const auto refused = exportGame(project, registry, options);
    CHECK(!refused && refused.error().find("not made by an export") != std::string::npos);
    CHECK(fs::exists(dir.path / "out" / "Cinder-Vale-windows" / "precious.txt"));
    fs::remove_all(dir.path / "out" / "Cinder-Vale-windows");
    options.overwrite = false;

    // Windows: .exe program and a zip with the game in one top folder.
    options.archive = true;
    auto windows = exportGame(project, registry, options);
    CHECK(windows);
    if (windows) {
        CHECK(windows.value().executable.filename() == "CinderVale.exe");
        CHECK(fs::is_regular_file(windows.value().archive));
        const auto zip = readZip(windows.value().archive);
        CHECK(zip);
        if (zip) {
            std::vector<std::string> names;
            for (const ZipInfo &info : zip.value()) {
                names.push_back(info.name);
                CHECK(info.name.starts_with("Cinder-Vale-windows/"));
                CHECK(((info.unixMode & 0111) != 0) == info.name.ends_with("CinderVale.exe"));
            }
            CHECK(has(names, "Cinder-Vale-windows/CinderVale.exe"));
            CHECK(has(names, "Cinder-Vale-windows/data/project.ykproj"));
            CHECK(has(names, "Cinder-Vale-windows/data/assets/hero.png"));
            CHECK(has(names, "Cinder-Vale-windows/README.txt"));
            CHECK(has(names, "Cinder-Vale-windows/licenses/SDL3.txt"));
        }
    }

    // macOS: an app bundle whose Resources folder holds the data.
    options.target = BuildTarget::MacOS;
    auto mac = exportGame(project, registry, options);
    CHECK(mac);
    if (mac) {
        const fs::path app = mac.value().output;
        CHECK(app.filename() == "Cinder Vale.app");
        CHECK(mac.value().executable == app / "Contents" / "MacOS" / "CinderVale");
        CHECK(fs::is_regular_file(mac.value().executable));
        CHECK(mac.value().dataFolder == app / "Contents" / "Resources" / "data");
        CHECK(fs::is_regular_file(app / "Contents" / "Resources" / "data" / "project.ykproj"));
        const std::string plist = readTextFile(app / "Contents" / "Info.plist").value();
        CHECK(plist.find("<string>CinderVale</string>") != std::string::npos);
        CHECK(plist.find("com.yk.cindervale") != std::string::npos);
        CHECK(plist.find("<key>NSHighResolutionCapable</key>") != std::string::npos);
        CHECK(plist.find("1.0.0") != std::string::npos);
        const auto zip = readZip(mac.value().archive);
        CHECK(zip);
        if (zip)
            for (const ZipInfo &info : zip.value())
                CHECK(info.name.starts_with("Cinder Vale.app/"));
    }
}

void exportRefusals() {
    TempDir dir("yk-export-refusals");
    ComponentRegistry registry;
    registerStandardComponents(registry);
    Project project = makeProject(dir.path / "project", registry);
    put(dir.path / "player", "player program");
    ExportOptions options;
    options.target = BuildTarget::Linux;
    options.destination = dir.path / "out";
    options.player = dir.path / "player";

    // No player program for the target.
    ExportOptions noPlayer = options;
    noPlayer.player = dir.path / "no_such_player";
    const auto missing = exportGame(project, registry, noPlayer);
    CHECK(!missing && missing.error().find("player") != std::string::npos);
    ExportOptions noDestination = options;
    noDestination.destination.clear();
    CHECK(!exportGame(project, registry, noDestination));

    // Without notices the export works, and says what it left out.
    const auto bare = exportGame(project, registry, options);
    CHECK(bare && !bare.value().warnings.empty() &&
          bare.value().warnings.front().find("notices") != std::string::npos);
    CHECK(bare && !fs::exists(bare.value().output / "licenses"));

    // Excluding something the game needs is caught by checking the copy, and nothing is left.
    ExportOptions again = options;
    again.destination = dir.path / "out2";
    project.build.exclude = {"assets"};
    const auto incomplete = exportGame(project, registry, again);
    CHECK(!incomplete && incomplete.error().find("incomplete") != std::string::npos);
    CHECK(!fs::exists(dir.path / "out2" / "Cinder-Vale-linux"));
    project.build.exclude.clear();

    // A project with errors is refused before anything is written.
    put(dir.path / "project" / "scenes" / "broken.ykscene", "{ not json");
    const auto broken = exportGame(project, registry, again);
    CHECK(!broken && broken.error().find("error") != std::string::npos);
    CHECK(!fs::exists(dir.path / "out2"));
}

void findingPlayers() {
    TempDir dir("yk-export-find");
    const fs::path here = dir.path / "bin";
    fs::create_directories(here);
    // The player next to the tool is the one for the host system, and only that one.
    CHECK(!findPlayer(hostTarget(), here));
    put(here / playerFileName(hostTarget()), "player");
    CHECK(findPlayer(hostTarget(), here).has_value());
    const BuildTarget other =
        hostTarget() == BuildTarget::Windows ? BuildTarget::Linux : BuildTarget::Windows;
    CHECK(!findPlayer(other, here));
    // Other systems come from templates/<target>/.
    put(here / "templates" / name(other) / playerFileName(other), "player");
    const auto found = findPlayer(other, here);
    CHECK(found && found->filename() == playerFileName(other));
    // An install keeps them under share/yk-engine.
    put(dir.path / "share" / "yk-engine" / "templates" / "macos" / "yk_player", "player");
    if (hostTarget() != BuildTarget::MacOS && other != BuildTarget::MacOS)
        CHECK(findPlayer(BuildTarget::MacOS, here).has_value());

    CHECK(!findNotices(here));
    put(here / "licenses" / "SDL3.txt", "license");
    CHECK(findNotices(here).has_value());
}
} // namespace

int main() {
    targetsAndNames();
    buildSettingsRoundTrip();
    whichFilesShip();
    zipFiles();
    exportsForEveryTarget();
    exportRefusals();
    findingPlayers();
    return yk::test::finish("export");
}
