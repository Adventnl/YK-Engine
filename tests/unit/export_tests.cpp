// Packaging a project as a game: build targets and names, which files ship, the ZIP writer, and
// exportGame() for every target against a stand-in player program.
#include "support/check.hpp"
#include "yk/assets/Export.hpp"
#include "yk/assets/Icon.hpp"
#include "yk/assets/Validation.hpp"
#include "yk/assets/Zip.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <algorithm>
#include <cstdlib>
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
    settings.icon = "assets/icon.png";
    settings.copyright = "(c) 2026 Shiny Studio";
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

void setEnvironment(const char *name, const std::string &value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
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

    // $YK_TEMPLATES is searched first, for players kept outside the installation.
    CHECK(!environmentVariable("YK_SURELY_NOT_SET_9F3A"));
    const fs::path shared = dir.path / "shared";
    put(shared / name(other) / playerFileName(other), "player");
    setEnvironment("YK_TEMPLATES", shared.string());
    CHECK(environmentVariable("YK_TEMPLATES") == shared.string());
    const auto fromEnvironment = findPlayer(other, dir.path / "nowhere");
    CHECK(fromEnvironment && fromEnvironment->parent_path().filename() == name(other));
    setEnvironment("YK_TEMPLATES", "");
    CHECK(!findPlayer(other, dir.path / "nowhere"));
}
// The header of a PNG of the given size; enough for pngSize() and makeIcns(), which never decode.
std::string fakePng(int width, int height) {
    std::string png("\x89PNG\r\n\x1a\n", 8);
    const auto be = [&](int value) {
        for (int shift = 24; shift >= 0; shift -= 8)
            png.push_back(static_cast<char>((value >> shift) & 0xFF));
    };
    be(13);
    png += "IHDR";
    be(width);
    be(height);
    png += std::string("\x08\x06\x00\x00\x00", 5);
    png += "pixels would follow";
    return png;
}

void appIcons() {
    CHECK(pngSize(fakePng(512, 512)) && pngSize(fakePng(512, 256)).value().height == 256);
    CHECK(!pngSize("not a png at all, just some text here"));
    CHECK(!pngSize(""));
    CHECK(!pngSize(fakePng(0, 10)));

    // .icns: "icns", total length, then one entry (type, length, the PNG itself).
    for (const auto &[edge, type] :
         {std::pair{128, "ic07"}, {300, "ic08"}, {512, "ic09"}, {1024, "ic10"}, {2048, "ic10"}}) {
        const std::string png = fakePng(edge, edge);
        const auto icns = makeIcns(png);
        CHECK(icns);
        if (!icns)
            continue;
        const std::string &file = icns.value();
        CHECK(file.substr(0, 4) == "icns" && file.substr(8, 4) == type);
        CHECK(file.size() == 16 + png.size() && file.substr(16) == png);
        const auto length = [&](std::size_t at) {
            return (static_cast<unsigned char>(file[at]) << 24) |
                   (static_cast<unsigned char>(file[at + 1]) << 16) |
                   (static_cast<unsigned char>(file[at + 2]) << 8) |
                   static_cast<unsigned char>(file[at + 3]);
        };
        CHECK(static_cast<std::size_t>(length(4)) == file.size());
        CHECK(static_cast<std::size_t>(length(12)) == file.size() - 8);
    }
    CHECK(!makeIcns(fakePng(512, 256))); // not square
    CHECK(!makeIcns(fakePng(64, 64)));   // too small
    CHECK(!makeIcns("GIF89a....................."));

    // Validation: a missing, broken or unsuitable icon is reported before an export fails on it.
    TempDir dir("yk-export-icons");
    ComponentRegistry registry;
    registerStandardComponents(registry);
    Project project = makeProject(dir.path / "project", registry);
    const auto errors = [&] {
        std::string all;
        for (const ProjectIssue &issue : validateProject(project, registry))
            if (issue.message.find("icon") != std::string::npos)
                all += (issue.severity == ProjectIssue::Severity::Error ? "E:" : "W:") +
                       issue.message + "\n";
        return all;
    };
    CHECK(errors().empty());
    project.build.icon = "assets/icon.png";
    CHECK(errors().starts_with("E:") && errors().find("does not exist") != std::string::npos);
    put(dir.path / "project" / "assets" / "icon.png", "this is text");
    CHECK(errors().find("not a PNG") != std::string::npos);
    put(dir.path / "project" / "assets" / "icon.png", fakePng(512, 256));
    CHECK(errors().find("square") != std::string::npos);
    put(dir.path / "project" / "assets" / "icon.png", fakePng(256, 256));
    CHECK(errors().starts_with("W:"));
    put(dir.path / "project" / "assets" / "icon.png", fakePng(1024, 1024));
    CHECK(errors().empty());
}

