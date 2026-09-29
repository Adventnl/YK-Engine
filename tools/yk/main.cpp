// yk: command-line tools for YK projects. Headless (no window, no audio): meant for developers,
// build scripts and CI.
//
//   yk validate [project]          check every scene, prefab and asset (exit 1 on errors)
//   yk format   [project] [--check]  rewrite scenes, prefabs, animations and the project file the
//                                  way the editor saves them; --check only reports (exit 1)
//   yk info     [project]          summarize a project
//   yk components                  print the component reference (Markdown)
//   yk targets                     which systems a game can be exported for from here
//   yk export [project] --target windows|macos|linux --out <folder> [--player <file>] [--zip]
//                                  package the game: the player, the data and the notices
//
// `project` is a directory or a project.ykproj file; it defaults to the current directory.
#include "yk/animation/AnimationController.hpp"
#include "yk/animation/AnimationSet.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Export.hpp"
#include "yk/assets/Validation.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/scene/RegistryDocs.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace yk;

namespace {
void usage() {
    std::puts(
        "usage: yk <command> [options] [project]\n"
        "  validate            check every scene, prefab and asset; exit 1 on errors\n"
        "  format [--check]    write scenes, prefabs, animations and the project file in the\n"
        "                      editor's canonical form (--check: only list what would change)\n"
        "  info                summarize the project\n"
        "  components          print the component reference (Markdown)\n"
        "  targets             list the systems a game can be exported for from here\n"
        "  export              package the game for a system:\n"
        "      --target <windows|macos|linux>   (default: this system)\n"
        "      --out <folder>                   where the game folder or bundle is created\n"
        "      --player <file>                  the player built for the target (default: found\n"
        "                                       beside yk or in templates/<target>/)\n"
        "      --zip                            also write a .zip next to it\n"
        "      --force                          replace an earlier export\n"
        "project: a directory or project.ykproj (default: the current directory)");
}

struct Args {
    std::string command;
    std::filesystem::path project;
    bool check{};
    bool strict{};
    // export
    std::string target;
    std::filesystem::path out, player;
    bool zip{};
    bool force{};
};

std::optional<Args> parse(int argc, char **argv) {
    if (argc < 2)
        return std::nullopt;
    Args args;
    args.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> const char * { return i + 1 < argc ? argv[++i] : nullptr; };
        if (arg == "--check") {
            args.check = true;
        } else if (arg == "--strict") {
            args.strict = true;
        } else if (arg == "--zip") {
            args.zip = true;
        } else if (arg == "--force") {
            args.force = true;
        } else if (arg == "--target" || arg == "--out" || arg == "--player") {
            const char *text = value();
            if (!text) {
                std::fprintf(stderr, "yk: %s needs a value\n", arg.c_str());
                return std::nullopt;
            }
            if (arg == "--target")
                args.target = text;
            else if (arg == "--out")
                args.out = text;
            else
                args.player = text;
        } else if (!arg.empty() && arg[0] != '-' && args.project.empty()) {
            args.project = arg;
        } else {
            std::fprintf(stderr, "yk: unknown argument '%s'\n", arg.c_str());
            return std::nullopt;
        }
    }
    if (args.project.empty())
        args.project = std::filesystem::current_path();
    return args;
}

// The folder this program runs from, to find the player and the notices next to it.
std::filesystem::path executableDirectory(const char *argv0) {
    std::error_code error;
#if defined(__linux__)
    const auto self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error && !self.empty())
        return self.parent_path();
#endif
    const auto resolved = std::filesystem::weakly_canonical(argv0, error);
    return error ? std::filesystem::current_path() : resolved.parent_path();
}

std::optional<Project> openProject(const Args &args) {
    auto project = Project::load(args.project);
    if (!project) {
        std::fprintf(stderr, "yk: %s\n", project.error().c_str());
        return std::nullopt;
    }
    return std::move(project.value());
}

int validate(const Args &args) {
    const auto project = openProject(args);
    if (!project)
        return 2;
    ComponentRegistry registry;
    registerStandardComponents(registry);
    const auto issues = validateProject(*project, registry);
    std::size_t errors = 0, warnings = 0;
    for (const ProjectIssue &issue : issues) {
        const bool error = issue.severity == ProjectIssue::Severity::Error;
        (error ? errors : warnings)++;
        std::printf("%s: %s: %s\n", error ? "error" : "warning", issue.path.c_str(),
                    issue.message.c_str());
    }
    std::printf("%s: %zu error(s), %zu warning(s)\n", project->name.c_str(), errors, warnings);
    return errors > 0 || (args.strict && warnings > 0) ? 1 : 0;
}

