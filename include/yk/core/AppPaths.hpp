#pragma once
#include <filesystem>
#include <string_view>

// Where a program lives and where it keeps its per-user files, asked of the operating system rather
// than guessed from argv[0], the working directory or the source tree. This is what keeps an
// installed or bundled program working outside the folder it was built in. No SDL: the headless
// `yk` tool uses it too.
namespace yk {
// The running program's absolute path, or empty when the system cannot say.
std::filesystem::path executablePath();
// The folder holding the running program ("." when unknown). Inside a macOS bundle that is
// Name.app/Contents/MacOS, which is not what SDL_GetBasePath reports (it reports Resources).
std::filesystem::path executableDirectory();

// For a program in Name.app/Contents/MacOS: the bundle's Contents/Resources folder. Empty for any
// other folder (a build tree, an installation prefix, a Windows or Linux game folder).
std::filesystem::path bundleResourcesFor(const std::filesystem::path &executableDirectory);
// bundleResourcesFor(executableDirectory()).
std::filesystem::path bundleResourcesDirectory();

// The current user's home folder ("." when it cannot be found).
std::filesystem::path homeDirectory();

struct UserDirectories {
    std::filesystem::path support; // Settings and other data the user keeps.
    std::filesystem::path logs;    // Log files, crash reports, the session marker.
};
// The per-user folders of an application, by each system's convention (`organization` may be
// empty; the folders are <org>/<app> below these):
//   macOS    ~/Library/Application Support     ~/Library/Logs
//   Windows  %APPDATA%                         %LOCALAPPDATA%\<org>\<app>\Logs
//   Linux    $XDG_DATA_HOME or ~/.local/share  $XDG_STATE_HOME or ~/.local/state, then
//   <org>/<app>/logs
// The support folder matches SDL_GetPrefPath. Setting YK_LOG_DIR replaces the log folder (tests,
// packagers and anyone who wants the logs elsewhere).
UserDirectories userDirectories(std::string_view organization, std::string_view application);
} // namespace yk
