// yk_player: runs a project's scene as a game (yk::host::runPlayer; player/main.cpp is one line).
#include "KeyScript.hpp"
#include "yk/assets/Export.hpp"
#include "yk/audio/SdlAudio.hpp"
#include "yk/core/AppPaths.hpp"
#include "yk/core/Application.hpp"
#include "yk/core/Diagnostics.hpp"
#include "yk/core/Log.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/graphics/GameView.hpp"
#include "yk/host/Hosts.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace yk;

namespace {
struct Options {
    std::filesystem::path project;
    std::string scene;
    unsigned frames{};
    std::filesystem::path capture;
    std::string keys;
    std::string debugCrash; // Test hook: crash after a few frames ("abort", "segv", "throw").
    bool fixedStep{};
    bool audio{true};
    bool help{};
};

void usage() {
    std::puts(
        "usage: yk_player [options] [project]\n"
        "  --project <dir|file>   project to run (default: ./data beside the executable, else "
        "here)\n"
        "  --scene <path>         project-relative scene to start with (default: the start scene)\n"
        "  --frames <n>           exit after n frames\n"
        "  --capture <file.bmp>   save the last frame (needs --frames)\n"
        "  --keys <script>        replay keys instead of the keyboard, e.g. D@0-120,W@60-62\n"
        "  --fixed                advance a fixed 1/60 s per frame (deterministic)\n"
        "  --no-audio             disable sound\n"
        "  --debug-crash <kind>   crash on purpose after a few frames (abort, segv or throw) to "
        "test\n"
        "                         crash reports\n"
        "In game: F1 physics outlines, F2 collider outlines, F3 stats, Esc quits.");
}

std::optional<Options> parse(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> const char * { return i + 1 < argc ? argv[++i] : nullptr; };
        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--project" || arg == "--scene" || arg == "--frames" ||
                   arg == "--capture" || arg == "--keys" || arg == "--debug-crash") {
            const char *text = value();
            if (!text) {
                std::fprintf(stderr, "%s needs a value\n", arg.c_str());
                return std::nullopt;
            }
            if (arg == "--project")
                options.project = text;
            else if (arg == "--scene")
                options.scene = text;
            else if (arg == "--frames")
                options.frames = static_cast<unsigned>(std::strtoul(text, nullptr, 10));
            else if (arg == "--capture")
                options.capture = text;
            else if (arg == "--debug-crash")
                options.debugCrash = text;
            else
                options.keys = text;
        } else if (arg == "--fixed") {
            options.fixedStep = true;
        } else if (arg == "--no-audio") {
            options.audio = false;
        } else if (!arg.empty() && arg[0] != '-' && options.project.empty()) {
            options.project = arg;
        } else {
            std::fprintf(stderr, "Unknown argument '%s'\n", arg.c_str());
            return std::nullopt;
        }
    }
    return options;
}

// A game started by double-click has no console: the error goes to the log, stderr and a message
// box (yk::showFatalError).
int fatal(const std::string &message) {
    showFatalError("The game cannot start", message);
    return 1;
}

std::filesystem::path locateProject(const Options &options) {
    if (!options.project.empty())
        return options.project;
    // An exported game keeps its project in data/ beside the program, or in Contents/Resources of a
    // macOS bundle (the program is in Contents/MacOS); project/ is the older name.
    std::vector<std::filesystem::path> homes;
    if (const auto resources = bundleResourcesDirectory(); !resources.empty())
        homes.push_back(resources);
    homes.push_back(executableDirectory());
    for (const auto &home : homes) {
        for (const char *folder : {"data", "project"}) {
            const std::filesystem::path beside = home / folder;
            if (std::filesystem::exists(beside / Project::fileName))
                return beside;
        }
    }
    return std::filesystem::current_path();
}

class PlayerLayer final : public ApplicationLayer {
  public:
    PlayerLayer(const Project &project, const ComponentRegistry &registry, const Options &options)
        : project_(project), registry_(registry), options_(options), assets_(project) {}