// Text a canonical writer would produce for the JSON file at `path`, or an error.
Result<std::string> canonicalText(const Project &project, const ComponentRegistry &registry,
                                  const AssetEntry &entry, const std::string &text) {
    auto document = Json::parse(text);
    if (!document)
        return Error{document.error()};
    switch (entry.kind) {
    case AssetKind::Scene: {
        auto canonical = canonicalScene(document.value(), registry);
        if (!canonical)
            return Error{canonical.error()};
        return canonical.value().dump(2) + "\n";
    }
    case AssetKind::Prefab: {
        auto canonical = canonicalPrefab(document.value(), registry);
        if (!canonical)
            return Error{canonical.error()};
        return canonical.value().dump(2) + "\n";
    }
    case AssetKind::Animation: {
        if (auto set = parseAnimationSet(document.value()); !set)
            return Error{set.error()};
        return document.value().dump(2) + "\n";
    }
    case AssetKind::Controller: {
        if (auto controller = AnimationController::fromJson(document.value()); !controller)
            return Error{controller.error()};
        return document.value().dump(2) + "\n";
    }
    case AssetKind::TextureMeta: {
        if (auto meta = TextureMeta::fromJson(document.value()); !meta)
            return Error{meta.error()};
        return document.value().dump(2) + "\n";
    }
    default:
        (void)project;
        return text;
    }
}

int format(const Args &args) {
    const auto project = openProject(args);
    if (!project)
        return 2;
    ComponentRegistry registry;
    registerStandardComponents(registry);
    std::size_t changed = 0, failed = 0, checked = 0;
    const auto handle =
        [&](const std::string &name, const std::filesystem::path &file,
            const std::function<Result<std::string>(const std::string &)> &rewrite) {
            auto text = readTextFile(file);
            if (!text) {
                std::fprintf(stderr, "error: %s: %s\n", name.c_str(), text.error().c_str());
                ++failed;
                return;
            }
            ++checked;
            auto formatted = rewrite(text.value());
            if (!formatted) {
                std::fprintf(stderr, "error: %s: %s\n", name.c_str(), formatted.error().c_str());
                ++failed;
                return;
            }
            if (formatted.value() == text.value())
                return;
            ++changed;
            std::printf("%s %s\n", args.check ? "would format" : "formatted", name.c_str());
            if (!args.check)
                if (auto written = writeTextFileAtomic(file, formatted.value()); !written) {
                    std::fprintf(stderr, "error: %s: %s\n", name.c_str(), written.error().c_str());
                    ++failed;
                }
        };
    for (const AssetEntry &entry : scanAssets(*project)) {
        if (entry.kind == AssetKind::Other || entry.kind == AssetKind::Texture ||
            entry.kind == AssetKind::Sound)
            continue;
        const auto absolute = project->resolve(entry.path);
        if (!absolute)
            continue;
        handle(entry.path, absolute.value(), [&](const std::string &text) {
            return canonicalText(*project, registry, entry, text);
        });
    }
    // The project file: load and save through the same code the editor uses.
    handle(Project::fileName, project->file(), [&](const std::string &) -> Result<std::string> {
        return project->toJson().dump(2) + "\n";
    });
    std::printf("%zu file(s) checked, %zu %s, %zu failed\n", checked, changed,
                args.check ? "would change" : "changed", failed);
    return failed > 0 || (args.check && changed > 0) ? 1 : 0;
}

