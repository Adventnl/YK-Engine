// Everything the editor does to a scene, exercised without a window: undo, selection, picking,
// gizmo drags, project files and play sessions.
#include "Modules.hpp"
#include "core/ConsoleLog.hpp"
#include "core/EditorDocument.hpp"
#include "core/EditorGeometry.hpp"
#include "core/EditorProject.hpp"
#include "core/PlaySession.hpp"
#include "core/SceneInteraction.hpp"
#include "support/check.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <cmath>
#include <filesystem>

using namespace yk;
using namespace yk::editor;

namespace {
const EntityTemplate &templateNamed(const ComponentRegistry &registry, const std::string &name) {
    for (const EntityTemplate &entityTemplate : registry.templates())
        if (entityTemplate.name == name)
            return entityTemplate;
    std::fprintf(stderr, "no template '%s'\n", name.c_str());
    std::abort();
}

std::size_t componentIndex(const Entity &entity, const std::string &type) {
    for (std::size_t i = 0; i < entity.components().size(); ++i)
        if (entity.components()[i]->type().name == type)
            return i;
    return static_cast<std::size_t>(-1);
}

struct Fixture {
    ComponentRegistry registry;
    std::unique_ptr<EditorDocument> document;
    Fixture() {
        registerAllModules(registry);
        document = std::make_unique<EditorDocument>(registry, std::make_unique<Scene>(registry, 7));
    }
    EditorDocument &doc() {
        return *document;
    }
    // A sprite and a matching collider, centered at `at`.
    EntityId box(const char *name, Vec2 at, Vec2 size) {
        EntityId id;
        document->change("add box", [&](Scene &scene) {
            Entity &entity = scene.createEntity(name);
            entity.setWorldPosition(at);
            entity.add<SpriteRenderer>().size = size;
            entity.add<Collider>().size = size;
            id = entity.id();
        });
        return id;
    }
    const Entity &get(EntityId id) const {
        return *document->scene().find(id);
    }
};

// ---------------------------------------------------------------------------------------------
void undoAndRedo() {
    Fixture f;
    EditorDocument &doc = f.doc();
    CHECK(!doc.dirty() && !doc.canUndo() && !doc.canRedo());
    const EntityId a = doc.createEntity("Thing", {}, Vec2{2.0F, 3.0F});
    CHECK(doc.scene().size() == 1 && doc.primary() == a && doc.dirty());
    CHECK(doc.canUndo() && doc.undoLabel() == "Create Entity");
    CHECK_NEAR(f.get(a).worldPosition().x, 2.0);
    CHECK(doc.undo() && doc.scene().size() == 0 && doc.selection().empty() && !doc.dirty());
    CHECK(doc.canRedo() && doc.redoLabel() == "Create Entity" && !doc.canUndo());
    CHECK(doc.redo() && doc.scene().find(a) && doc.primary() == a && doc.dirty());
    CHECK(!doc.redo() && !doc.canRedo());

    doc.markSaved();
    CHECK(!doc.dirty());
    doc.rename(a, "Renamed");
    CHECK(doc.dirty() && f.get(a).name() == "Renamed");
    CHECK(doc.undo() && !doc.dirty() && f.get(a).name() == "Thing"); // Back to the saved state.
    CHECK(doc.redo() && doc.dirty());
    CHECK(doc.undo());
    doc.rename(a, "Other"); // A new change discards the redo history.
    CHECK(!doc.canRedo() && !doc.redo());

    // A long drag is a single step, and only the final result matters.
    const std::size_t depth = doc.undoDepth();
    doc.beginChange("Drag");
    CHECK(doc.inChange() && !doc.canUndo()); // Undo waits for the edit to finish.
    for (int i = 1; i <= 10; ++i)
        doc.edit().find(a)->transform().position.x = static_cast<float>(i);
    doc.endChange();
    CHECK(!doc.inChange() && doc.undoDepth() == depth + 1 && doc.undoLabel() == "Drag");
    CHECK_NEAR(f.get(a).transform().position.x, 10.0);
    CHECK(doc.undo() && f.get(a).transform().position.x != 10.0F);

    // A change that ends where it began leaves no step and does not dirty the scene.
    doc.redo();
    doc.markSaved();
    const std::size_t before = doc.undoDepth();
    doc.beginChange("Nothing");
    doc.edit().find(a)->transform().position.x = 99.0F;
    doc.edit().find(a)->transform().position.x = 10.0F;
    doc.endChange();
    CHECK(doc.undoDepth() == before && !doc.dirty());

    // Cancelling restores everything.
    doc.beginChange("Cancelled");
    doc.edit().find(a)->transform().position = {50.0F, 50.0F};
    doc.edit().createEntity("Extra");
    doc.cancelChange();
    CHECK(!doc.inChange() && f.get(a).transform().position.x == 10.0F && doc.scene().size() == 1);
    CHECK(doc.undoDepth() == before && !doc.dirty());

    // Nested changes merge into the outer one.
    doc.change("Outer", [&](Scene &) {
        doc.rename(a, "Inner one");
        doc.rename(a, "Inner two");
    });
    CHECK(doc.undoDepth() == before + 1 && doc.undoLabel() == "Outer");
    CHECK(doc.undo() && f.get(a).name() != "Inner one" && f.get(a).name() != "Inner two");
}

void historyLimit() {
    Fixture f;
    for (int i = 0; i < 230; ++i)
        f.doc().createEntity("E");
    CHECK(f.doc().undoDepth() == EditorDocument::maxUndoSteps);
    std::size_t undone = 0;
    while (f.doc().undo())
        ++undone;
    CHECK(undone == EditorDocument::maxUndoSteps && f.doc().scene().size() == 30);
    CHECK(f.doc().dirty()); // The saved state (empty) is no longer reachable.
}

void selection() {
    Fixture f;
    EditorDocument &doc = f.doc();
    const EntityId a = f.box("A", {0, 0}, {1, 1});
    const EntityId b = f.box("B", {3, 0}, {1, 1});
    const EntityId c = f.box("C", {6, 0}, {1, 1});
    doc.select(a);
    doc.select(b, SelectMode::Add);
    CHECK(doc.selection() == std::vector<EntityId>({a, b}) && doc.primary() == b);
    doc.select(a, SelectMode::Add); // Re-selecting makes it primary without duplicating it.
    CHECK(doc.selection() == std::vector<EntityId>({b, a}) && doc.primary() == a);
    doc.select(b, SelectMode::Toggle);
    CHECK(doc.selection() == std::vector<EntityId>({a}));
    doc.select(c, SelectMode::Toggle);
    CHECK(doc.isSelected(c) && doc.selection().size() == 2);
    doc.select(EntityId{12345}); // Unknown ids are ignored.
    CHECK(doc.selection().empty());
    doc.selectAll();
    CHECK(doc.selection().size() == 3);
    doc.clearSelection();
    CHECK(doc.selection().empty());

    // Selecting a parent and its child moves the parent only.
    doc.change("nest", [&](Scene &scene) { scene.setParent(b, a); });
    doc.select(std::vector<EntityId>{b, a, c});
    CHECK(doc.selectionRoots() == std::vector<EntityId>({a, c}));

    // Undo restores what was selected when the change began.
    doc.select(a);
    doc.rename(a, "Renamed");
    doc.select(c);
    CHECK(doc.undo() && doc.primary() == a);
}

void propertiesAndComponents() {
    Fixture f;
    EditorDocument &doc = f.doc();
    const EntityId id = f.box("Box", {0, 0}, {2, 1});
    const std::size_t sprite = componentIndex(f.get(id), "SpriteRenderer");
    const std::size_t collider = componentIndex(f.get(id), "Collider");
    CHECK(doc.setProperty(id, sprite, "size", Vec2{4.0F, 2.0F}));
    CHECK(f.get(id).get<SpriteRenderer>()->size == Vec2({4.0F, 2.0F}) &&
          doc.undoLabel() == "Edit Size");
    const std::size_t depth = doc.undoDepth();
    CHECK(!doc.setProperty(id, sprite, "size", std::string("wide"))); // Wrong type.
    CHECK(!doc.setProperty(id, sprite, "nonsense", 1.0));
    CHECK(!doc.setProperty(id, 99, "size", Vec2{1, 1}));
    CHECK(!doc.setProperty(EntityId{999}, 0, "size", Vec2{1, 1}));
    CHECK(doc.undoDepth() == depth); // Rejected edits are not history.
    CHECK(doc.setProperty(id, sprite, "layer", std::int64_t{5000})); // Clamped to the range.
    CHECK(f.get(id).get<SpriteRenderer>()->layer == 1000);
    CHECK(doc.setProperty(id, collider, "isTrigger", true) && f.get(id).get<Collider>()->isTrigger);
    CHECK(doc.undo() && !f.get(id).get<Collider>()->isTrigger);

    // Adding a component brings its dependencies and runs its defaults.
    const EntityId doorId = doc.createEntity("Gate");
    CHECK(doc.addComponent(doorId, "Door"));
    const Entity &door = f.get(doorId);
    CHECK(door.has<Door>() && door.has<RigidBody>() && door.has<Collider>());
    CHECK(door.get<RigidBody>()->type == RigidBodyType::Kinematic);
    CHECK(!doc.addComponent(doorId, "NoSuchComponent"));
    const std::size_t colliderIndex = componentIndex(door, "Collider");
    const auto blocked = doc.removeComponent(doorId, colliderIndex); // The door needs it.
    CHECK(!blocked && blocked.error().find("Door") != std::string::npos);
    CHECK(f.get(doorId).has<Collider>());
    CHECK(doc.removeComponent(doorId, componentIndex(f.get(doorId), "Door")));
    CHECK(doc.removeComponent(doorId, componentIndex(f.get(doorId), "Collider")));
    CHECK(!f.get(doorId).has<Collider>() && f.get(doorId).has<RigidBody>());
    CHECK(doc.undo() && f.get(doorId).has<Collider>() && !f.get(doorId).has<Door>());
    CHECK(!doc.removeComponent(doorId, 42));

    doc.setEntityActive(doorId, false);
    CHECK(!f.get(doorId).active() && doc.undoLabel() == "Deactivate");
}

void creatingEntities() {
    Fixture f;
    EditorDocument &doc = f.doc();
    const auto first = doc.createFromTemplate(templateNamed(f.registry, "Platform"), {4.0F, 5.0F});
    const auto second = doc.createFromTemplate(templateNamed(f.registry, "Platform"), {8.0F, 5.0F});
    CHECK(first && second && doc.primary() == second.value());
    CHECK(f.get(first.value()).name() == "Platform" &&
          f.get(second.value()).name() == "Platform (1)");
    CHECK_NEAR(f.get(first.value()).worldPosition().x, 4.0);
    CHECK(f.get(first.value()).has<Collider>() && doc.undoLabel() == "Create Platform");
    const auto third = doc.createFromTemplate(templateNamed(f.registry, "Platform"), {0, 0});
    CHECK(f.get(third.value()).name() == "Platform (2)");
    CHECK(doc.createEntity("Platform").value != 0 && doc.primary() != third.value());

    // Templates can create straight into a group and keep their world position.
    const EntityId group = doc.createEntity("Group", {}, Vec2{100.0F, 0.0F});
    const auto child =
        doc.createFromTemplate(templateNamed(f.registry, "Crate"), {102.0F, 1.0F}, group);
    CHECK(child && f.get(child.value()).parentId() == group);
    CHECK_NEAR(f.get(child.value()).worldPosition().x, 102.0);
    CHECK_NEAR(f.get(child.value()).transform().position.x, 2.0);
    CHECK(doc.undo() && !doc.scene().find(child.value()));

    // A prefab from another scene arrives with fresh ids at the requested place.
    Fixture other;
    const auto lava =
        other.doc().createFromTemplate(templateNamed(other.registry, "Hazard"), {0, 0});
    const Json prefab = subtreeToJson(other.doc().scene(), lava.value());
    const auto placed = doc.instantiatePrefab(prefab, {7.0F, 7.0F});
    CHECK(placed && placed.value() != lava.value() && doc.primary() == placed.value());
    CHECK_NEAR(f.get(placed.value()).worldPosition().y, 7.0);
    CHECK(!doc.instantiatePrefab(Json::object(), {0, 0}));
}

void duplicating() {
    Fixture f;
    EditorDocument &doc = f.doc();
    const EntityId group = doc.createEntity("Group", {}, Vec2{10.0F, 0.0F});
    const EntityId part = f.box("Part", {12.0F, 0.0F}, {1, 1});
    doc.change("nest", [&](Scene &scene) { scene.setParent(part, group, {}, true); });
    const EntityId later = doc.createEntity("Later");

    doc.select(group);
    const auto copies = doc.duplicateSelection();
    CHECK(copies.size() == 1 && doc.selection() == copies);
    const Entity &copy = f.get(copies[0]);
    CHECK(copy.name() == "Group (1)" && copy.childIds().size() == 1);
    CHECK_NEAR(copy.worldPosition().x, 10.5);
    CHECK_NEAR(copy.worldPosition().y, 0.5);
    CHECK(f.get(copy.childIds()[0]).name() == "Part"); // Only the root is renamed.
    CHECK_NEAR(f.get(copy.childIds()[0]).worldPosition().x, 12.5);
    CHECK(f.get(part).worldPosition().x == 12.0F); // The original stays.
    const auto &roots = doc.scene().roots();       // The copy sits right after its original.
    CHECK(roots.size() == 3 && roots[0] == group && roots[1] == copies[0] && roots[2] == later);
    CHECK(doc.undoLabel() == "Duplicate");
    doc.duplicateSelection();
    CHECK(f.get(doc.primary()).name() == "Group (2)");
    CHECK(doc.undo() && doc.undo() && doc.scene().size() == 3);

    // A copied plate still points at the door it opened.
    const auto plate = doc.createFromTemplate(templateNamed(f.registry, "Pressure Plate"), {0, 5});
    const auto gate = doc.createFromTemplate(templateNamed(f.registry, "Door"), {5, 5});
    const std::size_t plateComponent = componentIndex(f.get(plate.value()), "PressurePlate");
    CHECK(doc.setProperty(plate.value(), plateComponent, "targets",
                          std::vector<EntityId>{gate.value()}));
    doc.select(plate.value());
    const auto plateCopy = doc.duplicateSelection();
    CHECK(plateCopy.size() == 1 &&
          f.get(plateCopy[0]).get<PressurePlate>()->targets == std::vector<EntityId>{gate.value()});
    doc.clearSelection();
    CHECK(doc.duplicateSelection().empty());
}

void deleting() {
    Fixture f;
    EditorDocument &doc = f.doc();
    const auto plate = doc.createFromTemplate(templateNamed(f.registry, "Pressure Plate"), {0, 5});
    const auto gate = doc.createFromTemplate(templateNamed(f.registry, "Door"), {5, 5});
    const EntityId group = doc.createEntity("Group");
    const EntityId inner = f.box("Inner", {1, 1}, {1, 1});
    doc.change("nest", [&](Scene &scene) { scene.setParent(inner, group); });
    const std::size_t plateComponent = componentIndex(f.get(plate.value()), "PressurePlate");
    doc.setProperty(plate.value(), plateComponent, "targets",
                    std::vector<EntityId>{gate.value(), inner});

    doc.select(std::vector<EntityId>{gate.value(), group});
    doc.deleteSelection();
    CHECK(!doc.scene().find(gate.value()) && !doc.scene().find(group) && !doc.scene().find(inner));
    CHECK(doc.scene().size() == 1 && doc.selection().empty());
    // Nothing is left pointing at the deleted entities.
    CHECK(f.get(plate.value()).get<PressurePlate>()->targets.empty());
    // Undo brings back the entities and the references.
    CHECK(doc.undo() && doc.scene().size() == 4);
    CHECK(f.get(plate.value()).get<PressurePlate>()->targets ==
          std::vector<EntityId>({gate.value(), inner}));
    CHECK(f.get(inner).parentId() == group);
    CHECK(doc.redoLabel() == "Delete Entities");
    doc.clearSelection();
    doc.deleteSelection(); // No selection: nothing to do, and the redo history is untouched.
    CHECK(doc.canRedo() && doc.redoLabel() == "Delete Entities");
}

void hierarchyEdits() {
    Fixture f;
    EditorDocument &doc = f.doc();
    const EntityId parent = doc.createEntity("Parent", {}, Vec2{10.0F, 0.0F});
    const EntityId child = f.box("Child", {12.0F, 0.0F}, {1, 1});
    const EntityId second = f.box("Second", {20.0F, 0.0F}, {1, 1});
    CHECK(doc.reparent(child, parent));
    CHECK(f.get(child).parentId() == parent);
    CHECK_NEAR(f.get(child).worldPosition().x, 12.0); // Reparenting does not move it.
    CHECK_NEAR(f.get(child).transform().position.x, 2.0);
    const std::size_t depth = doc.undoDepth();
    CHECK(!doc.reparent(parent, child)); // Would create a cycle.
    CHECK(doc.undoDepth() == depth);
    CHECK(doc.reparent(child, EntityId{}, 0)); // Back to the top, first in the list.
    CHECK(doc.scene().roots().front() == child && f.get(child).parentId() == EntityId{});

    CHECK(doc.moveAmongSiblings(child, 2));
    CHECK(doc.scene().roots().back() == child);
    CHECK(doc.moveAmongSiblings(child, 50) && doc.scene().roots().back() == child); // Clamped.
    CHECK(doc.moveAmongSiblings(child, -1) && doc.scene().roots()[1] == child);
    CHECK(!doc.moveAmongSiblings(EntityId{77}, 1));
    (void)second;
}

void clipboard() {
    Fixture f;
    EditorDocument &doc = f.doc();
    const auto plate = doc.createFromTemplate(templateNamed(f.registry, "Pressure Plate"), {2, 2});
    const auto gate = doc.createFromTemplate(templateNamed(f.registry, "Door"), {8, 2});
    doc.setProperty(plate.value(), componentIndex(f.get(plate.value()), "PressurePlate"), "targets",
                    std::vector<EntityId>{gate.value()});
    doc.select(std::vector<EntityId>{plate.value(), gate.value()});
    const Json clip = doc.copySelection();
    CHECK(clip.get("format").asString() == EditorDocument::clipboardFormat &&
          clip.get("items").size() == 2);

    // Pasting in place offsets from the originals; references to non-copied entities are kept.
    const auto same = doc.paste(clip);
    CHECK(same && same.value().size() == 2 && doc.scene().size() == 4);
    CHECK_NEAR(f.get(same.value()[0]).worldPosition().x, 2.5);
    CHECK(f.get(same.value()[0]).name() == "Pressure Plate (1)");
    // Both were copied together, so the copied plate targets the copied door.
    CHECK(f.get(same.value()[0]).get<PressurePlate>()->targets ==
          std::vector<EntityId>{same.value()[1]});
    CHECK(doc.selection() == same.value());
    CHECK(doc.undo() && doc.scene().size() == 2);

    // Through text into another scene, centered where asked.
    auto parsed = Json::parse(clip.dump());
    CHECK(parsed);
    Fixture other;
    const auto moved = other.doc().paste(parsed.value(), {}, Vec2{50.0F, 20.0F});
    CHECK(moved && moved.value().size() == 2);
    const Vec2 first = other.get(moved.value()[0]).worldPosition();
    const Vec2 last = other.get(moved.value()[1]).worldPosition();
    CHECK_NEAR((first.x + last.x) / 2.0F, 50.0);
    CHECK_NEAR((first.y + last.y) / 2.0F, 20.0);
    CHECK_NEAR(last.x - first.x, 6.0); // Relative layout survives.
    CHECK(other.get(moved.value()[0]).get<PressurePlate>()->targets ==
          std::vector<EntityId>{moved.value()[1]});
    // A door pasted alone, without the plate, does not gain references from thin air.
    other.doc().select(moved.value()[0]);
    const auto lonely = other.doc().paste(other.doc().copySelection(), {}, Vec2{0, 0});
    CHECK(lonely);

    CHECK(!doc.paste(Json::object()));
    Json empty = clip;
    empty.set("items", Json::array());
    CHECK(!doc.paste(empty));
    Json bad = Json::object();
    bad.set("format", EditorDocument::clipboardFormat);
    Json items = Json::array();
    Json item = Json::object();
    item.set("prefab", Json::object());
    items.push(item);
    bad.set("items", items);
    const std::size_t sizeBefore = doc.scene().size();
    CHECK(!doc.paste(bad) && doc.scene().size() == sizeBefore); // Failure leaves nothing behind.
}

// ---------------------------------------------------------------------------------------------
void geometry() {
    Fixture f;
    EditorDocument &doc = f.doc();
    const EntityId a = f.box("A", {0, 0}, {2, 1});
    const auto extents = extentsOf(f.get(a));
    CHECK(extents.size() == 2 && extents[0].visual && !extents[1].visual);
    CHECK_NEAR(extents[0].local.position.x, -1.0);
    CHECK_NEAR(extents[0].local.size.y, 1.0);

    // Circle colliders are as wide as they are tall.
    doc.change("circle", [&](Scene &scene) {
        auto *collider = scene.find(a)->get<Collider>();
        collider->shape = ColliderShape::Circle;
        collider->size = {2.0F, 1.0F};
    });
    CHECK_NEAR(extentsOf(f.get(a))[1].local.size.y, 2.0);
    CHECK(extentsOf(f.get(a))[1].ellipse);

    // Screen-space UI has no place in the world.
    const EntityId hud = doc.createEntity("Hud");
    doc.change("ui", [&](Scene &scene) { scene.find(hud)->add<UiPanel>(); });
    CHECK(extentsOf(f.get(hud)).empty() && !gizmoBox(f.get(hud)));
    const OrientedBox marker = displayBox(f.get(hud), 0.4F);
    CHECK_NEAR(marker.half.x, 0.4);

    // Rotation and scale carry into the world box.
    const EntityId r = f.box("R", {10, 0}, {4, 2});
    doc.change("rotate", [&](Scene &scene) {
        Entity *entity = scene.find(r);
        entity->transform().rotationDegrees = 90.0F;
        entity->transform().scale = {2.0F, 1.0F};
    });
    const auto rotated = gizmoBox(f.get(r));
    CHECK(rotated);
    CHECK_NEAR(rotated->center.x, 10.0);
    CHECK_NEAR(rotated->half.x, 4.0);
    CHECK_NEAR(rotated->half.y, 1.0);
    const Rect bounds = rotated->bounds();
    CHECK_NEAR(bounds.size.x, 2.0, 1e-3);
    CHECK_NEAR(bounds.size.y, 8.0, 1e-3);
    CHECK(rotated->contains({10.0F, 3.5F}) && !rotated->contains({12.0F, 0.0F}));

    CHECK_NEAR(snapTo(1.26F, 0.5F), 1.5);
    CHECK_NEAR(snapTo(-0.2F, 0.5F), 0.0);
    CHECK_NEAR(snapTo(3.3F, 0.0F), 3.3);
    CHECK(axisAligned(0) && axisAligned(90) && axisAligned(-180) && !axisAligned(45));
}

void picking() {
    Fixture f;
    EditorDocument &doc = f.doc();
    const EntityId back = f.box("Back", {0, 0}, {6, 6});
    const EntityId front = f.box("Front", {0, 0}, {2, 2});
    doc.change("layers", [&](Scene &scene) {
        scene.find(back)->get<SpriteRenderer>()->layer = 0;
        scene.find(front)->get<SpriteRenderer>()->layer = 3;
    });
    auto stack = pickAll(doc.scene(), {0, 0}, 0.02F);
    CHECK(stack.size() == 2 && stack[0] == front && stack[1] == back); // Topmost first.
    stack = pickAll(doc.scene(), {2.5F, 0}, 0.02F);
    CHECK(stack.size() == 1 && stack[0] == back);
    CHECK(pickAll(doc.scene(), {9, 9}, 0.02F).empty());

    // A hidden sprite stays selectable through its collider, but never beats a visible one.
    doc.change("hide",
               [&](Scene &scene) { scene.find(front)->get<SpriteRenderer>()->visible = false; });
    stack = pickAll(doc.scene(), {0, 0}, 0.02F);
    CHECK(stack.size() == 2 && stack[0] == back && stack[1] == front);

    // Round shapes are not hit in their corners.
    const EntityId ball = f.box("Ball", {20, 0}, {2, 2});
    doc.change("ball", [&](Scene &scene) {
        scene.find(ball)->get<SpriteRenderer>()->shape = SpriteShape::Ellipse;
        scene.find(ball)->get<Collider>()->shape = ColliderShape::Circle;
    });
    CHECK(pickAll(doc.scene(), {20.9F, 0.9F}, 0.02F)
              .empty()); // Inside the square, outside the circle.
    CHECK(pickAll(doc.scene(), {20.0F, 0.0F}, 0.02F).front() == ball);

    // Extent-less entities can be picked by their marker, sized in screen pixels.
    const EntityId empty = doc.createEntity("Logic", {}, Vec2{40.0F, 0.0F});
    CHECK(pickAll(doc.scene(), {40.2F, 0.1F}, 0.02F).front() == empty);
    CHECK(pickAll(doc.scene(), {41.5F, 0.0F}, 0.02F).empty());
    CHECK(!pickAll(doc.scene(), {41.5F, 0.0F}, 0.2F).empty()); // Zoomed out: bigger in the world.

    // A small trigger beats a large one that also contains the point.
    const auto bigZone = doc.createFromTemplate(templateNamed(f.registry, "Trigger Zone"), {60, 0});
    const auto smallZone = doc.createFromTemplate(templateNamed(f.registry, "Lever"), {60, 0});
    doc.change("hide zone", [&](Scene &scene) {
        scene.find(bigZone.value())->get<SpriteRenderer>()->visible = false;
        scene.find(smallZone.value())->get<SpriteRenderer>()->visible = false;
    });
    CHECK(pickAll(doc.scene(), {60, 0}, 0.02F).front() == smallZone.value());

    // Marquee.
    const auto inRect = pickInRect(doc.scene(), Rect{{-1, -1}, {3, 3}}, 0.02F);
    CHECK(inRect.size() == 2); // back and front overlap; nothing else does.
    CHECK(pickInRect(doc.scene(), Rect{{100, 100}, {1, 1}}, 0.02F).empty());
    const auto total = boundsOf(doc.scene(), {back, ball}, 0.02F);
    CHECK(total && total->position.x < -2.9F && total->position.x + total->size.x > 20.9F);
    CHECK(!boundsOf(doc.scene(), {}, 0.02F));
}

void linksAndGhosts() {
    Fixture f;
    EditorDocument &doc = f.doc();
    const auto plate = doc.createFromTemplate(templateNamed(f.registry, "Pressure Plate"), {0, 5});
    const auto gate = doc.createFromTemplate(templateNamed(f.registry, "Door"), {6, 3});
    const auto lever = doc.createFromTemplate(templateNamed(f.registry, "Lever"), {2, 5});
    doc.setProperty(plate.value(), componentIndex(f.get(plate.value()), "PressurePlate"), "targets",
                    std::vector<EntityId>{gate.value()});
    doc.setProperty(lever.value(), componentIndex(f.get(lever.value()), "Lever"), "targets",
                    std::vector<EntityId>{gate.value(), EntityId{424242}});
    const auto out = linksFrom(doc.scene(), plate.value());
    CHECK(out.size() == 1 && out[0].to == gate.value() && out[0].component == "PressurePlate" &&
          out[0].property == "targets");
    CHECK(linksFrom(doc.scene(), lever.value()).size() == 1); // The dangling one is skipped.
    CHECK(linksTo(doc.scene(), gate.value()).size() == 2);
    CHECK(allLinks(doc.scene()).size() == 2);
    CHECK(linksFrom(doc.scene(), EntityId{5}).empty());

    const auto ghosts = displacementGhosts(f.get(gate.value()), 0.3F);
    CHECK(ghosts.size() == 1 && ghosts[0].property == "openOffset");
    CHECK_NEAR(ghosts[0].displacement.y, -3.0);
    CHECK_NEAR(ghosts[0].box.center.y, 0.0, 1e-3); // Door center y=3 moved up by 3.
    CHECK(displacementGhosts(f.get(plate.value()), 0.3F).empty());
}

// ---------------------------------------------------------------------------------------------
struct View {
    Fixture f;
    SceneInteraction ui;
    View() : ui(&f.doc()) {
        ui.viewport = {800.0F, 600.0F};
        ui.camera.center = {0.0F, 0.0F};
        ui.camera.zoom = 40.0F;
        ui.snap.grid = 0.5F;
    }
    EditorDocument &doc() {
        return f.doc();
    }
    Vec2 px(Vec2 world) const {
        return ui.toScreen(world);
    }
    void click(Vec2 world, Modifiers modifiers = {}) {
        ui.pointerPressed(px(world), modifiers);
        ui.pointerReleased(px(world), modifiers);
    }
    void drag(Vec2 from, Vec2 to, Modifiers modifiers = {}) {
        ui.pointerPressed(px(from), modifiers);
        ui.pointerMoved(px(lerp(from, to, 0.5F)), modifiers);
        ui.pointerMoved(px(to), modifiers);
        ui.pointerReleased(px(to), modifiers);
    }
    const Entity &get(EntityId id) {
        return f.get(id);
    }
};

void viewCamera() {
    ViewCamera camera;
    camera.center = {5.0F, -2.0F};
    camera.zoom = 50.0F;
    const Vec2 viewport{800, 600};
    CHECK(camera.toScreen({5.0F, -2.0F}, viewport) == Vec2({400.0F, 300.0F}));
    const Vec2 world{7.25F, 1.5F};
    const Vec2 back = camera.toWorld(camera.toScreen(world, viewport), viewport);
    CHECK_NEAR(back.x, 7.25);
    CHECK_NEAR(back.y, 1.5);
    const Vec2 under = camera.toWorld({600, 100}, viewport);
    camera.zoomAt({600, 100}, viewport, 2.0F); // The point under the cursor stays put.
    CHECK_NEAR(camera.zoom, 100.0);
    const Vec2 after = camera.toWorld({600, 100}, viewport);
    CHECK_NEAR(after.x, under.x, 1e-3);
    CHECK_NEAR(after.y, under.y, 1e-3);
    camera.zoomAt({0, 0}, viewport, 1000.0F);
    CHECK_NEAR(camera.zoom, ViewCamera::maxZoom);
    camera.zoomAt({0, 0}, viewport, 1e-6F);
    CHECK_NEAR(camera.zoom, ViewCamera::minZoom);
    camera.frame({{0, 0}, {40, 20}}, viewport, 1.0F);
    CHECK_NEAR(camera.center.x, 20.0);
    CHECK_NEAR(camera.zoom, 20.0);                  // 800 px across 40 m.
    camera.frame({{0, 0}, {0, 0}}, viewport, 1.0F); // A point: no division by zero.
    CHECK(std::isfinite(camera.zoom) && camera.zoom <= ViewCamera::maxZoom);

    View view;
    view.ui.panBy({40.0F, 0.0F}); // Dragging the view right shows what is to the left.
    CHECK_NEAR(view.ui.camera.center.x, -1.0);
    view.f.box("Far", {100, 50}, {2, 2});
    view.ui.frameAll();
    CHECK(view.ui.camera.center.x > 40.0F);
    const Vec2 topLeft = view.ui.toScreen({99.0F, 49.0F});
    CHECK(topLeft.x > 0 && topLeft.x < 800 && topLeft.y > 0 && topLeft.y < 600);
    view.doc().select(view.doc().scene().roots().front());
    view.ui.frameSelection();
    CHECK_NEAR(view.ui.camera.center.x, 100.0, 0.5);
}

void clickAndMarquee() {
    View v;
    const EntityId a = v.f.box("A", {0, 0}, {2, 1});
    const EntityId b = v.f.box("B", {5, 0}, {2, 1});
    const EntityId c = v.f.box("C", {10, 0}, {2, 1});
    v.click({0, 0});
    CHECK(v.doc().selection() == std::vector<EntityId>({a}));
    v.click({5, 0}, {.shift = true});
    CHECK(v.doc().selection() == std::vector<EntityId>({a, b}));
    v.click({0, 0}, {.ctrl = true}); // Toggling a selected item off.
    CHECK(v.doc().selection() == std::vector<EntityId>({b}));
    v.click({10, 0}, {.ctrl = true});
    CHECK(v.doc().selection() == std::vector<EntityId>({b, c}));
    v.click({5, 0}); // A plain click on a member of a multi-selection selects just it.
    CHECK(v.doc().selection() == std::vector<EntityId>({b}));
    v.click({0, 5}); // Empty space.
    CHECK(v.doc().selection().empty());
    v.click({0, 0});
    v.click({0, 5}, {.shift = true}); // Shift keeps the selection on a miss.
    CHECK(v.doc().selection().size() == 1);
    CHECK(v.doc().undoDepth() == 3); // Selecting never adds undo steps (the three box creations).

    // Marquee: from empty space, drag over A and B but not C.
    v.ui.pointerPressed(v.px({-2, -3}), {});
    v.ui.pointerMoved(v.px({3, 0}), {});
    CHECK(v.ui.marquee() && v.ui.dragging());
    v.ui.pointerMoved(v.px({6.5F, 3}), {});
    const Rect area = *v.ui.marquee();
    CHECK_NEAR(area.position.x, -2.0);
    CHECK_NEAR(area.size.x, 8.5);
    v.ui.pointerReleased(v.px({6.5F, 3}), {});
    CHECK(!v.ui.marquee() && v.doc().selection() == std::vector<EntityId>({a, b}));
    // Shift+marquee adds.
    v.ui.pointerPressed(v.px({9, -2}), {.shift = true});
    v.ui.pointerMoved(v.px({13, 2}), {.shift = true});
    v.ui.pointerReleased(v.px({13, 2}), {.shift = true});
    CHECK(v.doc().selection().size() == 3 && v.doc().isSelected(c));

    // Alt-click cycles through overlapping entities.
    View w;
    const EntityId low = w.f.box("Low", {0, 0}, {4, 4});
    const EntityId high = w.f.box("High", {0, 0}, {2, 2});
    w.doc().change("layer",
                   [&](Scene &scene) { scene.find(high)->get<SpriteRenderer>()->layer = 2; });
    w.click({0, 0});
    CHECK(w.doc().primary() == high);
    w.click({0, 0}, {.alt = true});
    CHECK(w.doc().primary() == low);
    w.click({0, 0}, {.alt = true});
    CHECK(w.doc().primary() == high);

    // Hovering reports what a click would pick.
    w.ui.pointerMoved(w.px({1.5F, 1.5F}), {});
    CHECK(w.ui.hoveredEntity() == low && w.ui.hovered().kind == HandleKind::None);
    w.ui.pointerMoved(w.px({30, 30}), {});
    CHECK(!w.ui.hoveredEntity());
    CHECK(w.ui.pickAt(w.px({0, 0})) == high);
}

void movingThings() {
    View v;
    const EntityId a = v.f.box("A", {0, 0}, {2, 1});
    v.click({0, 0});
    const std::size_t depth = v.doc().undoDepth();
    v.drag({0, 0}, {1.0F, 0.0F});
    CHECK_NEAR(v.get(a).worldPosition().x, 1.0);
    CHECK(v.doc().undoDepth() == depth + 1 && v.doc().undoLabel() == "Move");
    CHECK(v.doc().undo() && v.get(a).worldPosition().x == 0.0F);
    v.doc().redo();

    // Snapping puts the dragged object on the grid; Ctrl turns it off for the drag.
    v.drag({1.0F, 0.0F}, {1.3F, 0.55F});
    CHECK_NEAR(v.get(a).worldPosition().x, 1.5);
    CHECK_NEAR(v.get(a).worldPosition().y, 0.5);
    v.drag({1.5F, 0.5F}, {1.8F, 0.9F}, {.ctrl = true});
    CHECK_NEAR(v.get(a).worldPosition().x, 1.8);
    CHECK_NEAR(v.get(a).worldPosition().y, 0.9);
    v.ui.snap.enabled = false;
    v.drag({1.8F, 0.9F}, {2.13F, 0.9F});
    CHECK_NEAR(v.get(a).worldPosition().x, 2.13, 0.03);
    v.ui.snap.enabled = true;
    // Shift constrains to the dominant axis.
    v.doc().change("reset", [&](Scene &scene) { scene.find(a)->setWorldPosition({0, 0}); });
    v.drag({0, 0}, {3.0F, 1.0F}, {.shift = true});
    CHECK_NEAR(v.get(a).worldPosition().x, 3.0);
    CHECK_NEAR(v.get(a).worldPosition().y, 0.0);

    // A press with no real movement is a click: nothing moves and nothing is recorded.
    const std::size_t before = v.doc().undoDepth();
    v.ui.pointerPressed(v.px({3, 0}), {});
    v.ui.pointerMoved(v.px({3.02F, 0.02F}), {});
    v.ui.pointerReleased(v.px({3.02F, 0.02F}), {});
    CHECK(v.doc().undoDepth() == before && v.get(a).worldPosition().x == 3.0F);

    // Escape puts everything back.
    v.ui.pointerPressed(v.px({3, 0}), {});
    v.ui.pointerMoved(v.px({6, 4}), {});
    CHECK(v.get(a).worldPosition().x > 5.0F && v.ui.dragging());
    v.ui.cancelDrag();
    CHECK(v.get(a).worldPosition().x == 3.0F && !v.ui.dragging() && v.doc().undoDepth() == before);
    v.ui.pointerReleased(v.px({6, 4}), {}); // The late release is harmless.
    CHECK(v.doc().undoDepth() == before && !v.doc().inChange());

    // Dragging an unselected object selects it and moves only it.
    const EntityId b = v.f.box("B", {10, 0}, {2, 1});
    v.drag({10, 0}, {12, 0});
    CHECK(v.doc().selection() == std::vector<EntityId>({b}) && v.get(b).worldPosition().x == 12.0F);
    CHECK(v.get(a).worldPosition().x == 3.0F);

    // Moving a selection moves each root once, even when a child is selected with its parent.
    View w;
    const EntityId parent = w.f.box("Parent", {0, 0}, {2, 2});
    const EntityId child = w.f.box("Child", {4, 0}, {2, 2});
    const EntityId other = w.f.box("Other", {0, 6}, {2, 2});
    w.doc().change("nest", [&](Scene &scene) { scene.setParent(child, parent, {}, true); });
    w.doc().select(std::vector<EntityId>{parent, child, other});
    w.drag({0, 0}, {1, 0});
    CHECK_NEAR(w.get(parent).worldPosition().x, 1.0);
    CHECK_NEAR(w.get(child).worldPosition().x, 5.0); // Not 6: no double move.
    CHECK_NEAR(w.get(other).worldPosition().x, 1.0);
    CHECK(w.doc().undo() && w.get(other).worldPosition().x == 0.0F &&
          w.get(child).worldPosition().x == 4.0F);

    // Arrow keys nudge by a grid step (ten with the large flag) as one undoable edit.
    w.doc().select(other);
    w.ui.nudge({1, 0}, false);
    CHECK_NEAR(w.get(other).worldPosition().x, 0.5);
    w.ui.nudge({0, -1}, true);
    CHECK_NEAR(w.get(other).worldPosition().y, 1.0);
    CHECK(w.doc().undoLabel() == "Nudge");

    // Rebinding to another document abandons a drag in progress.
    w.ui.pointerPressed(w.px({0.5F, 1.0F}), {});
    w.ui.pointerMoved(w.px({3, 3}), {});
    CHECK(w.doc().inChange());
    Fixture second;
    w.ui.bind(&second.doc());
    CHECK(!w.doc().inChange() && !w.ui.dragging());
    // Without a document nothing happens, and nothing crashes.
    w.ui.bind(nullptr);
    w.ui.pointerPressed({1, 1}, {});
    w.ui.pointerMoved({5, 5}, {});
    w.ui.pointerReleased({5, 5}, {});
    w.ui.nudge({1, 0}, false);
    w.ui.frameAll();
    CHECK(!w.ui.bound() && w.ui.handles().empty() && !w.ui.pickAt({1, 1}));
}

void resizing() {
    View v;
    const EntityId a = v.f.box("A", {0, 0}, {2, 1});
    v.doc().select(a);
    v.ui.tool = Tool::Resize;
    const auto handles = v.ui.handles();
    CHECK(handles.size() == 8);
    bool foundRight = false;
    for (const Handle &handle : handles)
        if (handle.kind == HandleKind::Right) {
            foundRight = true;
            CHECK_NEAR(handle.screen.x, v.px({1, 0}).x, 1e-3);
        }
    CHECK(foundRight);

    // Drag the right edge from x=1 to x=3: the left edge stays at -1, and the origin follows the
    // geometry (it stays in the middle of the box).
    const std::size_t depth = v.doc().undoDepth();
    v.drag({1.0F, 0.0F}, {3.0F, 0.0F});
    const auto *sprite = v.get(a).get<SpriteRenderer>();
    const auto *collider = v.get(a).get<Collider>();
    CHECK_NEAR(sprite->size.x, 4.0);
    CHECK_NEAR(sprite->size.y, 1.0);
    CHECK(collider->size == sprite->size); // Everything with a size scales together.
    CHECK_NEAR(v.get(a).worldPosition().x, 1.0);
    const auto box = gizmoBox(v.get(a));
    CHECK_NEAR(box->center.x - box->half.x, -1.0, 1e-3);
    CHECK_NEAR(box->center.x + box->half.x, 3.0, 1e-3);
    CHECK(v.doc().undoDepth() == depth + 1 && v.doc().undoLabel() == "Resize");
    CHECK(v.doc().undo() && v.get(a).get<SpriteRenderer>()->size.x == 2.0F &&
          v.get(a).worldPosition().x == 0.0F);
    v.doc().redo();

    // Left edge, snapped to the grid (x=-1.2 lands on -1.0 after the left edge was at -1).
    v.drag({-1.0F, 0.0F}, {-2.1F, 0.0F});
    const auto grown = gizmoBox(v.get(a));
    CHECK_NEAR(grown->center.x - grown->half.x, -2.0, 1e-3);
    CHECK_NEAR(grown->center.x + grown->half.x, 3.0, 1e-3); // The far edge did not move.

    // A drag past the opposite edge stops at a minimum size instead of inverting.
    v.drag({-2.0F, 0.0F}, {10.0F, 0.0F});
    CHECK(v.get(a).get<SpriteRenderer>()->size.x >= 0.05F - 1e-4F);
    CHECK(gizmoBox(v.get(a))->half.x > 0.0F);
    v.doc().undo();

    // Top edge and a corner. Corner + Shift keeps the proportions.
    View w;
    const EntityId b = w.f.box("B", {0, 0}, {4, 2});
    w.doc().select(b);
    w.ui.tool = Tool::Resize;
    w.drag({0.0F, -1.0F}, {0.0F, -2.0F}); // Top edge up by one.
    CHECK_NEAR(w.get(b).get<SpriteRenderer>()->size.y, 3.0);
    CHECK_NEAR(w.get(b).get<SpriteRenderer>()->size.x, 4.0);
    CHECK_NEAR(w.get(b).worldPosition().y, -0.5);
    w.doc().undo();
    w.drag({2.0F, 1.0F}, {4.0F, 3.0F}, {.shift = true}); // Bottom-right: both grow to 6 x 4 -> f=2.
    CHECK_NEAR(w.get(b).get<SpriteRenderer>()->size.x, 8.0);
    CHECK_NEAR(w.get(b).get<SpriteRenderer>()->size.y, 4.0);
    w.doc().undo();
    // Alt resizes from the center.
    w.drag({2.0F, 0.0F}, {3.0F, 0.0F}, {.alt = true});
    CHECK_NEAR(w.get(b).get<SpriteRenderer>()->size.x, 6.0);
    CHECK_NEAR(w.get(b).worldPosition().x, 0.0);
    w.doc().undo();

    // Offsets scale with sizes, and children stay where they are in the world.
    View x;
    const auto plate =
        x.doc().createFromTemplate(templateNamed(x.f.registry, "Pressure Plate"), {0, 0});
    const EntityId mark = x.doc().createEntity("Mark", plate.value(), Vec2{0.4F, -1.0F});
    x.doc().select(plate.value());
    x.ui.tool = Tool::Resize;
    const float plateWidth = x.get(plate.value()).get<SpriteRenderer>()->size.x;
    const float plateHalf = plateWidth / 2.0F;
    const Vec2 markBefore = x.get(mark).worldPosition();
    const float colliderOffsetBefore = x.get(plate.value()).get<Collider>()->offset.y;
    x.ui.snap.enabled = false;
    x.drag({plateHalf, 0.0F}, {plateHalf + 1.2F, 0.0F});
    const float factor = (plateWidth + 1.2F) / plateWidth;
    CHECK_NEAR(x.get(plate.value()).get<SpriteRenderer>()->size.x, plateWidth * factor, 0.03);
    CHECK_NEAR(x.get(plate.value()).get<Collider>()->offset.y, colliderOffsetBefore,
               1e-4); // y unscaled.
    CHECK_NEAR(x.get(mark).worldPosition().x, markBefore.x, 1e-3);
    CHECK_NEAR(x.get(mark).worldPosition().y, markBefore.y, 1e-3);
    x.ui.snap.enabled = true;

    // Rotated entities resize along their own axes.
    View r;
    const EntityId tilted = r.f.box("Tilted", {0, 0}, {4, 2});
    r.doc().change("rotate",
                   [&](Scene &scene) { scene.find(tilted)->transform().rotationDegrees = 90.0F; });
    r.doc().select(tilted);
    r.ui.tool = Tool::Resize;
    r.drag({0.0F, 2.0F}, {0.0F, 4.0F}); // Local right edge (world down) from 2 to 4.
    CHECK_NEAR(r.get(tilted).get<SpriteRenderer>()->size.x, 6.0);
    CHECK_NEAR(r.get(tilted).get<SpriteRenderer>()->size.y, 2.0);
    CHECK_NEAR(r.get(tilted).worldPosition().x, 0.0, 1e-3);
    CHECK_NEAR(r.get(tilted).worldPosition().y, 1.0, 1e-3);

    // Mirrored entities: the handle on screen's right still grows the side it is on.
    View m;
    const EntityId mirrored = m.f.box("Mirrored", {0, 0}, {2, 2});
    m.doc().change("mirror",
                   [&](Scene &scene) { scene.find(mirrored)->transform().scale = {-1.0F, 1.0F}; });
    m.doc().select(mirrored);
    m.ui.tool = Tool::Resize;
    Handle rightHandle;
    for (const Handle &handle : m.ui.handles())
        if (handle.kind == HandleKind::Right)
            rightHandle = handle;
    CHECK(rightHandle.kind == HandleKind::Right && rightHandle.screen.x > 400.0F);
    const Vec2 target = rightHandle.screen + Vec2{40.0F, 0.0F}; // One meter further right.
    m.ui.pointerPressed(rightHandle.screen, {});
    m.ui.pointerMoved(target, {});
    m.ui.pointerReleased(target, {});
    const auto mirroredBox = gizmoBox(m.get(mirrored));
    CHECK(mirroredBox);
    CHECK_NEAR(mirroredBox->center.x + mirroredBox->half.x, 2.0, 1e-3); // Grew toward the pointer,
    CHECK_NEAR(mirroredBox->center.x - mirroredBox->half.x, -1.0,
               1e-3); // away from the fixed side.

    // Entities with no size have no resize handles.
    View e;
    const EntityId bare = e.doc().createEntity("Bare");
    e.doc().select(bare);
    e.ui.tool = Tool::Resize;
    CHECK(e.ui.handles().empty());
}

void rotating() {
    View v;
    const EntityId a = v.f.box("A", {0, 0}, {2, 2});
    v.doc().select(a);
    v.ui.tool = Tool::Rotate;
    const auto handles = v.ui.handles();
    CHECK(handles.size() == 1 && handles[0].kind == HandleKind::Rotate);
    // The knob hangs above the top edge.
    CHECK_NEAR(handles[0].screen.x, v.px({0, 0}).x, 1e-3);
    CHECK(handles[0].screen.y < v.px({0, -1}).y);
    const Vec2 knob = v.ui.toWorld(handles[0].screen);
    v.drag(knob, {3.0F, 0.0F}); // Straight to the right: a quarter turn clockwise.
    CHECK_NEAR(v.get(a).transform().rotationDegrees, 90.0, 1e-2);
    CHECK(v.doc().undoLabel() == "Rotate");
    // Snapped to 15 degrees; Ctrl gives the exact angle.
    v.doc().undo();
    v.drag(knob, {3.0F, 1.2F});
    CHECK_NEAR(v.get(a).transform().rotationDegrees, 105.0, 1e-2);
    v.doc().undo();
    v.drag(knob, {3.0F, 1.2F}, {.ctrl = true});
    CHECK_NEAR(v.get(a).transform().rotationDegrees, 111.8, 0.05);
    v.doc().undo();
    // The knob follows the rotation.
    v.doc().change("turn",
                   [&](Scene &scene) { scene.find(a)->transform().rotationDegrees = 90.0F; });
    const Handle turned = v.ui.handles().front();
    CHECK(turned.screen.x > v.px({1, 0}).x); // "Up" now points right.
    CHECK_NEAR(turned.screen.y, v.px({0, 0}).y, 1e-3);
}

void draggingGhosts() {
    View v;
    const auto gate = v.doc().createFromTemplate(templateNamed(v.f.registry, "Door"), {0, 0});
    v.ui.tool = Tool::Move;
    Handle ghost;
    for (const Handle &handle : v.ui.handles())
        if (handle.kind == HandleKind::Ghost)
            ghost = handle;
    CHECK(ghost.kind == HandleKind::Ghost);
    CHECK_NEAR(ghost.screen.y, v.px({0, -3}).y, 1e-3); // Where the door ends up when open.
    v.drag({0.0F, -3.0F}, {1.0F, -5.0F});
    const auto *door = v.get(gate.value()).get<Door>();
    CHECK_NEAR(door->openOffset.x, 1.0);
    CHECK_NEAR(door->openOffset.y, -5.0);
    CHECK(v.get(gate.value()).worldPosition() == Vec2({0.0F, 0.0F})); // The door itself stays.
    CHECK(v.doc().undoLabel() == "Move Target" && v.doc().undo());
    CHECK_NEAR(v.get(gate.value()).get<Door>()->openOffset.y, -3.0);
}

// ---------------------------------------------------------------------------------------------
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char *name) : path(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove_all(path);
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void projectFiles() {
    ComponentRegistry registry;
    registerAllModules(registry);
    TempDir dir("yk-editor-project-test");
    auto created = EditorProject::create(dir.path / "My Game", "My Game", registry);
    CHECK(created);
    if (!created)
        return;
    EditorProject &project = *created.value();
    const auto root = project.project().root;
    CHECK(std::filesystem::exists(root / "project.ykproj") &&
          std::filesystem::exists(root / "scenes" / "main.ykscene") &&
          std::filesystem::is_directory(root / "prefabs") &&
          std::filesystem::is_directory(root / "assets"));
    CHECK(project.project().startScene == "scenes/main.ykscene");
    CHECK(project.project().layers.indexOf("Solid") >= 0 &&
          project.project().layers.indexOf("Player") >= 0);
    CHECK(!EditorProject::create(dir.path / "My Game", "Again", registry)); // Already a project.
    CHECK(!EditorProject::create(dir.path / "Other", "", registry));

    // The new project opens and its first scene has a camera to start from.
    auto reopened = EditorProject::open(root, registry);
    CHECK(reopened && reopened.value()->project().name == "My Game");
    auto main = project.openScene("scenes/main.ykscene");
    CHECK(main && main.value()->scene().findByName("Main Camera") &&
          main.value()->scene().findByName("Main Camera")->has<Camera>());
    CHECK(!project.openScene("scenes/missing.ykscene"));
    CHECK(!project.openScene("../outside.ykscene"));
    CHECK(project.validate().empty());

    // New scenes, with the extension supplied and location kept inside the project.
    auto level = project.newScene("scenes/level one");
    CHECK(level && level.value()->path() == "scenes/level one.ykscene" &&
          std::filesystem::exists(root / "scenes" / "level one.ykscene"));
    CHECK(!project.newScene("scenes/level one.ykscene")); // Never overwrites.
    CHECK(!project.newScene("../evil") && !project.newScene("   ") && !project.newScene("scenes/"));
    CHECK(!project.newScene("/etc/evil"));
    auto backslashes = project.newScene("scenes\\sub\\deep");
    CHECK(backslashes && backslashes.value()->path() == "scenes/sub/deep.ykscene");

    // Editing, saving, reloading.
    EditorDocument &doc = *level.value();
    CHECK(!doc.dirty());
    const auto platform = doc.createFromTemplate(templateNamed(registry, "Platform"), {3, 4});
    CHECK(doc.dirty() && platform);
    CHECK(project.saveScene(doc) && !doc.dirty());
    auto again = project.openScene("scenes/level one.ykscene");
    CHECK(again && again.value()->scene().find(platform.value()));
    CHECK_NEAR(again.value()->scene().find(platform.value())->worldPosition().x, 3.0);
    // Save As gives the document its new path.
    CHECK(project.saveSceneAs(doc, "scenes/copy") && doc.path() == "scenes/copy.ykscene" &&
          std::filesystem::exists(root / "scenes" / "copy.ykscene"));
    CHECK(!project.saveSceneAs(doc, "../up") && doc.path() == "scenes/copy.ykscene");
    EditorDocument unsaved(registry, std::make_unique<Scene>(registry, 1));
    CHECK(!project.saveScene(unsaved)); // No file name yet.
    doc.beginChange("busy");
    CHECK(!project.saveScene(doc));
    doc.endChange();

    // Prefabs round trip.
    CHECK(project.savePrefab(doc, platform.value(), "prefabs/floor"));
    CHECK(std::filesystem::exists(root / "prefabs" / "floor.ykprefab"));
    auto prefab = project.loadPrefab("prefabs/floor.ykprefab");
    CHECK(prefab && prefab.value().get("format").asString() == "yk.prefab");
    CHECK(!project.loadPrefab("prefabs/none.ykprefab"));
    const auto placed = doc.instantiatePrefab(prefab.value(), {9, 9});
    CHECK(placed && doc.scene().size() == 3); // Camera, platform and the new copy.

    // The asset list sees them all.
    project.refresh();
    int scenes = 0, prefabs = 0;
    for (const AssetEntry &entry : project.files()) {
        scenes += entry.kind == AssetKind::Scene ? 1 : 0;
        prefabs += entry.kind == AssetKind::Prefab ? 1 : 0;
    }
    CHECK(scenes == 4 && prefabs == 1);

    // Exporting the game: the player beside a copy of the project.
    const auto fakePlayer = dir.path / "yk_player";
    CHECK(writeTextFileAtomic(fakePlayer, "not really a program"));
    CHECK(!project.exportGame(dir.path / "out",
                              dir.path / "missing_player")); // No player, no export.
    const auto exported = project.exportGame(dir.path / "out", fakePlayer);
    CHECK(exported && exported.value().filename() == "My Game-game");
    if (exported) {
        CHECK(std::filesystem::exists(exported.value() / "yk_player") &&
              std::filesystem::exists(exported.value() / "README.txt") &&
              std::filesystem::exists(exported.value() / "project" / "project.ykproj") &&
              std::filesystem::exists(exported.value() / "project" / "scenes" / "main.ykscene") &&
              std::filesystem::exists(exported.value() / "project" / "prefabs" / "floor.ykprefab"));
        CHECK(Project::load(exported.value() / "project"));       // The copy is a complete project.
        CHECK(!project.exportGame(dir.path / "out", fakePlayer)); // Never overwrites.
    }

    // Validation flags a scene that points at a missing asset; such a project cannot be exported.
    doc.change("break", [&](Scene &scene) {
        scene.find(platform.value())->get<SpriteRenderer>()->texture.path = "assets/missing.png";
    });
    CHECK(project.saveScene(doc));
    const auto issues = project.validate();
    CHECK(!issues.empty() && hasErrors(issues));
    const auto refused = project.exportGame(dir.path / "out2", fakePlayer);
    CHECK(!refused && refused.error().find("error") != std::string::npos);
    CHECK(!std::filesystem::exists(dir.path / "out2"));
    doc.change("repair", [&](Scene &scene) {
        scene.find(platform.value())->get<SpriteRenderer>()->texture.path.clear();
    });
    CHECK(project.saveScene(doc));

    // Project settings persist.
    project.project().name = "Renamed";
    project.project().window.width = 1000;
    CHECK(project.save());
    auto settings = EditorProject::open(root, registry);
    CHECK(settings && settings.value()->project().name == "Renamed" &&
          settings.value()->project().window.width == 1000);
    CHECK(!EditorProject::open(dir.path / "nothing here", registry));

    // Recent projects: newest first, unique, capped, and forgetful about deleted folders.
    RecentProjects recent(dir.path / "recent.json");
    recent.load();
    CHECK(recent.paths().empty());
    recent.add(root);
    recent.add(dir.path);
    recent.add(root);
    CHECK(recent.paths().size() == 2 && recent.paths()[0] == root.string());
    for (int i = 0; i < 12; ++i)
        recent.add(dir.path / ("p" + std::to_string(i)));
    CHECK(recent.paths().size() == RecentProjects::maxEntries);
    recent.add(root);
    CHECK(recent.save());
    RecentProjects loaded(dir.path / "recent.json");
    loaded.load();
    CHECK(loaded.paths().size() == 1 &&
          loaded.paths()[0] == root.string()); // The rest do not exist.
    CHECK(!RecentProjects(dir.path / "missing" / "recent.json").paths().size());
}

void sampleProject() {
    setLogStderrEnabled(false);
    ComponentRegistry registry;
    registerAllModules(registry);
    auto opened = EditorProject::open(
        std::filesystem::path(YK_SOURCE_DIR) / "projects" / "elemental-prototype", registry);
    CHECK(opened);
    if (!opened)
        return;
    EditorProject &project = *opened.value();
    CHECK(!hasErrors(project.validate()));
    auto level = project.openScene("scenes/test_level.ykscene");
    CHECK(level && level.value()->scene().size() > 40);
    CHECK(!level.value()->dirty() && !level.value()->canUndo());
    // Round trip through the editor's own save format changes nothing of substance.
    const Json original = sceneToJson(level.value()->scene());
    auto text = Json::parse(original.dump(2));
    CHECK(text && text.value().dump(2) == original.dump(2));
    // Picking works on real content: the fire character is where its sprite is.
    const Entity *fireCharacter = level.value()->scene().findByName("Fire Character");
    CHECK(fireCharacter);
    if (fireCharacter) {
        const auto stack = pickAll(level.value()->scene(), fireCharacter->worldPosition(), 0.02F);
        CHECK(!stack.empty());
        bool found = false;
        for (const EntityId id : stack)
            found = found || id == fireCharacter->id();
        CHECK(found);
    }
    // Links in the real level: the lever opens something.
    CHECK(!allLinks(level.value()->scene()).empty());
    setLogStderrEnabled(true);
}

// ---------------------------------------------------------------------------------------------
Keyboard held(std::initializer_list<Key> keys) {
    Keyboard keyboard;
    keyboard.beginFrame();
    for (const Key key : keys)
        keyboard.set(key, true);
    return keyboard;
}

void playing() {
    setLogStderrEnabled(false);
    ComponentRegistry registry;
    registerAllModules(registry);
    auto opened = EditorProject::open(
        std::filesystem::path(YK_SOURCE_DIR) / "projects" / "elemental-prototype", registry);
    CHECK(opened);
    if (!opened)
        return;
    EditorProject &project = *opened.value();
    auto document = project.openScene("scenes/test_level.ykscene");
    CHECK(document);
    if (!document)
        return;
    EditorDocument &doc = *document.value();
    // An edit that was never saved must still be in the game that Play starts.
    const Entity *character = doc.scene().findByName("Fire Character");
    CHECK(character);
    const EntityId characterId = character->id();
    doc.rename(characterId, "Player One");
    const Json before = sceneToJson(doc.scene());

    doc.beginChange("busy");
    CHECK(!PlaySession::start(doc, project, nullptr, {1280, 720}));
    doc.endChange();
    auto started = PlaySession::start(doc, project, nullptr, {1280, 720});
    CHECK(started);
    if (!started)
        return;
    PlaySession &play = *started.value();
    Scene &running = play.runtime().scene();
    Entity *runner = running.findByName("Player One");
    CHECK(runner && &running != &doc.scene());
    if (!runner)
        return;
    CHECK(runner->id() == characterId); // Same entities, same ids.
    const float startX = runner->worldPosition().x;
    for (int i = 0; i < 90; ++i)
        play.update(1.0 / 60.0, held({Key::D}));
    CHECK(running.find(characterId)->worldPosition().x > startX + 2.0F); // D runs P1 to the right.
    CHECK(play.runtime().tick() >= 90);

    // Pausing freezes the game; stepping advances one tick.
    play.setPaused(true);
    const auto tick = play.runtime().tick();
    play.update(1.0, held({Key::D}));
    CHECK(play.paused() && play.runtime().tick() == tick);
    play.step(held({Key::D}));
    CHECK(play.runtime().tick() == tick + 1);
    play.setPaused(false);
    play.update(1.0 / 60.0, held({}));
    CHECK(play.runtime().tick() > tick + 1);

    // Restart begins the scene again.
    CHECK(play.restart());
    CHECK_NEAR(play.runtime().scene().find(characterId)->worldPosition().x, startX, 0.05);

    // Scene changes requested by the game load that scene from the project.
    play.runtime().requestSceneChange("scenes/playground.ykscene");
    play.update(1.0 / 60.0, held({}));
    CHECK(play.scenePath() == "scenes/playground.ykscene");
    CHECK(!play.runtime().scene().findByName("Player One"));
    play.runtime().requestSceneChange("scenes/none.ykscene");
    play.update(1.0 / 60.0, held({})); // A bad request is reported, not fatal.
    CHECK(play.scenePath() == "scenes/playground.ykscene");
    play.setViewportSize({640, 360});
    CHECK(play.runtime().viewportSize() == Vec2({640.0F, 360.0F}));

    // Stopping drops the copy; the edited scene is exactly as it was.
    started.value().reset();
    CHECK(sceneToJson(doc.scene()) == before && doc.dirty() && !doc.inChange());
    CHECK(doc.scene().find(characterId)->name() == "Player One");
    setLogStderrEnabled(true);
}

void console() {
    setLogStderrEnabled(false);
    {
        ConsoleLog log;
        yk::log(LogLevel::Info, "a", "one");
        yk::log(LogLevel::Warning, "b", "two");
        yk::log(LogLevel::Error, "c", "three");
        const auto entries = log.snapshot();
        CHECK(entries.size() == 3 && entries[0].subsystem == "a" && entries[2].message == "three");
        CHECK(entries[1].serial == entries[0].serial + 1);
        CHECK(log.count(LogLevel::Warning) == 1 && log.count(LogLevel::Error) == 1);
        CHECK(log.latest().message == "three" && log.latest().level == LogLevel::Error);
        const auto version = log.version();
        yk::log(LogLevel::Info, "a", "four");
        CHECK(log.version() != version);
        log.clear();
        CHECK(log.snapshot().empty() && log.latest().message.empty());
        for (int i = 0; i < 2100; ++i)
            yk::log(LogLevel::Info, "spam", std::to_string(i));
        const auto spam = log.snapshot();
        CHECK(spam.size() == ConsoleLog::capacity && spam.back().message == "2099" &&
              spam.front().message == "100");
    }
    yk::log(LogLevel::Info, "after", "the console is gone"); // No dangling sink.
    setLogStderrEnabled(true);
}
} // namespace

int main() {
    undoAndRedo();
    historyLimit();
    selection();
    propertiesAndComponents();
    creatingEntities();
    duplicating();
    deleting();
    hierarchyEdits();
    clipboard();
    geometry();
    picking();
    linksAndGhosts();
    viewCamera();
    clickAndMarquee();
    movingThings();
    resizing();
    rotating();
    draggingGhosts();
    projectFiles();
    sampleProject();
    playing();
    console();
    return yk::test::finish("editor_core");
}
