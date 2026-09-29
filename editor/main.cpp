// yk_editor: the YK Engine editor.
#include "Modules.hpp"
#include "ui/EditorApp.hpp"
#include "ui/EditorDriver.hpp"
#include "yk/core/Application.hpp"
#include "yk/core/Log.hpp"
#include "yk/scene/RegistryDocs.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace yk;
using namespace yk::editor;

namespace {
struct Options {
    EditorOptions editor;
    unsigned frames{};
    std::filesystem::path capture;
    std::filesystem::path script;
    std::filesystem::path failureDirectory;
    int width{1600};
    int height{900};
    bool help{};
};

void usage() {
    std::puts("usage: yk_editor [options] [project]\n"
              "  project                project folder or project.ykproj to open\n"
              "  --scene <path>         project-relative scene to open (default: the start scene)\n"
              "  --size <WxH>           window size (default 1600x900)\n"
              "  --settings-dir <dir>   where recent projects and the window layout are kept\n"
              "  --fresh-layout         ignore and do not save the window layout\n"
              "  --no-audio             disable sound\n"
              "Automation (used by the tests):\n"
              "  --script <file>        drive the UI from a script (see ui/EditorDriver.hpp); exit "
              "code 1 if\n"
              "                         any expectation fails\n"
              "  --test-hooks           record widget positions so scripts can find them (implied "
              "by --script)\n"
              "  --failure-dir <dir>    save a screenshot for each failed expectation\n"
              "  --frames <n>           exit after n frames\n"
              "  --capture <file.bmp>   save the last frame (needs --frames)");
}

std::optional<Options> parse(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> const char * { return i + 1 < argc ? argv[++i] : nullptr; };
        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--no-audio") {
            options.editor.audio = false;
        } else if (arg == "--fresh-layout") {
            options.editor.persistLayout = false;
        } else if (arg == "--test-hooks") {
            options.editor.testHooks = true;
        } else if (arg == "--scene" || arg == "--size" || arg == "--settings-dir" ||
                   arg == "--script" || arg == "--failure-dir" || arg == "--frames" ||
                   arg == "--capture") {
            const char *text = value();
            if (!text) {
                std::fprintf(stderr, "%s needs a value\n", arg.c_str());
                return std::nullopt;
            }
            if (arg == "--scene") {
                options.editor.scene = text;
            } else if (arg == "--size") {
                if (std::sscanf(text, "%dx%d", &options.width, &options.height) != 2 ||
                    options.width < 320 || options.height < 240) {
                    std::fprintf(stderr, "--size wants WIDTHxHEIGHT, at least 320x240\n");
                    return std::nullopt;
                }
            } else if (arg == "--settings-dir") {
                options.editor.settingsDirectory = text;
            } else if (arg == "--script") {
                options.script = text;
            } else if (arg == "--failure-dir") {
                options.failureDirectory = text;
            } else if (arg == "--frames") {
                options.frames = static_cast<unsigned>(std::strtoul(text, nullptr, 10));
            } else {
                options.capture = text;
            }
        } else if (!arg.empty() && arg[0] != '-' && options.editor.project.empty()) {
            options.editor.project = arg;
        } else {
            std::fprintf(stderr, "Unknown argument '%s'\n", arg.c_str());
            return std::nullopt;
        }
    }
    return options;
}
} // namespace

int main(int argc, char **argv) {
    auto options = parse(argc, argv);
    if (!options) {
        usage();
        return 2;
    }
    if (options->help) {
        usage();
        return 0;
    }
    if (!options->capture.empty() && options->frames == 0) {
        std::fprintf(stderr, "--capture needs --frames\n");
        return 2;
    }
    ComponentRegistry registry;
    registerAllModules(registry);
    if (auto valid = registry.validate(); !valid) {
        std::fprintf(stderr, "Component registry is inconsistent: %s\n", valid.error().c_str());
        return 1;
    }
    std::unique_ptr<EditorDriver> driver;
    if (!options->script.empty()) {
        auto loaded = EditorDriver::load(options->script);
        if (!loaded) {
            std::fprintf(stderr, "%s\n", loaded.error().c_str());
            return 2;
        }
        driver = std::move(loaded.value());
        driver->setFailureDirectory(options->failureDirectory);
        options->editor.testHooks = true;
    }

    ApplicationConfig config;
    config.title = "YK Editor";
    config.width = options->width;
    config.height = options->height;
    config.logicalWidth = 0; // Native resolution: the editor lays out in real pixels.
    config.logicalHeight = 0;
    auto app = Application::create(config);
    if (!app) {
        std::fprintf(stderr, "%s\n", app.error().c_str());
        return 1;
    }
    EditorApp editor(registry, options->editor, app.value()->nativeWindow());
    if (driver)
        editor.setFrameHook([&driver](EditorApp &self) { driver->step(self); });
    RunOptions run;
    run.frameLimit = options->frames;
    if (!options->capture.empty())
        run.captureLastFrame = options->capture;
    const auto result = app.value()->run(editor, run);
    if (!result) {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    if (driver) {
        if (!driver->finished()) {
            std::fprintf(stderr, "The script did not finish (stopped at a later command)\n");
            return 1;
        }
        if (driver->failures() != 0) {
            std::fprintf(stderr, "%d expectation(s) failed\n", driver->failures());
            return 1;
        }
    }
    return 0;
}