int info(const Args &args) {
    const auto project = openProject(args);
    if (!project)
        return 2;
    ComponentRegistry registry;
    registerStandardComponents(registry);
    std::map<std::string, std::size_t> counts;
    std::vector<std::string> scenes;
    for (const AssetEntry &entry : scanAssets(*project)) {
        static const char *names[] = {"scene",     "prefab",     "texture",      "sound",
                                      "animation", "controller", "texture meta", "other"};
        ++counts[names[static_cast<int>(entry.kind)]];
        if (entry.kind == AssetKind::Scene)
            scenes.push_back(entry.path);
    }
    std::printf("%s\n  root:        %s\n  start scene: %s\n  window:      %s, %dx%d\n",
                project->name.c_str(), project->root.string().c_str(),
                project->startScene.empty() ? "(none)" : project->startScene.c_str(),
                project->window.title.c_str(), project->window.width, project->window.height);
    std::printf("  layers:      ");
    for (std::size_t i = 0; i < project->layers.size(); ++i)
        std::printf("%s%s", i ? ", " : "", project->layers.names[i].c_str());
    std::printf("\n  input sets:  ");
    for (std::size_t i = 0; i < project->input.sets.size(); ++i)
        std::printf("%s%s", i ? ", " : "", project->input.sets[i].name.c_str());
    std::printf("\n  assets:      ");
    bool first = true;
    for (const auto &[kind, count] : counts) {
        std::printf("%s%zu %s%s", first ? "" : ", ", count, kind.c_str(), count == 1 ? "" : "s");
        first = false;
    }
    std::printf("\n");
    for (const std::string &path : scenes) {
        auto scene = loadScene(project->resolve(path).value(), registry);
        if (scene)
            std::printf("  scene %-32s %zu entities\n", path.c_str(), scene.value()->size());
        else
            std::printf("  scene %-32s does not load: %s\n", path.c_str(), scene.error().c_str());
    }
    return 0;
}
int targets(const std::filesystem::path &here) {
    for (const BuildTarget target : allBuildTargets) {
        const auto player = findPlayer(target, here);
        std::printf("%-8s %s\n", name(target),
                    player ? player->string().c_str()
                           : "no player program found (see docs/BUILDING.md)");
    }
    if (const auto notices = findNotices(here))
        std::printf("notices  %s\n", notices->string().c_str());
    else
        std::printf("notices  not found (exported games will carry none)\n");
    return 0;
}

int exportProject(const Args &args, const std::filesystem::path &here) {
    const auto project = openProject(args);
    if (!project)
        return 2;
    ExportOptions options;
    if (!args.target.empty()) {
        const auto target = buildTargetFromName(args.target);
        if (!target) {
            std::fprintf(stderr, "yk: unknown target '%s' (windows, macos or linux)\n",
                         args.target.c_str());
            return 2;
        }
        options.target = *target;
    }
    if (args.out.empty()) {
        std::fprintf(stderr, "yk export: --out <folder> is required\n");
        return 2;
    }
    options.destination = args.out;
    if (!args.player.empty()) {
        options.player = args.player;
    } else if (const auto found = findPlayer(options.target, here)) {
        options.player = *found;
    } else {
        std::fprintf(stderr,
                     "yk export: no %s player program found. Build yk_player for that system and "
                     "pass --player <file>, or put it in templates/%s/ beside yk.\n",
                     displayName(options.target), name(options.target));
        return 1;
    }
    if (const auto notices = findNotices(here))
        options.notices = *notices;
    options.archive = args.zip;
    options.overwrite = args.force;
    options.progress = [](const std::string &line) { std::printf("  %s\n", line.c_str()); };
    ComponentRegistry registry;
    registerStandardComponents(registry);
    std::printf("Exporting %s for %s\n", productName(*project).c_str(),
                displayName(options.target));
    const auto report = exportGame(*project, registry, options);
    if (!report) {
        std::fprintf(stderr, "yk export: %s\n", report.error().c_str());
        return 1;
    }
    for (const std::string &warning : report.value().warnings)
        std::printf("warning: %s\n", warning.c_str());
    std::printf("%zu file(s), %.1f MB\n  program: %s\n  data:    %s\n", report.value().files,
                static_cast<double>(report.value().bytes) / 1048576.0,
                report.value().executable.string().c_str(),
                report.value().dataFolder.string().c_str());
    if (!report.value().archive.empty())
        std::printf("  archive: %s\n", report.value().archive.string().c_str());
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    const auto args = parse(argc, argv);
    if (!args || args->command == "help" || args->command == "--help" || args->command == "-h") {
        usage();
        return args ? 0 : 2;
    }
    if (args->command == "components") {
        ComponentRegistry registry;
        registerStandardComponents(registry);
        std::fputs(describeRegistryMarkdown(registry).c_str(), stdout);
        return 0;
    }
    if (args->command == "validate")
        return validate(*args);
    if (args->command == "format")
        return format(*args);
    if (args->command == "info")
        return info(*args);
    if (args->command == "targets")
        return targets(executableDirectory(argv[0]));
    if (args->command == "export")
        return exportProject(*args, executableDirectory(argv[0]));
    std::fprintf(stderr, "yk: unknown command '%s'\n\n", args->command.c_str());
    usage();
    return 2;
}
