#include "yk/assets/Export.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Validation.hpp"
#include "yk/assets/Zip.hpp"
#include "yk/core/FileIO.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>

#ifndef YK_VERSION
#define YK_VERSION "0.0.0"
#endif

namespace yk {
namespace fs = std::filesystem;

namespace {
// The notices the player's own libraries (SDL, Box2D, stb_image) require a redistributed binary to
// carry. The editor's fonts, icons and Dear ImGui are not in the player.
constexpr const char *runtimeNotices[] = {"Box2D-MIT.txt",       "SDL3.txt",
                                          "SDL3-HIDAPI-BSD.txt", "SDL3-yuv2rgb-BSD.txt",
                                          "SDL3-fdlibm.txt",     "stb_image-PD.txt"};

bool isAlnum(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

// Keeps letters, digits, '.', '-' and '_'; a space becomes `space` (or nothing when it is 0) and
// anything else, non-ASCII bytes included, becomes '_'. Names that survive on every file system.
std::string fileSafe(std::string_view text, char space) {
    std::string out;
    for (const char c : text) {
        if (isAlnum(c) || c == '.' || c == '-' || c == '_')
            out.push_back(c);
        else if (c == ' ' || c == '\t')
            out.push_back(space);
        else
            out.push_back('_');
    }
    out.erase(std::remove(out.begin(), out.end(), '\0'), out.end());
    while (!out.empty() && (out.front() == '.' || out.front() == '-'))
        out.erase(out.begin());
    while (!out.empty() && out.back() == '.')
        out.pop_back();
    return out;
}

// The name of a macOS bundle keeps its spaces; only characters that cannot be in a file name go.
std::string bundleSafe(std::string_view text) {
    std::string out;
    for (const char c : text)
        out.push_back((static_cast<unsigned char>(c) < 0x20 || c == '/' || c == '\\' || c == ':' ||
                       c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                          ? '_'
                          : c);
    while (!out.empty() && (out.front() == '.' || out.front() == ' '))
        out.erase(out.begin());
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();
    return out;
}

std::string xmlEscape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out.push_back(c);
        }
    }
    return out;
}

std::string normalizedExclude(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.starts_with("./"))
        path.erase(0, 2);
    while (!path.empty() && path.back() == '/')
        path.pop_back();
    return path;
}

bool neverShipped(const std::string &path) {
    const fs::path file(path);
    const std::string first = file.begin() == file.end() ? std::string() : file.begin()->string();
    if (file.has_parent_path() && (first == "tools" || first == "docs" || first == "build"))
        return true;
    const std::string extension = file.extension().string();
    if (extension == ".py" || extension == ".md" || extension == ".bak" || extension == ".tmp" ||
        extension == ".orig")
        return true;
    const std::string leaf = file.filename().string();
    return !leaf.empty() && leaf.back() == '~';
}

void say(const ExportOptions &options, const std::string &line) {
    if (options.progress)
        options.progress(line);
}

std::string readmeFor(const Project &project, BuildTarget target, const std::string &executable,
                      bool hasNotices) {
    const std::string product = productName(project);
    std::string text = product + " " + project.build.version + "\n\n";
    if (target == BuildTarget::MacOS)
        text += "Open " + bundleSafe(product) + ".app to play.\n";
    else
        text +=
            "Run " + executable + (target == BuildTarget::Windows ? ".exe" : "") +
            " to play. The game's files are in the data folder next to it; keep them together.\n";
    text += "\nMade with YK Engine " + std::string(YK_VERSION) + ".\n";
    if (hasNotices)
        text += "Notices for the libraries the game program contains are in the licenses folder.\n";
    return text;
}

