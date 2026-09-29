#pragma once
#include "yk/assets/Project.hpp"
#include "yk/scene/Registry.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Turning a project into a game somebody else can run: the player program for the target system,
// renamed after the game, next to a copy of the project's data. The editor's Build > Export Game
// and the `yk export` command line both call exportGame(), so they cannot disagree.
//
//   Windows, Linux   <Product>-<target>/
//                        <executable>[.exe]   the player
//                        data/                the project (project.ykproj, scenes, assets...)
//                        licenses/  README.txt  yk-export.json
//   macOS            <Product>.app/Contents/
//                        MacOS/<executable>   the player
//                        Resources/data/      the project (SDL reports Resources as the base path)
//                        Resources/licenses/  README.txt  yk-export.json
//                        Info.plist
//
// The player has to be built for the target: exportGame() copies it, it does not compile anything.
// Building for another operating system means building yk_player there (or with a cross toolchain,
// see docs/BUILDING.md) and handing its path in ExportOptions::player.
namespace yk {
enum class BuildTarget { Linux, Windows, MacOS };
inline constexpr BuildTarget allBuildTargets[] = {BuildTarget::Windows, BuildTarget::MacOS,
                                                  BuildTarget::Linux};
const char *name(BuildTarget target);        // "windows", "macos", "linux"
const char *displayName(BuildTarget target); // "Windows", "macOS", "Linux"
std::optional<BuildTarget> buildTargetFromName(std::string_view text);
BuildTarget hostTarget();
std::string playerFileName(BuildTarget target); // "yk_player.exe" or "yk_player"
std::string_view engineVersion();

// How the game is called, from the project's build settings with sensible fallbacks.
std::string productName(const Project &project);
// Program file name without extension: letters, digits, '-' and '_' only.
std::string executableName(const Project &project);
std::string bundleIdentifier(const Project &project);
// The folder (or .app bundle) that exportGame() creates inside the destination.
std::string exportFolderName(const Project &project, BuildTarget target);

// The project files a shipped game carries: everything except hidden entries, the build folder,
// tools/, docs/, scripts, notes and backup files, and the settings' own exclude list. Project-
// relative, '/' separated, sorted; project.ykproj is always included.
std::vector<std::string> exportedFiles(const Project &project);

struct ExportOptions {
    BuildTarget target{hostTarget()};
    std::filesystem::path destination; // Parent folder of the game folder or bundle.
    std::filesystem::path player;      // The player built for `target`.
    // Folder holding the third-party notices (Box2D-MIT.txt, SDL3.txt, ...) the player's libraries
    // require you to ship. Empty: none are copied and the report says so.
    std::filesystem::path notices;
    bool archive{false};   // Also write <folder>.zip beside the game.
    bool overwrite{false}; // Replace an earlier export made by this tool.
    std::function<void(const std::string &)> progress; // One line per step.
};
struct ExportReport {
    std::filesystem::path output;     // The game folder, or the .app bundle.
    std::filesystem::path executable; // The player inside it.
    std::filesystem::path dataFolder; // Where the project's files went.
    std::filesystem::path archive;    // Empty unless ExportOptions::archive.
    std::size_t files{};              // Project files copied.
    std::uintmax_t bytes{};           // Their total size.
    std::vector<std::string> warnings;
};
// Checks the project (errors stop the export), copies the player and the project's data, writes
// the README, notices and bundle metadata, then validates the exported copy on its own so a game
// that would be missing a file it needs is never handed out.
Result<ExportReport> exportGame(const Project &project, const ComponentRegistry &registry,
                                const ExportOptions &options);

// Where a player program for `target` lives, searching next to `executableDir` (the editor or
// `yk`): for the host system the player beside it, for any system `templates/<target>/` next to it
// or `../share/yk-engine/templates/<target>/`, and the folder named by $YK_TEMPLATES.
std::optional<std::filesystem::path> findPlayer(BuildTarget target,
                                                const std::filesystem::path &executableDir);
// The notices folder of an installation or build tree, when it can be found.
std::optional<std::filesystem::path> findNotices(const std::filesystem::path &executableDir);
} // namespace yk
