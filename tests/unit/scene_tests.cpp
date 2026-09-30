#include "support/check.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <filesystem>
#include <limits>
#include <stdexcept>

using namespace yk;

namespace {
enum class Mode { Alpha, Beta, Gamma };

struct Widget final : Component {
    bool flag{true};
    int count{3};
    float speed{1.5F};
    std::string label{"widget"};
    Vec2 size{2, 1};
    Color tint{1, 2, 3, 4};
    Mode mode{Mode::Beta};
    EntityRef target;
    std::vector<EntityRef> targets;
    std::vector<std::string> names{"a"};
    AssetRef sound;
    int runtimeOnly{7};
    static void describe(TypeBuilder<Widget> &t) {
        t.category("Test").description("Every field kind");
        t.field("flag", &Widget::flag);
        t.field("count", &Widget::count).range(0, 10);
        t.field("speed", &Widget::speed).range(0, 5, 0.1).tooltip("units per second");
        t.field("label", &Widget::label);
        t.field("size", &Widget::size).range(0.1, 100).size();
        t.field("tint", &Widget::tint);
        t.field("mode", &Widget::mode).options({"Alpha", "Beta", "Gamma"});
        t.field("target", &Widget::target);
        t.field("targets", &Widget::targets);
        t.field("names", &Widget::names);
        t.field("sound", &Widget::sound).asset("sound");
        t.field("runtimeOnly", &Widget::runtimeOnly).readOnly();
    }
};
struct Pingable {
    virtual ~Pingable() = default;
    virtual int ping() const = 0;
};
struct Follower final : Component, Pingable {
    float offset{0.25F};
    int ping() const override {
        return 42;
    }
    static void describe(TypeBuilder<Follower> &t) {
        t.category("Test").dependsOn("Widget");
        t.field("offset", &Follower::offset);
    }
};
struct Stackable final : Component {
    int level{1};
    static void describe(TypeBuilder<Stackable> &t) {
        t.allowMultiple();
        t.field("level", &Stackable::level);
    }
};

ComponentRegistry makeRegistry() {
    ComponentRegistry registry;
    registry.add<Widget>("Widget");
    registry.add<Follower>("Follower");
    registry.add<Stackable>("Stackable");
    return registry;
}

void entityIds() {
    const EntityId id{0x0123456789abcdefULL};
    CHECK(toString(id) == "0123456789abcdef");
    CHECK(parseEntityId("0123456789abcdef").value() == id);
    CHECK(toString(EntityId{}).empty() && !*parseEntityId("") && !EntityId{});
    CHECK(!parseEntityId("123") && !parseEntityId("0123456789ABCDEF") &&
          !parseEntityId("zzzzzzzzzzzzzzzz") && !parseEntityId("0000000000000000"));
    CHECK(toString(EntityId{1}) == "0000000000000001" &&
          parseEntityId(toString(EntityId{1})).value() == EntityId{1});
}

void registration() {
    auto registry = makeRegistry();
    CHECK(registry.find("Widget") && registry.find<Widget>() == registry.find("Widget"));
    CHECK(!registry.find("Nope") && registry.types().size() == 3);
    CHECK(registry.validate());
    bool threw = false;
    try {
        registry.add<Widget>("Widget");
    } catch (const std::logic_error &) {
        threw = true;
    }
    CHECK(threw); // Duplicate registration is a programming error.
    struct BadEnum final : Component {
        Mode mode{};
        static void describe(TypeBuilder<BadEnum> &t) {
            t.field("mode", &BadEnum::mode); // Missing options().
        }
    };
    threw = false;
    try {
        registry.add<BadEnum>("BadEnum");
    } catch (const std::logic_error &) {
        threw = true;
    }
    CHECK(threw);
    ComponentRegistry cyclic;
    struct A final : Component {
        static void describe(TypeBuilder<A> &t) {
            t.dependsOn("B");
        }
    };
    struct B final : Component {
        static void describe(TypeBuilder<B> &t) {
            t.dependsOn("A");
        }
    };
    cyclic.add<A>("A");
    CHECK(!cyclic.validate()); // "B" is not registered yet.
    cyclic.add<B>("B");
    CHECK(!cyclic.validate()); // Registered, but the dependencies loop.
    registry.addTemplate({"Thing", "Test", [](Scene &scene, Vec2 at) {
                              Entity &entity = scene.createEntity("Thing");
                              entity.setWorldPosition(at);
                              return entity.id();
                          }});
    CHECK(registry.templates().size() == 1);
    threw = false;
    try {
        registry.addTemplate({"Thing", "Test", nullptr});
    } catch (const std::logic_error &) {
        threw = true;
    }
    CHECK(threw);
}

void reflection() {
    auto registry = makeRegistry();
    Scene scene(registry, 1);
    Entity &entity = scene.createEntity("E");
    auto &widget = entity.add<Widget>();
    const ComponentType &type = widget.type();
    const auto property = [&](const char *name) -> const PropertyInfo & {
        return *type.find(name);
    };
    CHECK(type.properties.size() == 12 && type.find("count")->defaultValue.index() == 1);
    CHECK(std::get<std::int64_t>(property("count").defaultValue) == 3);
    CHECK(std::get<bool>(property("flag").read(widget)));
    CHECK(property("count").assign(widget, std::int64_t{7}) && widget.count == 7);
    CHECK(property("count").assign(widget, std::int64_t{99}) &&
          widget.count == 10); // Clamped to range.
    CHECK(property("count").assign(widget, -5.0) &&
          widget.count == 0); // Doubles accepted, clamped.
    CHECK(!property("count").assign(widget, std::string("x")) &&
          widget.count == 0); // Wrong type rejected.
    CHECK(!property("count").assign(widget, std::numeric_limits<double>::quiet_NaN()));
    CHECK(property("speed").assign(widget, 9.0) && widget.speed == 5.0F);
    CHECK(!property("speed").assign(widget, std::numeric_limits<double>::infinity()) &&
          widget.speed == 5.0F);
    CHECK(property("size").assign(widget, Vec2{500, -3}) &&
          widget.size == Vec2{100, 0.1F}); // Per-axis clamp.
    CHECK(!property("size").assign(widget, Vec2{std::numeric_limits<float>::infinity(), 1}));
    CHECK(property("mode").assign(widget, std::int64_t{2}) && widget.mode == Mode::Gamma);
    CHECK(!property("mode").assign(widget, std::int64_t{3}) &&
          !property("mode").assign(widget, std::int64_t{-1}) && widget.mode == Mode::Gamma);
    CHECK(property("label").assign(widget, std::string("hi")) && widget.label == "hi");
    CHECK(property("tint").assign(widget, Color{9, 8, 7, 6}) && widget.tint == Color{9, 8, 7, 6});
    CHECK(property("target").assign(widget, EntityId{5}) && widget.target == EntityId{5});
    CHECK(property("targets").assign(widget, std::vector<EntityId>{{1}, {2}}) &&
          widget.targets.size() == 2);
    CHECK(property("names").assign(widget, std::vector<std::string>{"x", "y"}) &&
          widget.names.size() == 2);
    CHECK(property("sound").assign(widget, AssetRef{"a.wav"}) && widget.sound.path == "a.wav");
    CHECK(!property("runtimeOnly").assign(widget, std::int64_t{1}) &&
          widget.runtimeOnly == 7); // Read-only.
    CHECK(prettifyName("moveSpeed") == "Move Speed" && prettifyName("is_trigger") == "Is Trigger" &&
          prettifyName("size") == "Size" && prettifyName("aBC") == "A BC");
}

void hierarchy() {
    auto registry = makeRegistry();
    Scene scene(registry, 2);
    Entity &a = scene.createEntity("A");
    Entity &b = scene.createEntity("B");
    Entity &a1 = scene.createEntity("A1", a.id());
    Entity &a2 = scene.createEntity("A2", a.id());
    Entity &a1x = scene.createEntity("A1x", a1.id());
    CHECK(scene.size() == 5 && scene.roots().size() == 2 && scene.roots()[0] == a.id());
    const auto order = scene.hierarchyOrder();
    CHECK(order.size() == 5 && order[0] == a.id() && order[1] == a1.id() && order[2] == a1x.id() &&
          order[3] == a2.id() && order[4] == b.id());
    CHECK(scene.findByName("A2") == &a2 && !scene.findByName("nope"));
    CHECK(scene.isAncestor(a.id(), a1x.id()) && !scene.isAncestor(a1x.id(), a.id()) &&
          !scene.isAncestor(b.id(), a.id()));
    CHECK(!scene.setParent(a.id(), a1x.id()) &&
          !scene.setParent(a.id(), a.id())); // Cycles rejected.
    CHECK(!scene.setParent(EntityId{999}, a.id()) && !scene.setParent(a.id(), EntityId{999}));
    CHECK(scene.setParent(a2.id(), b.id()) && a2.parentId() == b.id() && a.childIds().size() == 1);
    CHECK(scene.setParent(a2.id(), a.id(), 0) && a.childIds()[0] == a2.id() &&
          a.childIds()[1] == a1.id());
    CHECK(scene.setSiblingIndex(a2.id(), 5) && a.childIds().back() == a2.id());
    CHECK(scene.setParent(a1.id(), EntityId{}) && a1.parentId() == EntityId{} &&
          scene.roots().back() == a1.id());
    const EntityId doomed = a.id();
    const EntityId doomedChild = a2.id(); // `a` and `a2` are gone once the subtree is destroyed.
    CHECK(scene.destroy(doomed) && !scene.find(doomed) && !scene.find(doomedChild) &&
          scene.size() == 3 && !scene.destroy(doomed));
    CHECK(scene.find(a1x.id()) != nullptr &&
          scene.find(a1x.id())->parentId() == a1.id()); // Unrelated subtree survives.
    Entity &unknownParent = scene.createEntity("Orphan", EntityId{12345});
    CHECK(unknownParent.parentId() == EntityId{} && scene.roots().back() == unknownParent.id());
    CHECK(!scene.createEntityWithId(EntityId{}, "x") && !scene.createEntityWithId(b.id(), "dup"));
    Entity &fixed = *scene.createEntityWithId(EntityId{77}, "Fixed").value();
    CHECK(fixed.id() == EntityId{77});
}

// orderedIds() is the cached form of hierarchyOrder(): same order, rebuilt after every structural
// change (a stale cache would make the renderer or runtime skip or repeat entities).
void cachedOrder() {
    auto registry = makeRegistry();
    Scene scene(registry, 6);
    const auto matches = [&] { return scene.orderedIds() == scene.hierarchyOrder(); };
    CHECK(scene.orderedIds().empty());
    Entity &a = scene.createEntity("A");
    CHECK(scene.orderedIds().size() == 1 && matches());
    Entity &b = scene.createEntity("B");
    Entity &a1 = scene.createEntity("A1", a.id());
    CHECK(scene.orderedIds().size() == 3 && matches());
    CHECK(scene.orderedIds()[0] == a.id() && scene.orderedIds()[1] == a1.id() &&
          scene.orderedIds()[2] == b.id());
    // Reparenting, reordering and destroying each invalidate it.
    CHECK(scene.setParent(b.id(), a.id(), 0));
    CHECK(matches() && scene.orderedIds()[1] == b.id());
    CHECK(scene.setSiblingIndex(b.id(), 1));
    CHECK(matches() && scene.orderedIds()[2] == b.id());
    CHECK(scene.destroy(a1.id()));
    CHECK(matches() && scene.orderedIds().size() == 2);
    CHECK(scene.findByName("B") == &b && !scene.findByName("A1"));
    // A copy taken earlier is unaffected by later changes (callers rely on that while iterating).
    const auto snapshot = scene.hierarchyOrder();
    scene.createEntity("C");
    CHECK(snapshot.size() == 2 && scene.orderedIds().size() == 3);
    // Repeated calls without changes give the very same storage (no rebuild).
    const auto *first = scene.orderedIds().data();
    CHECK(scene.orderedIds().data() == first);
}

void transforms() {
    auto registry = makeRegistry();
    Scene scene(registry, 3);
    Entity &parent = scene.createEntity("P");
    parent.transform().position = {10, 5};
    parent.transform().rotationDegrees = 90;
    parent.transform().scale = {2, 2};
    Entity &child = scene.createEntity("C", parent.id());
    child.transform().position = {1, 0};
    const Vec2 world = child.worldPosition();
    CHECK_NEAR(world.x, 10.0, 1e-4);
    CHECK_NEAR(world.y, 7.0,
               1e-4); // (1,0) scaled x2, rotated 90deg -> (0,2), plus the parent offset.
    child.setWorldPosition({10, 9});
    CHECK_NEAR(child.transform().position.x, 2.0, 1e-4);
    CHECK_NEAR(child.transform().position.y, 0.0, 1e-4);
    CHECK_NEAR(child.worldTransform().rotationDegrees, 90.0, 1e-4);
    CHECK_NEAR(child.worldTransform().scale.x, 2.0, 1e-4);
    Entity &free = scene.createEntity("Free");
    free.transform().position = {3, 3};
    CHECK(
        scene.setParent(free.id(), parent.id(), std::nullopt, true)); // Keeps its world placement.
    CHECK_NEAR(free.worldPosition().x, 3.0, 1e-3);
    CHECK_NEAR(free.worldPosition().y, 3.0, 1e-3);
    CHECK(scene.setParent(free.id(), EntityId{}, std::nullopt, false));
    CHECK_NEAR(free.worldPosition().x, free.transform().position.x,
               1e-6); // Detached: local == world.
    parent.setActive(false);
    CHECK(!child.activeInHierarchy() && child.active() && free.activeInHierarchy());
    child.addTag("x");
    child.addTag("x");
    child.addTag("");
    CHECK(child.tags().size() == 1 && child.hasTag("x") && !child.hasTag("y"));
    child.removeTag("x");
    CHECK(child.tags().empty());
}

void components() {
    auto registry = makeRegistry();
    Scene scene(registry, 4);
    Entity &entity = scene.createEntity("E");
    setLogStderrEnabled(false); // The unknown type is reported through the log by design.
    CHECK(!entity.addComponent("Missing"));
    setLogStderrEnabled(true);
    auto &follower = entity.add<Follower>(); // Adds its Widget dependency first.
    CHECK(entity.components().size() == 2 && entity.components()[0]->type().name == "Widget");
    CHECK(entity.has<Widget>() && entity.has<Follower>() && !entity.has<Stackable>());
    CHECK(entity.get<Pingable>() == &follower &&
          entity.get<Pingable>()->ping() == 42); // Interface lookup.
    CHECK(&entity.add<Widget>() == entity.get<Widget>() &&
          entity.components().size() == 2); // No duplicates.
    entity.add<Stackable>().level = 1;
    entity.add<Stackable>().level = 2;
    CHECK(entity.getAll<Stackable>().size() == 2 && entity.components().size() == 4);
    CHECK(entity.removalBlocker(*entity.get<Widget>()) == "Follower");
    CHECK(!entity.removeComponent(entity.get<Widget>()));
    CHECK(entity.removeComponent(&follower) && entity.removeComponent(entity.get<Widget>()));
    CHECK(entity.components().size() == 2 && !entity.removeComponent(&follower));
    // A dependency is satisfied by an existing instance even when its type allows several.
    struct NeedsStack final : Component {
        static void describe(TypeBuilder<NeedsStack> &t) {
            t.dependsOn("Stackable");
        }
    };
    auto extended = makeRegistry();
    extended.add<NeedsStack>("NeedsStack");
    Scene stackScene(extended, 6);
    Entity &stacked = stackScene.createEntity("S");
    stacked.add<Stackable>();
    stacked.add<NeedsStack>();
    CHECK(stacked.getAll<Stackable>().size() == 1);
    ComponentRegistry empty;
    Scene bare(empty, 5);
    bool threw = false;
    try {
        bare.createEntity("x").add<Widget>();
    } catch (const std::logic_error &) {
        threw = true;
    }
    CHECK(threw);
    const auto before = scene.revision();
    entity.add<Widget>();
    CHECK(scene.revision() > before);
}

std::unique_ptr<Scene> buildSample(const ComponentRegistry &registry) {
    auto scene = std::make_unique<Scene>(registry, 100);
    scene->settings.name = "Sample";
    scene->settings.gravity = {0, 20.5F};
    scene->settings.background = {10, 20, 30, 255};
    Entity &root = scene->createEntity("Root");
    root.transform().position = {1.5F, -2.25F};
    root.transform().rotationDegrees = 33.5F;
    root.transform().scale = {2, 0.5F};
    root.addTag("solid");
    root.addTag("blue");
    Entity &kid = scene->createEntity("Kid", root.id());
    auto &widget = kid.add<Widget>();
    widget.flag = false;
    widget.count = 9;
    widget.speed = 0.1F;
    widget.label = "quote\"and\nnewline\xC3\xA9";
    widget.size = {3.5F, 0.25F};
    widget.tint = {200, 100, 50, 25};
    widget.mode = Mode::Gamma;
    widget.target = root.id();
    widget.targets = {root.id(), kid.id()};
    widget.names = {"one", "two"};
    widget.sound = {"assets/door.wav"};
    kid.add<Follower>().offset = -1.75F;
    kid.add<Stackable>().level = 5;
    kid.add<Stackable>().level = 6;
    kid.get<Stackable>()->enabled = false;
    kid.setActive(false);
    scene->createEntity("Sibling", root.id());
    scene->createEntity("Other");
    return scene;
}

void serialization() {
    auto registry = makeRegistry();
    auto scene = buildSample(registry);
    const Json document = sceneToJson(*scene);
    auto loaded = sceneFromJson(document, registry);
    CHECK(loaded);
    if (!loaded)
        return;
    Scene &copy = *loaded.value();
    CHECK(copy.size() == scene->size() && copy.settings.name == "Sample" &&
          copy.settings.gravity == Vec2{0, 20.5F} &&
          copy.settings.background == Color{10, 20, 30, 255});
    CHECK(sceneToJson(copy) == document); // Lossless, including ids and order.
    CHECK(sceneToJson(copy).dump(2) == document.dump(2));
    Entity *kid = copy.findByName("Kid");
    CHECK(kid && !kid->active() && kid->parent() && kid->parent()->name() == "Root");
    CHECK(kid && kid->get<Widget>()->label == "quote\"and\nnewline\xC3\xA9" &&
          kid->get<Widget>()->mode == Mode::Gamma);
    CHECK(kid && kid->get<Widget>()->speed == 0.1F &&
          kid->get<Widget>()->size == Vec2{3.5F, 0.25F});
    CHECK(kid && kid->get<Widget>()->target == kid->parentId() &&
          kid->get<Widget>()->targets.size() == 2);
    CHECK(kid && kid->getAll<Stackable>().size() == 2 && !kid->getAll<Stackable>()[0]->enabled &&
          kid->getAll<Stackable>()[1]->enabled && kid->getAll<Stackable>()[1]->level == 6);
    CHECK(kid && kid->get<Widget>()->runtimeOnly == 7);
    Entity *root = copy.findByName("Root");
    CHECK(root && root->hasTag("solid") && root->hasTag("blue") && root->childIds().size() == 2);
    CHECK(root && root->transform().rotationDegrees == 33.5F &&
          root->transform().scale == Vec2{2, 0.5F});
    CHECK(document.dump().find("runtimeOnly") ==
          std::string::npos); // Read-only state is not saved.
    CHECK(document.dump(2).find("\"position\": [1.5, -2.25]") != std::string::npos);

    const auto file =
        std::filesystem::temp_directory_path() / "yk-scene-test" / "nested" / "a.ykscene";
    std::filesystem::remove_all(file.parent_path().parent_path());
    CHECK(saveScene(*scene, file));
    auto fromFile = loadScene(file, registry);
    CHECK(fromFile && sceneToJson(*fromFile.value()) == document);
    CHECK(!loadScene(file.parent_path() / "missing.ykscene", registry));
    std::filesystem::remove_all(file.parent_path().parent_path());
}

std::string loadError(const std::string &text, const ComponentRegistry &registry) {
    auto document = Json::parse(text);
    if (!document)
        return "unparseable: " + document.error();
    auto scene = sceneFromJson(document.value(), registry);
    return scene ? std::string() : scene.error();
}

Result<std::unique_ptr<Scene>> loadText(const std::string &text,
                                        const ComponentRegistry &registry) {
    auto document = Json::parse(text);
    if (!document)
        return Error{document.error()};
    return sceneFromJson(document.value(), registry);
}

void malformedScenes() {
    auto registry = makeRegistry();
    const auto scene = [](const std::string &entities) {
        return R"({"format":"yk.scene","version":1,"entities":)" + entities + "}";
    };
    CHECK(loadError(scene("[]"), registry).empty());
    CHECK(loadError(R"({"format":"other","version":1,"entities":[]})", registry)
              .find("Not a yk.scene") != std::string::npos);
    CHECK(
        loadError(R"({"format":"yk.scene","version":99,"entities":[]})", registry).find("newer") !=
        std::string::npos);
    CHECK(!loadError(R"({"format":"yk.scene","entities":[]})", registry).empty()); // No version.
    CHECK(!loadError("[]", registry).empty());
    CHECK(!loadError(scene(R"({"a":1})"), registry).empty()); // 'entities' not an array.
    CHECK(!loadError(scene(R"([{"name":"no id"}])"), registry).empty());
    CHECK(!loadError(scene(R"([{"id":"0000000000000001"},{"id":"0000000000000001"}])"), registry)
               .empty());
    CHECK(!loadError(scene(R"([{"id":"0000000000000001","parent":"0000000000000009"}])"), registry)
               .empty());
    CHECK(loadError(scene(R"([{"id":"0000000000000001","parent":"0000000000000002"},)"
                          R"({"id":"0000000000000002","parent":"0000000000000001"}])"),
                    registry)
              .find("descendant") != std::string::npos); // Parent cycle.
    const std::string base = R"([{"id":"0000000000000001","name":"E","components":[)";
    const std::string bad = loadError(scene(base + R"({"type":"Nope"}]}])"), registry);
    CHECK(bad.find("unknown component type 'Nope'") != std::string::npos &&
          bad.find("'E'") != std::string::npos);
    const std::string wrongType =
        loadError(scene(base + R"({"type":"Widget","properties":{"count":"x"}}]}])"), registry);
    CHECK(wrongType.find("count") != std::string::npos &&
          wrongType.find("Widget") != std::string::npos);
    CHECK(
        !loadError(scene(base + R"({"type":"Widget","properties":{"mode":"Delta"}}]}])"), registry)
             .empty());
    CHECK(!loadError(scene(base + R"({"type":"Widget","properties":{"size":[1]}}]}])"), registry)
               .empty());
    CHECK(!loadError(scene(base + R"({"type":"Widget","properties":{"tint":"red"}}]}])"), registry)
               .empty());
    CHECK(
        !loadError(scene(base + R"({"type":"Widget","properties":{"target":"bad"}}]}])"), registry)
             .empty());
    // Unknown properties are skipped with a warning; known ones still load.
    std::vector<std::string> warnings;
    setLogStderrEnabled(false);
    setLogSink([&](LogLevel level, std::string_view, std::string_view message) {
        if (level == LogLevel::Warning)
            warnings.emplace_back(message);
    });
    auto tolerant = loadText(
        scene(base + R"({"type":"Widget","properties":{"future":1,"count":4}}]}])"), registry);
    CHECK(tolerant && tolerant.value()->findByName("E")->get<Widget>()->count == 4);
    CHECK(warnings.size() == 1 && warnings[0].find("future") != std::string::npos);
    warnings.clear();
    auto dangling =
        loadText(scene(base + R"({"type":"Widget","properties":{"target":"00000000000000aa"}}]}])"),
                 registry);
    CHECK(dangling && warnings.size() == 1 &&
          warnings[0].find("missing entity") != std::string::npos);
    setLogSink({});
    setLogStderrEnabled(true);
    // Out-of-range values are clamped rather than trusted.
    auto clamped =
        loadText(scene(base + R"({"type":"Widget","properties":{"count":500}}]}])"), registry);
    CHECK(clamped && clamped.value()->findByName("E")->get<Widget>()->count == 10);
    // Dependencies listed after their dependents still load with their saved values.
    auto ordered = loadText(
        scene(base + R"({"type":"Follower"},{"type":"Widget","properties":{"count":8}}]}])"),
        registry);
    CHECK(ordered && ordered.value()->findByName("E")->get<Widget>()->count == 8 &&
          ordered.value()->findByName("E")->components().size() == 2);
}

void prefabs() {
    auto registry = makeRegistry();
    Scene scene(registry, 200);
    Entity &outside = scene.createEntity("Outside");
    Entity &root = scene.createEntity("PrefabRoot");
    root.transform().position = {4, 4};
    Entity &leaf = scene.createEntity("Leaf", root.id());
    leaf.transform().position = {1, 1};
    auto &rootWidget = root.add<Widget>();
    rootWidget.target = leaf.id();                  // Internal reference: must be remapped.
    rootWidget.targets = {leaf.id(), outside.id()}; // One internal, one external.
    leaf.add<Widget>().target = outside.id();       // External: must be cleared.
    const Json prefab = subtreeToJson(scene, root.id());
    CHECK(prefab.get("format").asString() == "yk.prefab" && prefab.get("entities").size() == 2);
    CHECK(!prefab.get("entities").at(0).contains("parent")); // Root carries no parent.

    Entity &holder = scene.createEntity("Holder");
    holder.transform().position = {100, 0};
    auto first = instantiateSubtree(scene, prefab, holder.id(), Vec2{110, 20});
    auto second = instantiateSubtree(scene, prefab, {});
    CHECK(first && second && first.value() != second.value() && first.value() != root.id());
    CHECK(scene.size() == 3 + 1 + 2 + 2);
    Entity *copy = scene.find(first.value());
    CHECK(copy && copy->name() == "PrefabRoot" && copy->parentId() == holder.id());
    CHECK_NEAR(copy->worldPosition().x, 110.0, 1e-3);
    CHECK_NEAR(copy->worldPosition().y, 20.0, 1e-3);
    CHECK(copy->childIds().size() == 1);
    Entity *copyLeaf = scene.find(copy->childIds()[0]);
    CHECK(copyLeaf && copyLeaf->id() != leaf.id());
    auto *copyWidget = copy->get<Widget>();
    CHECK(copyWidget && copyWidget->target == copyLeaf->id());
    CHECK(copyWidget && copyWidget->targets.size() == 1 &&
          copyWidget->targets[0] == copyLeaf->id()); // External dropped.
    CHECK(copyLeaf->get<Widget>()->target == EntityId{});
    CHECK(rootWidget.target == leaf.id() &&
          leaf.get<Widget>()->target == outside.id()); // Source untouched.
    Entity *other = scene.find(second.value());
    CHECK(other &&
          other->get<Widget>()->target != copyWidget->target); // Instances are independent.

    // Duplicating inside one scene keeps references to entities that are not being copied.
    auto duplicate = instantiateSubtree(scene, prefab, {}, std::nullopt, true);
    CHECK(duplicate);
    Entity *duplicated = scene.find(duplicate.value());
    CHECK(duplicated && duplicated->get<Widget>()->targets.size() == 2);
    CHECK(duplicated &&
          duplicated->get<Widget>()->targets[0] == scene.find(duplicated->childIds()[0])->id());
    CHECK(duplicated && duplicated->get<Widget>()->targets[1] == outside.id()); // Kept.
    CHECK(scene.find(duplicated->childIds()[0])->get<Widget>()->target == outside.id());
    // ...but a reference to an entity the destination lacks is still cleared.
    Scene elsewhere(registry, 900);
    auto stranger = instantiateSubtree(elsewhere, prefab, {}, std::nullopt, true);
    CHECK(stranger && elsewhere.find(stranger.value())->get<Widget>()->targets.size() == 1);
    // Several documents at once: references between them are remapped to the new copies.
    Entity &second_root = scene.createEntity("SecondRoot");
    second_root.add<Widget>().target = root.id(); // Points at the first prefab's root.
    const Json secondPrefab = subtreeToJson(scene, second_root.id());
    const auto pair =
        instantiateSubtrees(scene, {{&prefab, {}}, {&secondPrefab, holder.id()}}, true);
    CHECK(pair && pair.value().size() == 2);
    Entity *pairFirst = scene.find(pair.value()[0]);
    Entity *pairSecond = scene.find(pair.value()[1]);
    CHECK(pairFirst && pairSecond && pairSecond->parentId() == holder.id());
    CHECK(pairSecond->get<Widget>()->target == pairFirst->id()); // Not the original root.
    CHECK(pairFirst->get<Widget>()->targets[1] == outside.id()); // External kept.
    const auto sizeBeforePair = scene.size();
    Json invalid = Json::object();
    CHECK(!instantiateSubtrees(scene, {{&prefab, {}}, {&invalid, {}}}));
    CHECK(scene.size() == sizeBeforePair); // The first document was rolled back too.
    CHECK(!instantiateSubtree(scene, Json::object()));
    Json broken = prefab;
    broken.set("root", "00000000000000bb");
    const auto sizeBefore = scene.size();
    CHECK(!instantiateSubtree(scene, broken) &&
          scene.size() == sizeBefore); // Failure leaves no debris.
    const auto file = std::filesystem::temp_directory_path() / "yk-prefab-test" / "thing.ykprefab";
    CHECK(savePrefab(scene, root.id(), file));
    auto document = loadPrefabDocument(file);
    CHECK(document && document.value() == prefab);
    CHECK(!savePrefab(scene, EntityId{31337}, file) &&
          !loadPrefabDocument(file.parent_path() / "none.ykprefab"));
    std::filesystem::remove_all(file.parent_path());
}

void prefabSources() {
    auto registry = makeRegistry();
    Scene scene(registry, 300);
    Entity &root = scene.createEntity("Thing");
    scene.createEntity("Part", root.id()).add<Widget>();
    const Json prefab = subtreeToJson(scene, root.id());
    CHECK(!prefab.get("entities").at(0).contains("prefab")); // An ordinary entity is no instance.

    const std::string path = "prefabs/thing.ykprefab";
    auto placed = instantiateSubtree(scene, prefab, {}, Vec2{3, 4}, false, path);
    CHECK(placed);
    Entity *instance = scene.find(placed.value());
    CHECK(instance && instance->prefabSource() == path);
    CHECK(instance && scene.find(instance->childIds()[0])->prefabSource().empty()); // Root only.
    CHECK(root.prefabSource().empty());

    // The scene file keeps the link and loads it back.
    const Json document = sceneToJson(scene);
    auto reloaded = sceneFromJson(document, registry);
    CHECK(reloaded && reloaded.value()->find(placed.value()) &&
          reloaded.value()->find(placed.value())->prefabSource() == path);
    CHECK(reloaded && reloaded.value()->find(root.id())->prefabSource().empty());
    Json broken = document;
    for (std::size_t i = 0; i < broken.get("entities").size(); ++i)
        if (broken.get("entities").at(i).get("id").asString() == toString(placed.value()))
            broken.find("entities")->at(i).set("prefab", 5);
    CHECK(!sceneFromJson(broken, registry)); // A prefab reference must be a string.

    // Locking is an editor hint saved with the scene; it covers everything below the entity.
    Entity &locker = scene.createEntity("Locker");
    Entity &locked = scene.createEntity("Locked Child", locker.id());
    CHECK(!locked.lockedInHierarchy());
    locker.setLocked(true);
    CHECK(locked.lockedInHierarchy() && !locked.locked());
    auto withLock = sceneFromJson(sceneToJson(scene), registry);
    CHECK(withLock && withLock.value()->find(locker.id())->locked() &&
          withLock.value()->find(locked.id())->lockedInHierarchy());
    CHECK(!sceneToJson(scene).get("entities").at(0).contains("locked"));
    locker.setLocked(false);

    // A copy of an instance stays an instance; a prefab saved from an instance names nothing.
    const Json copy = subtreeToJson(scene, placed.value());
    CHECK(copy.get("entities").at(0).get("prefab").asString() == path);
    auto duplicate = instantiateSubtree(scene, copy, {}, std::nullopt, true);
    CHECK(duplicate && scene.find(duplicate.value())->prefabSource() == path);
    const auto file =
        std::filesystem::temp_directory_path() / "yk-prefab-source-test" / "again.ykprefab";
    CHECK(savePrefab(scene, placed.value(), file));
    auto saved = loadPrefabDocument(file);
    CHECK(saved && !saved.value().get("entities").at(0).contains("prefab"));
    std::filesystem::remove_all(file.parent_path());
}
void reapplyingPrefabs() {
    auto registry = makeRegistry();
    // The prefab: a gate with two parts; the gate's widget points at one of its own parts.
    Scene template_(registry, 400);
    Entity &gate = template_.createEntity("Gate");
    gate.add<Widget>().label = "from the prefab";
    Entity &handle = template_.createEntity("Handle", gate.id());
    handle.add<Widget>().count = 2;
    template_.createEntity("Latch", gate.id());
    gate.get<Widget>()->target = handle.id();
    const Json prefab = subtreeToJson(template_, gate.id());
    const std::string source = "prefabs/gate.ykprefab";

    // A level with the gate placed in it, a lever that points at the gate and a door beside it.
    Scene level(registry, 401);
    Entity &door = level.createEntity("Door");
    Entity &lever = level.createEntity("Lever");
    auto placed = instantiateSubtree(level, prefab, {}, Vec2{5, 6}, false, source);
    CHECK(placed);
    const EntityId gateId = placed.value();
    lever.add<Widget>().target = gateId;

    // The designer's changes to this one instance: a new name, tag and rotation on the root, a
    // changed label, a wire to the door, an edited part and a deleted part.
    Entity *instance = level.find(gateId);
    instance->setName("Front Gate");
    instance->addTag("front");
    instance->transform().rotationDegrees = 30.0F;
    instance->get<Widget>()->label = "edited";
    instance->get<Widget>()->targets.push_back(door.id());
    Entity *editedHandle = level.findByName("Handle");
    CHECK(editedHandle);
    editedHandle->get<Widget>()->count = 9;
    const EntityId oldHandle = editedHandle->id();
    Entity *latch = level.findByName("Latch");
    CHECK(latch);
    level.destroy(latch->id());
    CHECK(instance->childIds().size() == 1);
    const std::size_t entitiesBefore = level.size();

    CHECK(reapplyPrefab(level, gateId, prefab, source));
    instance = level.find(gateId);
    CHECK(instance); // The root is the same entity, so the lever's link is still good.
    CHECK(level.find(lever.id())->get<Widget>()->target == gateId);
    // Identity and place stay; content comes from the prefab.
    CHECK(instance->name() == "Front Gate" && instance->hasTag("front"));
    CHECK(instance->transform().position == Vec2{5, 6} &&
          instance->transform().rotationDegrees == 30.0F);
    CHECK(instance->prefabSource() == source);
    CHECK(instance->get<Widget>()->label == "from the prefab");
    CHECK(instance->childIds().size() == 2 && level.size() == entitiesBefore + 1);
    Entity *newHandle = level.findByName("Handle");
    CHECK(newHandle && newHandle->id() != oldHandle && newHandle->get<Widget>()->count == 2);
    CHECK(level.findByName("Latch") != nullptr);
    // The gate's link to its own part now points at the new part; the wire to the door survived.
    CHECK(newHandle && instance->get<Widget>()->target == newHandle->id());
    CHECK(instance->get<Widget>()->targets.size() == 1 &&
          instance->get<Widget>()->targets[0] == door.id());
    CHECK(level.find(door.id()) && level.find(lever.id()));

    // An invalid prefab changes nothing.
    Json broken = prefab;
    broken.set("format", "nope");
    const std::size_t count = level.size();
    CHECK(!reapplyPrefab(level, gateId, broken, source));
    CHECK(level.size() == count && level.find(gateId)->childIds().size() == 2);
    CHECK(!reapplyPrefab(level, EntityId{0x99}, prefab, source));

    // A prefab that points at its own root: after the update the reference means the reused root.
    Scene loop(registry, 402);
    Entity &owner = loop.createEntity("Owner");
    owner.add<Widget>();
    Entity &child = loop.createEntity("Child", owner.id());
    child.add<Widget>().target = owner.id();
    const Json loopPrefab = subtreeToJson(loop, owner.id());
    Scene home(registry, 403);
    auto instantiated =
        instantiateSubtree(home, loopPrefab, {}, std::nullopt, false, "prefabs/o.ykprefab");
    CHECK(instantiated);
    CHECK(reapplyPrefab(home, instantiated.value(), loopPrefab, "prefabs/o.ykprefab"));
    const Entity *reusedChild = home.findByName("Child");
    CHECK(reusedChild && reusedChild->get<Widget>()->target == instantiated.value());
}
} // namespace

int main() {
    entityIds();
    registration();
    reflection();
    hierarchy();
    cachedOrder();
    transforms();
    components();
    serialization();
    malformedScenes();
    prefabs();
    prefabSources();
    reapplyingPrefabs();
    return yk::test::finish("scene");
}