std::string infoPlist(const Project &project, const std::string &executable) {
    const std::string product = xmlEscape(productName(project));
    const std::string version = xmlEscape(project.build.version);
    std::string text = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                       "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                       "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                       "<plist version=\"1.0\">\n<dict>\n";
    const auto entry = [&](const char *key, const std::string &value) {
        text += std::string("  <key>") + key + "</key>\n  <string>" + value + "</string>\n";
    };
    entry("CFBundleDevelopmentRegion", "en");
    entry("CFBundleExecutable", xmlEscape(executable));
    entry("CFBundleIdentifier", xmlEscape(bundleIdentifier(project)));
    entry("CFBundleInfoDictionaryVersion", "6.0");
    entry("CFBundleName", product);
    entry("CFBundleDisplayName", product);
    entry("CFBundlePackageType", "APPL");
    entry("CFBundleShortVersionString", version);
    entry("CFBundleVersion", version);
    entry("LSMinimumSystemVersion", "11.0");
    entry("LSApplicationCategoryType", "public.app-category.games");
    entry("NSPrincipalClass", "NSApplication");
    text += "  <key>NSHighResolutionCapable</key>\n  <true/>\n</dict>\n</plist>\n";
    return text;
}

Status copyFile(const fs::path &from, const fs::path &to) {
    std::error_code error;
    fs::create_directories(to.parent_path(), error);
    if (error)
        return Error{"Cannot create '" + to.parent_path().string() + "': " + error.message()};
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, error);
    if (error)
        return Error{"Cannot copy '" + from.string() + "': " + error.message()};
    return success();
}
} // namespace

const char *name(BuildTarget target) {
    switch (target) {
    case BuildTarget::Windows:
        return "windows";
    case BuildTarget::MacOS:
        return "macos";
    case BuildTarget::Linux:
        break;
    }
    return "linux";
}
const char *displayName(BuildTarget target) {
    switch (target) {
    case BuildTarget::Windows:
        return "Windows";
    case BuildTarget::MacOS:
        return "macOS";
    case BuildTarget::Linux:
        break;
    }
    return "Linux";
}
std::optional<BuildTarget> buildTargetFromName(std::string_view text) {
    std::string lower;
    for (const char c : text)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lower == "windows" || lower == "win" || lower == "win64")
        return BuildTarget::Windows;
    if (lower == "macos" || lower == "mac" || lower == "osx")
        return BuildTarget::MacOS;
    if (lower == "linux")
        return BuildTarget::Linux;
    return std::nullopt;
}
BuildTarget hostTarget() {
#if defined(_WIN32)
    return BuildTarget::Windows;
#elif defined(__APPLE__)
    return BuildTarget::MacOS;
#else
    return BuildTarget::Linux;
#endif
}
std::string playerFileName(BuildTarget target) {
    return target == BuildTarget::Windows ? "yk_player.exe" : "yk_player";
}
std::string_view engineVersion() {
    return YK_VERSION;
}

std::string productName(const Project &project) {
    if (!project.build.productName.empty())
        return project.build.productName;
    return project.name.empty() ? std::string("Game") : project.name;
}
std::string executableName(const Project &project) {
    std::string name = fileSafe(
        project.build.executable.empty() ? productName(project) : project.build.executable, 0);
    return name.empty() ? std::string("game") : name;
}
std::string bundleIdentifier(const Project &project) {
    if (!project.build.identifier.empty())
        return project.build.identifier;
    std::string id;
    for (const char c : productName(project))
        if (isAlnum(c) && static_cast<unsigned char>(c) < 0x80)
            id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return "com.yk." + (id.empty() ? std::string("game") : id);
}
std::string exportFolderName(const Project &project, BuildTarget target) {
    if (target == BuildTarget::MacOS) {
        const std::string bundle = bundleSafe(productName(project));
        return (bundle.empty() ? std::string("Game") : bundle) + ".app";
    }
    const std::string base = fileSafe(productName(project), '-');
    return (base.empty() ? std::string("game") : base) + "-" + name(target);
}

std::vector<std::string> exportedFiles(const Project &project) {
    std::vector<std::string> excluded;
    for (const std::string &path : project.build.exclude)
        excluded.push_back(normalizedExclude(path));
    std::vector<std::string> files;
    for (const AssetEntry &entry : scanAssets(project)) {
        if (entry.path != Project::fileName) {
            if (neverShipped(entry.path))
                continue;
            const bool left =
                std::any_of(excluded.begin(), excluded.end(), [&](const std::string &e) {
                    return !e.empty() && (entry.path == e || entry.path.starts_with(e + "/"));
                });
            if (left)
                continue;
        }
        files.push_back(entry.path);
    }
    return files;
}