    Status initialize(Renderer &renderer) override {
        auto sceneRenderer = SceneRenderer::create(renderer, &assets_, project_.textures);
        if (!sceneRenderer)
            return Error{sceneRenderer.error()};
        sceneRenderer_ = std::move(sceneRenderer.value());
        if (options_.audio)
            audio_ = SdlAudio::create(&assets_);
        if (!options_.keys.empty()) {
            auto script = KeyScript::parse(options_.keys);
            if (!script)
                return Error{script.error()};
            script_ = std::move(script.value());
        }
        return load(options_.scene.empty() ? project_.startScene : options_.scene, renderer);
    }
    bool update(const FrameContext &frame) override {
        if (frame.input.keyboard.state(Key::Escape).pressed)
            return false;
        InputFrame input = frame.input;
        if (!script_.empty())
            input.keyboard = script_.at(frameIndex_);
        if (input.keyboard.state(Key::F1).pressed)
            physicsDebug_ = !physicsDebug_;
        if (input.keyboard.state(Key::F2).pressed)
            colliders_ = !colliders_;
        if (input.keyboard.state(Key::F3).pressed)
            stats_ = !stats_;
        const double seconds =
            options_.fixedStep ? 1.0 / 60.0 : static_cast<double>(frame.delta.seconds);
        runtime_->update(seconds, input);
        ++frameIndex_;
        if (!options_.debugCrash.empty() && frameIndex_ == 5 &&
            !crashOnPurpose(options_.debugCrash))
            log(LogLevel::Error, "player",
                "Unknown --debug-crash kind '" + options_.debugCrash + "'");
        if (audio_)
            audio_->update();
        if (const std::string next = runtime_->sceneChangeRequested(); !next.empty()) {
            runtime_->clearSceneChangeRequest();
            pendingScene_ = next;
        }
        return true;
    }
    Status render(Renderer &renderer) override {
        if (!pendingScene_.empty()) {
            const std::string next = std::move(pendingScene_);
            pendingScene_.clear();
            if (auto status = load(next, renderer); !status)
                log(LogLevel::Error, "player", status.error()); // Keep playing the current scene.
        }
        GameViewOptions view;
        view.physicsDebug = physicsDebug_;
        view.colliders = colliders_;
        if (stats_)
            view.overlay = "TICK " + std::to_string(runtime_->tick()) + "  BODIES " +
                           std::to_string(runtime_->physics().stats().bodies);
        return drawGameView(renderer, *sceneRenderer_, *runtime_, {{0, 0}, renderer.viewport()},
                            view);
    }

  private:
    Status load(const std::string &sceneFile, Renderer &renderer) {
        auto path = project_.resolve(sceneFile);
        if (!path)
            return Error{path.error()};
        auto scene = loadScene(path.value(), registry_);
        if (!scene)
            return Error{scene.error()};
        RuntimeOptions runtimeOptions;
        runtimeOptions.layers = project_.layers;
        runtimeOptions.inputMap = project_.input;
        runtimeOptions.viewportSize = renderer.viewport();
        runtimeOptions.audio = audio_.get();
        runtimeOptions.assets = &assets_;
        auto runtime = GameRuntime::create(std::move(scene.value()), runtimeOptions);
        if (!runtime)
            return Error{runtime.error()};
        runtime_ = std::move(runtime.value());
        frameIndex_ = 0;
        log(LogLevel::Info, "player", "Loaded scene " + sceneFile);
        return success();
    }

    const Project &project_;
    const ComponentRegistry &registry_;
    Options options_;
    ProjectAssets assets_;
    std::unique_ptr<SceneRenderer> sceneRenderer_;
    std::unique_ptr<SdlAudio> audio_;
    std::unique_ptr<GameRuntime> runtime_;
    KeyScript script_;
    std::string pendingScene_;
    unsigned frameIndex_{};
    bool physicsDebug_{};
    bool colliders_{};
    bool stats_{};
};
} // namespace

namespace yk::host {
int runPlayer(int argc, char **argv, const RegisterComponents &registerGame) {
    const auto options = parse(argc, argv);
    if (!options) {
        usage();
        return 2;
    }
    if (options->help) {
        usage();
        return 0;
    }
    ComponentRegistry registry;
    registerStandardComponents(registry);
    if (registerGame)
        registerGame(registry);
    if (auto valid = registry.validate(); !valid) {
        std::fprintf(stderr, "Component registry is inconsistent: %s\n", valid.error().c_str());
        return 1;
    }
    auto project = Project::load(locateProject(*options));
    // The log and crash reports go where the system keeps them, named after the game, so a player
    // who double-clicks a game has something to send when it misbehaves.
    SessionInfo session;
    session.application = project ? productName(project.value()) : std::string("YK Player");
    session.version = project ? project.value().build.version : std::string(engineVersion());
    session.logName = "player";
    session.logDirectory =
        userDirectories("", project ? executableName(project.value()) : std::string("YKPlayer"))
            .logs;
    const DiagnosticsSession diagnostics(session);
    if (!project) {
        if (options->project.empty())
            std::fprintf(stderr,
                         "Tell the player which game to run: yk_player <project folder> (an "
                         "exported game keeps its data/ folder next to the program).\n");
        return fatal(project.error());
    }
    if (!options->capture.empty() && options->frames == 0) {
        std::fprintf(stderr, "--capture needs --frames\n");
        return 2;
    }
    const Project &loaded = project.value();
    ApplicationConfig config;
    config.title = loaded.window.title;
    config.width = loaded.window.width;
    config.height = loaded.window.height;
    config.logicalWidth = loaded.window.width;
    config.logicalHeight = loaded.window.height;
    auto app = Application::create(config);
    if (!app)
        return fatal(app.error());
    PlayerLayer layer(loaded, registry, *options);
    RunOptions run;
    run.frameLimit = options->frames;
    if (!options->capture.empty())
        run.captureLastFrame = options->capture;
    const auto result = app.value()->run(layer, run);
    if (!result)
        return fatal(result.error());
    return 0;
}
} // namespace yk::host
