// The example game module (Spinner): a component that is not in the engine behaves like any other
// in every tool once its game registers it, and the stock tools reject a project that needs it.
#include "SpinnerModule.hpp"
#include "support/check.hpp"
#include "yk/assets/Validation.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/host/Hosts.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <cstring>
#include <filesystem>

using namespace yk;

namespace {
ComponentRegistry gameRegistry() {
    ComponentRegistry registry;
    registerStandardComponents(registry);
    spinner::registerComponents(registry);
    return registry;
}

const std::filesystem::path projectFolder = SPINNER_PROJECT;

void registration() {
    const ComponentRegistry registry = gameRegistry();
    CHECK(registry.validate());
    const ComponentType *type = registry.find("Spinner");
    CHECK(type != nullptr && type->category == "Example" && !type->description.empty());
    CHECK(type && type->find("degreesPerSecond") && type->find("clockwise"));
    // The engine's own components are still there.
    CHECK(registry.find("SpriteRenderer") && registry.find("PlatformerController"));

    ComponentRegistry stock;
    registerStandardComponents(stock);
    CHECK(stock.find("Spinner") == nullptr);
}

void projectChecks() {
    auto project = Project::load(projectFolder);
    CHECK(project && project.value().name == "Spinner Test");
    if (!project)
        return;
    // With the module registered the project is clean...
    CHECK(!hasErrors(validateProject(project.value(), gameRegistry())));
    // ...and the stock tools refuse it, naming the component they do not know.
    ComponentRegistry stock;
    registerStandardComponents(stock);
    const auto issues = validateProject(project.value(), stock);
    CHECK(hasErrors(issues));
    bool named = false;
    for (const ProjectIssue &issue : issues)
        named = named || issue.message.find("Spinner") != std::string::npos;
    CHECK(named);
}

void running() {
    const ComponentRegistry registry = gameRegistry();
    auto project = Project::load(projectFolder);
    if (!project)
        return;
    auto scene = loadScene(project.value().resolve(project.value().startScene).value(), registry);
    CHECK(scene);
    if (!scene)
        return;
    auto runtime = GameRuntime::create(std::move(scene.value()));
    CHECK(runtime);
    if (!runtime)
        return;
    for (int tick = 0; tick < 60; ++tick)
        runtime.value()->update(1.0 / 60.0, InputFrame{});
    const Entity *box = runtime.value()->scene().findByName("Box");
    CHECK(box != nullptr);
    if (box)
        CHECK_NEAR(box->transform().rotationDegrees, 90.0,
                   1.0); // One second at 90 degrees per second.

    // Saving and loading keep the component and its settings, like any other.
    auto again = loadScene(project.value().resolve(project.value().startScene).value(), registry);
    if (again) {
        Entity *entity = again.value()->findByName("Box");
        auto *spinner = entity ? entity->get<spinner::Spinner>() : nullptr;
        CHECK(spinner != nullptr);
        if (spinner) {
            spinner->degreesPerSecond = -45.0F;
            const auto reloaded = sceneFromJson(sceneToJson(*again.value()), registry);
            CHECK(reloaded &&
                  reloaded.value()->findByName("Box")->get<spinner::Spinner>()->degreesPerSecond ==
                      -45.0F);
        }
    }
}

#if YK_HAS_RUNTIME
// The stock player program with the module registered runs the project (a project with a
// component the registry does not know would fail to load).
void playerHost() {
    const std::string project = projectFolder.string();
    std::vector<char *> arguments;
    std::vector<std::string> words = {"spinner_player", "--project", project, "--frames", "10",
                                      "--fixed",        "--no-audio"};
    for (std::string &word : words)
        arguments.push_back(word.data());
    CHECK(host::runPlayer(static_cast<int>(arguments.size()), arguments.data(),
                          spinner::registerComponents) == 0);
}
#endif
} // namespace

int main() {
    registration();
    projectChecks();
    running();
#if YK_HAS_RUNTIME
    playerHost();
#endif
    return yk::test::finish("game_module");
}