std::optional<fs::path> findPlayer(BuildTarget target, const fs::path &executableDir) {
    std::vector<fs::path> candidates;
    const std::string file = playerFileName(target);
    if (const auto templates = environmentVariable("YK_TEMPLATES");
        templates && !templates->empty())
        candidates.push_back(fs::path(*templates) / name(target) / file);
    if (target == hostTarget())
        candidates.push_back(executableDir / file);
    candidates.push_back(executableDir / "templates" / name(target) / file);
    candidates.push_back(executableDir / ".." / "share" / "yk-engine" / "templates" / name(target) /
                         file);
    // Inside a macOS bundle the programs are in Contents/MacOS and the data in Contents/Resources.
    candidates.push_back(executableDir / ".." / "Resources" / "templates" / name(target) / file);
    for (const fs::path &candidate : candidates) {
        std::error_code error;
        if (fs::is_regular_file(candidate, error))
            return fs::weakly_canonical(candidate, error);
    }
    return std::nullopt;
}

std::optional<fs::path> findNotices(const fs::path &executableDir) {
    const fs::path candidates[] = {
        executableDir / "licenses", executableDir / ".." / "Resources" / "licenses",
        executableDir / ".." / "share" / "doc" / "YKEngine" / "licenses",
        executableDir / ".." / "LICENSES", executableDir / ".." / ".." / "LICENSES"};
    for (const fs::path &candidate : candidates) {
        std::error_code error;
        if (fs::is_regular_file(candidate / "SDL3.txt", error))
            return fs::weakly_canonical(candidate, error);
    }
    return std::nullopt;
}

