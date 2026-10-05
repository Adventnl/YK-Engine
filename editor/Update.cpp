#include "Update.hpp"
#include "yk/core/AppPaths.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Process.hpp"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace yk::editor {
namespace {
#if defined(_WIN32) || defined(__APPLE__)
constexpr int installerStarted = 10;
#endif

bool shouldCheck(int argc, char **argv) {
    const auto disabled = environmentVariable("YK_DISABLE_UPDATE");
    if (disabled && *disabled == "1")
        return false;
    // Automated runs must stay offline and must never replace the test installation.
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--script" || argument == "--frames" || argument == "--help" ||
            argument == "-h" || argument == "--capture")
            return false;
    }
    return true;
}

#if defined(_WIN32)
int runWindowsUpdater(const std::filesystem::path &script, const std::filesystem::path &root) {
    wchar_t systemDirectory[MAX_PATH]{};
    if (GetSystemDirectoryW(systemDirectory, MAX_PATH) == 0)
        return -1;
    const std::filesystem::path powershell =
        std::filesystem::path(systemDirectory) / L"WindowsPowerShell/v1.0/powershell.exe";
    if (!std::filesystem::exists(powershell))
        return -1;
    // Windows file names cannot contain quotes. The PowerShell executable and script are fixed
    // local paths, and both the script and install root are quoted as single process arguments.
    std::wstring command = L"\"" + powershell.wstring() +
                           L"\" -NoProfile -NonInteractive -WindowStyle Hidden "
                           L"-ExecutionPolicy Bypass -File \"" + script.wstring() + L"\" " +
                           L"\"" + std::filesystem::path(YK_VERSION).wstring() + L"\" \"" +
                           root.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        return -1;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}
#endif
} // namespace

bool startUpdateIfAvailable(int argc, char **argv) {
    if (!shouldCheck(argc, argv))
        return false;
#if defined(_WIN32)
    const auto directory = executableDirectory();
    const auto root = directory.parent_path();
    const auto script = root / "share/yk-engine/update/windows.ps1";
    if (!std::filesystem::is_regular_file(script))
        return false;
    return runWindowsUpdater(script, root) == installerStarted;
#elif defined(__APPLE__)
    const auto resources = bundleResourcesDirectory();
    const auto script = resources / "macos.sh";
    if (resources.empty() || !std::filesystem::is_regular_file(script))
        return false;
    const auto result = runProcess({"/bin/bash", script.string(), YK_VERSION,
                                    resources.parent_path().parent_path().string()});
    return result && result.value().exitCode == installerStarted;
#else
    return false;
#endif
}
} // namespace yk::editor
