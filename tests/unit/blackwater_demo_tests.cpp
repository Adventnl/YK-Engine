#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Project.hpp"
#include "yk/assets/Validation.hpp"
#include "yk/components/Components.hpp"
#include "yk/gameplay/Exploration.hpp"
#include "yk/runtime/GameSession.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <cmath>

using namespace yk;

int main() {
    ComponentRegistry registry;
    registerStandardComponents(registry);
    auto projectResult = Project::load(YK_SIMULATION_PROJECT_DIR);
    CHECK(projectResult);
    if (!projectResult) return test::finish("blackwater_demo");
    auto project = projectResult.value();
    CHECK(!hasErrors(validateProject(project, registry)));
    ProjectAssets assets(project);
    auto load = [&](const std::string &file) -> Result<std::unique_ptr<Scene>> {
        const auto path = project.resolve(file);
        if (!path) return Error{path.error()};
        return loadScene(path.value(), registry);
    };
    const std::string scenePath = "scenes/blackwater.ykscene";
    auto scene = load(scenePath);
    CHECK(scene);
    if (!scene) return test::finish("blackwater_demo");
    RuntimeOptions options;
    options.layers = project.layers;
    options.inputMap = project.input;
    options.assets = &assets;
    auto created = GameSession::create(std::move(scene.value()), scenePath, options, load);
    CHECK(created);
    if (!created) return test::finish("blackwater_demo");
    auto session = std::move(created.value());
    Keyboard keyboard;
    auto tick = [&](int count = 1) {
        for (int i=0; i<count; ++i) {
            InputFrame frame;
            frame.keyboard = keyboard;
            session->update(1.0/60.0, frame);
            keyboard.beginFrame();
        }
    };
    auto press = [&](Key key) {
        keyboard.set(key,true); tick(); keyboard.set(key,false); tick();
    };
    auto object = [&](const char *name) { return session->runtime().scene().findByName(name); };
    auto player = [&]() { return object("Runner"); };
    auto teleport = [&](Vec2 at) { session->runtime().teleport(*player(),at); tick(3); };
    auto flag = [&](const char *name) { return session->runtime().blackboard().number(name); };
    auto closeDialogue = [&]() {
        for (int i=0;i<5;++i) {
            bool active = false;
            for (auto id : session->runtime().scene().orderedIds()) {
                auto *d = session->runtime().scene().find(id)->get<Dialogue>();
                if (d && d->active()) active = true;
            }
            if (!active) break;
            press(Key::E);
        }
    };
    auto use = [&](const char *name, Vec2 at) {
        CHECK(object(name)); teleport(at); press(Key::E); closeDialogue();
    };
    // Walk through real colliders: catches inaccessible doors, furniture and escape bypasses.
    auto walk = [&](Vec2 target) {
        for (int i=0; i<700; ++i) {
            const Vec2 at = player()->worldPosition();
            const Vec2 delta = target-at;
            if ((std::abs(delta.x)<.14F && std::abs(delta.y)<.14F) ||
                object("Blackwater escape rules")->get<LevelFlow>()->completed()) break;
            keyboard.set(Key::A,delta.x<-.12F); keyboard.set(Key::D,delta.x>.12F);
            keyboard.set(Key::W,delta.y<-.12F); keyboard.set(Key::S,delta.y>.12F);
            tick();
        }
        for (auto k : {Key::A,Key::D,Key::W,Key::S}) keyboard.set(k,false);
        tick(4);
        const Vec2 at=player()->worldPosition();
        const bool escaped=object("Blackwater escape rules")->get<LevelFlow>()->completed();
        if (!escaped && distance(at,target) >= .4F)
            std::fprintf(stderr,"Walk blocked: target %.2f,%.2f reached %.2f,%.2f\n",target.x,target.y,at.x,at.y);
        CHECK(escaped || distance(at,target)<.4F);
    };
    tick(75);
    CHECK(flag("yard_access")==0 && flag("power")==0);
    const Vec2 guardStart=object("Officer Vale")->worldPosition();
    tick(80);
    CHECK(distance(guardStart,object("Officer Vale")->worldPosition())>.5F);
    use("Security button",{41.5F,6}); CHECK(flag("yard_access")==0);
    teleport({3.7F,4.6F});
    walk({3.7F,8.5F}); walk({8.6F,8.5F}); walk({8.6F,14.1F});
    press(Key::E); closeDialogue(); CHECK(flag("code")==1);
    walk({13.5F,14.6F}); walk({18.5F,14.6F}); walk({18.5F,10.0F});
    // No fuse or tool is granted just by asking; leave the trade when empty-handed.
    use("Tess",{33.4F,8.2F}); CHECK(flag("fuse")==0);
    use("Rex",{19,21.2F}); CHECK(flag("tool")==0);
    use("Relay workbench",{18.5F,23.2F}); CHECK(flag("relay")==0);
    use("Maintenance breaker",{49.4F,16}); CHECK(flag("power")==0);
    use("Exit release button",{47.4F,22.7F}); CHECK(flag("exit_ready")==0);
    teleport({36.1F,14}); press(Key::E); tick(24);
    CHECK(!object("Yard gate")->get<StateGate>()->open());
    keyboard.set(Key::D,true); tick(55); keyboard.set(Key::D,false); tick(4);
    CHECK(player()->worldPosition().x<36.6F);
    teleport({49,23.6F}); press(Key::E); tick(24);
    CHECK(!object("Exit gate")->get<StateGate>()->open());
    keyboard.set(Key::S,true); tick(45); keyboard.set(Key::S,false); tick(4);
    CHECK(player()->worldPosition().y<24.4F);
    CHECK(!object("Blackwater escape rules")->get<LevelFlow>()->completed());
    // Pickups and actual dialogue choices consume the traded goods.
    use("Snack",{22.5F,5.2F}); CHECK(flag("snack")==1);
    use("Tess",{33.4F,8.2F}); CHECK(flag("fuse")==1 && flag("snack")==0);
    use("Tess",{33.4F,8.2F}); CHECK(flag("fuse")==1 && flag("snack")==0);
    use("Relay workbench",{18.5F,23.2F}); CHECK(flag("relay")==0); // Both parts required.
    use("Bandage",{33.2F,24.5F}); CHECK(flag("bandage")==1);
    use("Rex",{19,21.2F}); CHECK(flag("tool")==1 && flag("bandage")==0);
    use("Relay workbench",{18.5F,23.2F}); CHECK(flag("relay")==1);
    // All rooms' openings are usable. Supplies are accessible from the hall.
    teleport({31,14.6F}); walk({31,9.5F}); walk({31,14.6F}); walk({31,18.7F});
    walk({31,24.5F}); walk({31,18.7F}); walk({31,14.6F});
    walk({31,9}); walk({31,7}); walk({36,7}); walk({38.5F,7}); walk({41.5F,6});
    press(Key::E); closeDialogue(); CHECK(flag("yard_access")==1);
    walk({38.5F,7}); walk({36,7}); walk({31,7}); walk({31,14.6F}); walk({36,14.6F});
    press(Key::E); tick(25);
    CHECK(object("Yard gate")->get<StateGate>()->open());
    walk({38.2F,14.6F}); walk({38.2F,13.8F}); walk({44.5F,13.8F}); walk({44.5F,14.5F});
    walk({47.5F,14.5F}); walk({47.5F,16}); walk({49.4F,16});
    press(Key::E); tick(4); CHECK(flag("power")==1);
    walk({49,20}); walk({49,22.5F}); walk({47.4F,22.5F});
    press(Key::E); tick(4); CHECK(flag("exit_ready")==1);
    walk({49,23.7F}); press(Key::E); tick(25);
    CHECK(object("Exit gate")->get<StateGate>()->open());
    walk({49,26.0F}); tick(60);
    CHECK(object("Blackwater escape rules")->get<LevelFlow>()->completed());
    CHECK(session->runtime().blackboard().text("level_message")=="ESCAPED FROM BLACKWATER!");
    press(Key::R); tick(80);
    CHECK(flag("code")==0 && flag("fuse")==0 && flag("tool")==0 && flag("relay")==0);
    CHECK(flag("yard_access")==0 && flag("power")==0 && flag("exit_ready")==0);
    CHECK(!object("Yard gate")->get<StateGate>()->open());
    CHECK(!object("Exit gate")->get<StateGate>()->open());
    CHECK(object("Snack")->active() && object("Bandage")->active());
    return test::finish("blackwater_demo");
}