// codesign and hdiutil are stood in for by a recorder, so the steps and their order are checked
// on any system; the real tools run in the macOS CI job.
void macOsBundleTools() {
    TempDir dir("yk-export-mac");
    ComponentRegistry registry;
    registerStandardComponents(registry);
    Project project = makeProject(dir.path / "project", registry);
    project.build.icon = "assets/icon.png";
    project.build.copyright = "(c) 2026 Cinder & Co";
    put(dir.path / "project" / "assets" / "icon.png", fakePng(1024, 1024));
    put(dir.path / "player", "player program");

    std::vector<std::vector<std::string>> calls;
    bool stageHadApp = false, stageHadLink = false;
    int failOn = -1; // The index of a call to make fail.
    ExportOptions options;
    options.target = BuildTarget::MacOS;
    options.destination = dir.path / "out";
    options.player = dir.path / "player";
    options.codesign = "-";
    options.dmg = true;
    options.runTool = [&](const std::vector<std::string> &arguments) -> Result<ProcessResult> {
        calls.push_back(arguments);
        if (arguments.front() == "hdiutil") {
            const fs::path stage = arguments[5]; // hdiutil create -volname N -srcfolder <stage> ...
            stageHadApp = fs::is_directory(stage / "Cinder Vale.app" / "Contents");
            stageHadLink = fs::is_symlink(stage / "Applications");
            put(arguments.back(), "disk image");
        }
        if (static_cast<int>(calls.size()) - 1 == failOn)
            return ProcessResult{1, "the tool said no\n"};
        return ProcessResult{};
    };
    auto result = exportGame(project, registry, options);
    CHECK(result);
    if (result) {
        const fs::path app = result.value().output;
        // Icon and metadata.
        const std::string icns = readTextFile(app / "Contents/Resources/AppIcon.icns").value();
        CHECK(icns.substr(0, 4) == "icns" && icns.find("ic10") == 8);
        const std::string plist = readTextFile(app / "Contents/Info.plist").value();
        CHECK(plist.find("<key>CFBundleIconFile</key>") != std::string::npos);
        CHECK(plist.find("<string>AppIcon</string>") != std::string::npos);
        CHECK(plist.find("(c) 2026 Cinder &amp; Co") != std::string::npos);
        // Steps: sign, verify, then the disk image; the .app is untouched beside the image.
        CHECK(calls.size() == 3);
        if (calls.size() == 3) {
            CHECK((calls[0] == std::vector<std::string>{"codesign", "--force", "--deep", "--sign",
                                                        "-", app.string()}));
            CHECK((calls[1] == std::vector<std::string>{"codesign", "--verify", "--deep",
                                                        "--strict", app.string()}));
            CHECK(calls[2][0] == "hdiutil" && calls[2][1] == "create" &&
                  calls[2][2] == "-volname" && calls[2][3] == "Cinder Vale");
        }
        CHECK(stageHadApp && stageHadLink);
        CHECK(result.value().diskImage == dir.path / "out" / "Cinder Vale.dmg");
        CHECK(fs::is_regular_file(result.value().diskImage));
        CHECK(!fs::exists(dir.path / "out" / ".yk-dmg-staging"));
    }

    // A signing identity means the hardened runtime and a time stamp, and the image is signed too.
    calls.clear();
    options.codesign = "Developer ID Application: Cinder Co (ABCDE12345)";
    options.overwrite = true;
    result = exportGame(project, registry, options);
    CHECK(result && calls.size() == 4);
    if (calls.size() == 4) {
        const std::vector<std::string> &sign = calls[0];
        CHECK(std::find(sign.begin(), sign.end(), "runtime") != sign.end());
        CHECK(std::find(sign.begin(), sign.end(), "--timestamp") != sign.end());
        CHECK(std::find(sign.begin(), sign.end(), options.codesign) != sign.end());
        CHECK(calls[3][0] == "codesign" && calls[3].back().ends_with("Cinder Vale.dmg"));
    }

    // A signing failure leaves nothing half-made and carries the tool's own words.
    calls.clear();
    options.dmg = false;
    failOn = 0;
    const auto failed = exportGame(project, registry, options);
    CHECK(!failed && failed.error().find("the tool said no") != std::string::npos);
    CHECK(!fs::exists(dir.path / "out" / "Cinder Vale.app"));

    // A failed disk image is reported, the app itself stays.
    calls.clear();
    options.dmg = true;
    options.codesign.clear();
    failOn = 0;
    const auto noImage = exportGame(project, registry, options);
    CHECK(!noImage && noImage.error().find("disk image") != std::string::npos);
    CHECK(fs::is_directory(dir.path / "out" / "Cinder Vale.app"));
    failOn = -1;

    // Only macOS bundles are signed or imaged, and only where Apple's tools exist.
    ExportOptions linux = options;
    linux.target = BuildTarget::Linux;
    linux.overwrite = true;
    CHECK(!exportGame(project, registry, linux));
    if (hostTarget() != BuildTarget::MacOS) {
        ExportOptions bare;
        bare.target = BuildTarget::MacOS;
        bare.destination = dir.path / "out2";
        bare.player = dir.path / "player";
        bare.dmg = true;
        const auto noTools = exportGame(project, registry, bare);
        CHECK(!noTools && noTools.error().find("need a Mac") != std::string::npos);
        CHECK(!fs::exists(dir.path / "out2"));
    }
    // A bad icon stops the export and cleans up.
    put(dir.path / "project" / "assets" / "icon.png", fakePng(500, 300));
    options.dmg = false;
    options.runTool = {};
    options.overwrite = true;
    CHECK(!exportGame(project, registry, options));
}

void runningProcesses() {
#if !defined(_WIN32)
    const auto echoed = runProcess({"sh", "-c", "echo out; echo err >&2; exit 3"});
    CHECK(echoed && echoed.value().exitCode == 3);
    CHECK(echoed && echoed.value().output.find("out") != std::string::npos &&
          echoed.value().output.find("err") != std::string::npos);
    // Arguments are passed as they are, no shell in between.
    const auto quoted = runProcess({"printf", "%s|", "a b", "$HOME", "'q'"});
    CHECK(quoted && quoted.value().output == "a b|$HOME|'q'|"); // Nothing was expanded.
    CHECK(!runProcess({"yk-no-such-program-9f3a"}));
    CHECK(!runProcess({}));
#else
    CHECK(!runProcess({"cmd"}));
#endif
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
    appIcons();
    macOsBundleTools();
    runningProcesses();
    return yk::test::finish("export");
}