Result<ExportReport> exportGame(const Project &project, const ComponentRegistry &registry,
                                const ExportOptions &options) {
    std::error_code error;
    if (!fs::is_regular_file(options.player, error))
        return Error{"The " + std::string(displayName(options.target)) +
                     " player program was not found (" + options.player.string() +
                     "). Build yk_player for that system first; see docs/BUILDING.md."};
    if (options.destination.empty())
        return Error{"Choose a folder to export into"};

    say(options, "Checking the project '" + project.name + "'");
    const std::vector<ProjectIssue> issues = validateProject(project, registry);
    if (hasErrors(issues)) {
        std::size_t errors = 0;
        for (const ProjectIssue &issue : issues)
            errors += issue.severity == ProjectIssue::Severity::Error ? 1 : 0;
        for (const ProjectIssue &issue : issues)
            if (issue.severity == ProjectIssue::Severity::Error)
                return Error{"The project has " + std::to_string(errors) +
                             " error(s); fix them first (Build > Validate Project). First: " +
                             issue.path + ": " + issue.message};
    }

    const std::string productLabel = productName(project);
    const std::string executable = executableName(project);
    const bool bundle = options.target == BuildTarget::MacOS;
    const fs::path output = options.destination / exportFolderName(project, options.target);
    const fs::path resources = bundle ? output / "Contents" / "Resources" : output;
    const fs::path programFolder = bundle ? output / "Contents" / "MacOS" : output;
    const std::string programFile =
        executable + (options.target == BuildTarget::Windows ? ".exe" : "");

    if (fs::exists(output, error)) {
        if (!options.overwrite)
            return Error{"'" + output.string() +
                         "' already exists; choose another folder or replace it explicitly"};
        if (!fs::is_regular_file(resources / "yk-export.json", error))
            return Error{"'" + output.string() +
                         "' exists but was not made by an export; remove it yourself"};
        fs::remove_all(output, error);
        if (error)
            return Error{"Cannot replace '" + output.string() + "': " + error.message()};
    }

    ExportReport report;
    report.output = output;
    report.executable = programFolder / programFile;
    report.dataFolder = resources / "data";
    // Anything that goes wrong from here on removes the half-written output again, and the
    // destination folder too when this export created it and nothing else is in it.
    const bool destinationExisted = fs::exists(options.destination, error);
    const auto fail = [&](const std::string &message) -> Result<ExportReport> {
        std::error_code ignored;
        fs::remove_all(output, ignored);
        if (!destinationExisted)
            fs::remove(options.destination, ignored); // Only succeeds on an empty folder.
        return Error{message};
    };

    say(options, std::string("Copying the ") + displayName(options.target) + " player");
    if (auto status = copyFile(options.player, report.executable); !status)
        return fail(status.error());
    fs::permissions(report.executable,
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, error);

    const std::vector<std::string> files = exportedFiles(project);
    say(options, "Copying " + std::to_string(files.size()) + " project files");
    for (const std::string &relative : files) {
        const fs::path from = project.root / fs::path(relative);
        if (auto status = copyFile(from, report.dataFolder / fs::path(relative)); !status)
            return fail(status.error());
        std::error_code sizeError;
        report.bytes += fs::file_size(from, sizeError);
        ++report.files;
    }

    say(options, "Writing the README and metadata");
    bool hasNotices = false;
    if (!options.notices.empty()) {
        std::vector<std::string> missing;
        for (const char *notice : runtimeNotices) {
            const fs::path from = options.notices / notice;
            if (!fs::is_regular_file(from, error)) {
                missing.push_back(notice);
                continue;
            }
            if (auto status = copyFile(from, resources / "licenses" / notice); !status)
                return fail(status.error());
            hasNotices = true;
        }
        if (!missing.empty())
            report.warnings.push_back("Some third-party notices were not found in " +
                                      options.notices.string() + " (" + missing.front() +
                                      (missing.size() > 1 ? ", ..." : "") + ")");
    } else {
        report.warnings.push_back(
            "No third-party notices were copied; the player contains SDL3, Box2D and stb_image, "
            "whose licenses require their notices to ship with it");
    }
    if (auto status = writeTextFileAtomic(
            resources / "README.txt", readmeFor(project, options.target, executable, hasNotices));
        !status)
        return fail(status.error());
    Json marker = Json::object();
    marker.set("tool", "yk-engine");
    marker.set("version", std::string(YK_VERSION));
    marker.set("target", name(options.target));
    marker.set("product", productLabel);
    marker.set("executable", programFile);
    marker.set("files", static_cast<std::int64_t>(report.files));
    if (auto status = writeTextFileAtomic(resources / "yk-export.json", marker.dump(2) + "\n");
        !status)
        return fail(status.error());
    if (bundle)
        if (auto status = writeTextFileAtomic(output / "Contents" / "Info.plist",
                                              infoPlist(project, executable));
            !status)
            return fail(status.error());

    // The copy has to stand on its own: load it as a project of its own and check it again, so
    // an exclude rule that removed a file the game needs shows up now, not on a player's machine.
    say(options, "Checking the exported game on its own");
    auto exported = Project::load(report.dataFolder);
    if (!exported)
        return fail("The exported project cannot be loaded: " + exported.error());
    const std::vector<ProjectIssue> after = validateProject(exported.value(), registry);
    for (const ProjectIssue &issue : after)
        if (issue.severity == ProjectIssue::Severity::Error)
            return fail("The exported game would be incomplete: " + issue.path + ": " +
                        issue.message + " (check Build > exclude in the project settings)");

    if (options.archive) {
        const fs::path zip =
            options.destination / (exportFolderName(project, options.target) + ".zip");
        say(options, "Writing " + zip.filename().string());
        std::vector<std::string> names;
        for (fs::recursive_directory_iterator it(output, error), end; !error && it != end;
             it.increment(error))
            if (it->is_regular_file(error))
                names.push_back(toPortablePath(fs::relative(it->path(), options.destination)));
        std::sort(names.begin(), names.end());
        std::vector<ZipEntry> entries;
        const std::string programName =
            toPortablePath(fs::relative(report.executable, options.destination));
        for (const std::string &entryName : names)
            entries.push_back(
                {entryName, options.destination / fs::path(entryName), entryName == programName});
        // The folder itself is fine at this point; only the archive failed.
        if (auto status = writeZip(zip, entries); !status)
            return Error{status.error()};
        report.archive = zip;
    }
    say(options, "Exported to " + output.string());
    return report;
}
} // namespace yk
