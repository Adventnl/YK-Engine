#include "yk/core/AppPaths.hpp"
#include "yk/core/FileIO.hpp"
#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace yk {
namespace fs = std::filesystem;

fs::path executablePath() {
    std::error_code error;
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size); // Reports the size needed.
    std::vector<char> buffer(size + 1U, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        return {};
    const fs::path resolved = fs::weakly_canonical(fs::path(buffer.data()), error);
    return error ? fs::path(buffer.data()) : resolved;
#elif defined(_WIN32)
    std::vector<wchar_t> buffer(512, L'\0');
    for (;;) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return {};
        if (length < buffer.size())
            return fs::path(std::wstring(buffer.data(), length));
        buffer.resize(buffer.size() * 2U); // Truncated: try a longer buffer.
    }
#else
    const fs::path self = fs::read_symlink("/proc/self/exe", error);
    return error ? fs::path{} : self;
#endif
}

fs::path executableDirectory() {
    const fs::path program = executablePath();
    return program.empty() ? fs::path(".") : program.parent_path();
}

fs::path bundleResourcesFor(const fs::path &directory) {
    const fs::path contents = directory.parent_path();
    const fs::path bundle = contents.parent_path();
    if (directory.filename() == "MacOS" && contents.filename() == "Contents" &&
        bundle.extension() == ".app")
        return contents / "Resources";
    return {};
}

fs::path bundleResourcesDirectory() {
    return bundleResourcesFor(executableDirectory());
}

fs::path homeDirectory() {
#if defined(_WIN32)
    if (const auto profile = environmentVariable("USERPROFILE"); profile && !profile->empty())
        return *profile;
#else
    if (const auto home = environmentVariable("HOME"); home && !home->empty())
        return *home;
    if (const passwd *entry = getpwuid(getuid()); entry != nullptr && entry->pw_dir != nullptr)
        return entry->pw_dir;
#endif
    return ".";
}

#if !defined(__APPLE__)
namespace {
// $NAME when it is set to an absolute path, else `fallback` (the XDG rules ignore relative values).
fs::path absoluteEnvironment(std::string_view name, const fs::path &fallback) {
    if (const auto value = environmentVariable(name); value && !value->empty()) {
        const fs::path path(*value);
        if (path.is_absolute())
            return path;
    }
    return fallback;
}
} // namespace
#endif

UserDirectories userDirectories(std::string_view organization, std::string_view application) {
    fs::path relative;
    if (!organization.empty())
        relative /= std::string(organization);
    relative /= std::string(application);
    const fs::path home = homeDirectory();
    UserDirectories result;
#if defined(__APPLE__)
    result.support = home / "Library" / "Application Support" / relative;
    result.logs = home / "Library" / "Logs" / relative;
#elif defined(_WIN32)
    result.support = absoluteEnvironment("APPDATA", home / "AppData" / "Roaming") / relative;
    result.logs =
        absoluteEnvironment("LOCALAPPDATA", home / "AppData" / "Local") / relative / "Logs";
#else
    result.support = absoluteEnvironment("XDG_DATA_HOME", home / ".local" / "share") / relative;
    result.logs =
        absoluteEnvironment("XDG_STATE_HOME", home / ".local" / "state") / relative / "logs";
#endif
    if (const auto override = environmentVariable("YK_LOG_DIR"); override && !override->empty())
        result.logs = *override;
    return result;
}
} // namespace yk
