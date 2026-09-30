#include "ui/Panels.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace yk::editor::ui {
namespace {
bool containsInsensitive(const std::string &text, const std::string &needle) {
    if (needle.empty())
        return true;
    const auto match =
        std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return match != text.end();
}

bool matchesFilter(const Entity &entity, const std::string &filter) {
    if (containsInsensitive(entity.name(), filter))
        return true;
    for (const auto &component : entity.components())
        if (containsInsensitive(component->type().name, filter))
            return true;
    return false;
}

// Icon and color hint by what the entity does, so a glance at the tree tells logic from art.
struct RowStyle {
    Icon icon{Icon::Entity};
    Color color{palette::dim};
};
RowStyle styleOf(const Entity &entity) {
    if (entity.has<Camera>())
        return {Icon::Target, palette::camera};
    bool gameplay = false, physics = false, rendering = false, ui = false;
    for (const auto &component : entity.components()) {
        const std::string &category = component->type().category;
        gameplay = gameplay || category == "Gameplay";
        physics = physics || category == "Physics";
        rendering = rendering || category == "Rendering";
        ui = ui || category == "UI";
    }
    if (gameplay)
        return {Icon::Entity, palette::selection};
    if (ui)
        return {Icon::Entity, {110, 210, 200, 255}};
    if (physics)
        return {Icon::Entity, {120, 170, 225, 255}};
    if (rendering)
        return {Icon::Entity, {205, 210, 224, 255}};
    if (!entity.childIds().empty() && entity.components().empty())
        return {Icon::Folder, {150, 156, 172, 255}};
    return {Icon::Entity, palette::dim};
}

struct Tree {
    EditorState &state;
    const Scene &scene;
    EditorDocument *doc;         // Null while playing (read-only).
    std::vector<EntityId> shown; // Rows in drawing order, for range selection.
    EntityId toFrame;
};

bool selected(const Tree &tree, EntityId id) {
    return tree.doc ? tree.doc->isSelected(id) : tree.state.playSelection == id;
}

void reparentDropped(Tree &tree, EntityId dragged, EntityId target) {
    if (!tree.doc || dragged == target)
        return;
    // Dragging a member of the selection moves the whole selection.
    std::vector<EntityId> moving =
        tree.doc->isSelected(dragged) ? tree.doc->selectionRoots() : std::vector<EntityId>{dragged};
    tree.doc->change("Reparent", [&](Scene &scene) {
        for (const EntityId id : moving) {
            if (id == target || (target && scene.isAncestor(id, target)))
                continue; // Cannot become its own descendant.
            if (auto status = scene.setParent(id, target, {}, true); !status)
                log(LogLevel::Warning, "editor", status.error());
        }
    });
}

void contextMenu(Tree &tree, EntityId id) {
    EditorState &state = tree.state;
    if (!ImGui::BeginPopupContextItem("node_menu"))
        return;
    if (!tree.doc) {
        if (ImGui::MenuItem("Inspect"))
            state.inspect(id);
        ImGui::EndPopup();
        return;
    }
    if (!tree.doc->isSelected(id))
        tree.doc->select(id);
    if (ImGui::BeginMenu("Create Child")) {
        createEntityMenu(state, id, std::nullopt);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Add Component")) {
        addComponentMenu(state, id);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Rename", "F2")) {
        state.renaming = id;
        state.renameBuffer = tree.scene.find(id)->name();
        state.renameFocus = true;
    }
    if (ImGui::MenuItem("Duplicate", shortcutText("Ctrl+D")))
        tree.doc->duplicateSelection();
    if (ImGui::MenuItem("Delete", "Del"))
        tree.doc->deleteSelection();
    ImGui::Separator();
    if (ImGui::MenuItem("Move Up", shortcutText("Alt+Up"), false,
                        tree.doc->canMoveAmongSiblings(id, -1)))
        tree.doc->moveAmongSiblings(id, -1);
    if (ImGui::MenuItem("Move Down", shortcutText("Alt+Down"), false,
                        tree.doc->canMoveAmongSiblings(id, 1)))
        tree.doc->moveAmongSiblings(id, 1);
    if (ImGui::MenuItem("Move to Top Level", nullptr, false,
                        tree.scene.find(id)->parentId().value != 0))
        tree.doc->reparent(id, {});
    ImGui::Separator();
    const Entity *entity = tree.scene.find(id);
    if (ImGui::MenuItem(entity->active() ? "Deactivate" : "Activate"))
        tree.doc->setEntityActive(id, !entity->active());
    if (ImGui::MenuItem("Save as Prefab..."))
        showDialog(state, DialogKind::SavePrefab, id);
    if (tree.doc->prefabRootOf(id).value != 0 && ImGui::BeginMenu("Prefab")) {
        prefabMenuItems(state, id);
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Frame in Scene View"))
        tree.toFrame = id;
    ImGui::EndPopup();
}

void selectRow(Tree &tree, EntityId id) {
    const ImGuiIO &io = ImGui::GetIO();
    EditorState &state = tree.state;
    if (!tree.doc) {
        state.inspect(id);
        return;
    }
    if (io.KeyShift && state.selectionAnchor && tree.scene.find(state.selectionAnchor)) {
        const auto order = tree.scene.hierarchyOrder();
        auto from = std::find(order.begin(), order.end(), state.selectionAnchor);
        auto to = std::find(order.begin(), order.end(), id);
        if (from != order.end() && to != order.end()) {
            if (from > to)
                std::swap(from, to);
            tree.doc->select(std::vector<EntityId>(from, to + 1),
                             io.KeyCtrl ? SelectMode::Add : SelectMode::Replace);
            tree.doc->select(id, SelectMode::Add); // The clicked row is the primary.
            return;
        }
    }
    state.selectionAnchor = id;
    tree.doc->select(id, io.KeyCtrl ? SelectMode::Toggle : SelectMode::Replace);
}

void drawRename(Tree &tree, EntityId id) {
    EditorState &state = tree.state;
    ImGui::SetNextItemWidth(-1.0F);
    if (state.renameFocus) {
        ImGui::SetKeyboardFocusHere();
        state.renameFocus = false;
    }
    const bool committed =
        inputText("##rename", state.renameBuffer,
                  ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    markItem("hierarchy/rename");
    if (committed) {
        if (tree.doc && !state.renameBuffer.empty())
            tree.doc->rename(id, state.renameBuffer);
        state.renaming = {};
    } else if (ImGui::IsItemDeactivated() || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        state.renaming = {};
    }
}

void drawRow(Tree &tree, EntityId id, bool flat);

// The eye (hide in the editor) and lock (not pickable) buttons at the right end of a row. They
// show when the row is hovered or when they are on, like the actions in VS Code's trees.
void rowToggles(Tree &tree, EntityId id, const Entity &entity) {
    if (!tree.doc)
        return;
    const float size = ImGui::GetTextLineHeight() + 2.0F;
    const float gap = 2.0F;
    const float left = ImGui::GetWindowContentRegionMax().x - 2.0F * size - gap - 6.0F;
    const ImVec2 rowMin = ImGui::GetItemRectMin();
    const bool rowHovered = ImGui::IsMouseHoveringRect(
        rowMin,
        {ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x, rowMin.y + size + 2.0F});
    const struct {
        const char *id;
        Icon on, off;
        bool value;
        const char *tip;
    } toggles[2] = {{"hide", Icon::EyeOff, Icon::Eye, entity.editorHidden(),
                     "Hide in the editor (the running game still shows it)"},
                    {"lock", Icon::Lock, Icon::Unlock, entity.locked(),
                     "Lock: not selectable or movable in the scene view"}};
    ImGui::SameLine(left);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    for (int i = 0; i < 2; ++i) {
        const auto &toggle = toggles[i];
        ImGui::SetCursorScreenPos({start.x + static_cast<float>(i) * (size + gap), start.y});
        if (!toggle.value && !rowHovered) {
            ImGui::Dummy({size, size});
            continue;
        }
        ImGui::PushID(toggle.id);
        const bool clicked = ImGui::InvisibleButton("##toggle", {size, size});
        const bool hovered = ImGui::IsItemHovered();
        drawIcon(
            *ImGui::GetWindowDrawList(), toggle.value ? toggle.on : toggle.off,
            {start.x + static_cast<float>(i) * (size + gap) + size * 0.5F, start.y + size * 0.5F},
            size * 0.8F,
            packed(toggle.value ? vs::text
                   : hovered    ? vs::text
                                : vs::textDim));
        markItem(std::string("hierarchy/") + toggle.id + "/" + entity.name());
        tooltip(toggle.tip);
        ImGui::PopID();
        if (clicked) {
            if (i == 0)
                tree.doc->setEditorHidden({id}, !entity.editorHidden());
            else
                tree.doc->setLocked({id}, !entity.locked());
        }
    }
}

void drawChildren(Tree &tree, const Entity &entity) {
    for (const EntityId child : std::vector<EntityId>(entity.childIds()))
        drawRow(tree, child, false);
}

void drawRow(Tree &tree, EntityId id, bool flat) {
    EditorState &state = tree.state;
    const Entity *entity = tree.scene.find(id);
    if (!entity)
        return;
    tree.shown.push_back(id);
    const bool hasChildren = !entity->childIds().empty() && !flat;
    // AllowOverlap: the eye and lock buttons sit on top of the row and must get their own clicks.
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth |
                               ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_DefaultOpen |
                               ImGuiTreeNodeFlags_AllowOverlap;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selected(tree, id))
        flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID(reinterpret_cast<const void *>(static_cast<std::uintptr_t>(id.value)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        {4.0F, std::max(2.0F, (metrics::row - ImGui::GetFontSize()) * 0.5F)});
    const bool open = ImGui::TreeNodeEx("##row", flags);
    ImGui::PopStyleVar();
    // A row is selected when the mouse comes back up on it, so pressing it to drag it (onto an
    // inspector field, into another parent) does not change what the inspector is showing.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
        state.pressedRow = id;
    const bool released = ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                          state.pressedRow == id && !ImGui::IsItemToggledOpen() &&
                          ImGui::GetDragDropPayload() == nullptr;
    const bool doubleClicked =
        ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (released && state.renaming != id)
        selectRow(tree, id);
    if (doubleClicked)
        tree.toFrame = id;
    contextMenu(tree, id);
    // A context-menu action may have deleted this very entity; finish the row without touching it.
    entity = tree.scene.find(id);
    if (!entity) {
        if (hasChildren && open)
            ImGui::TreePop();
        ImGui::PopID();
        return;
    }

    // Drag: entity references accept this payload too (inspector fields).
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("YK_ENTITY", &id, sizeof id);
        ImGui::TextUnformatted(entityLabel(tree.scene, id).c_str());
        ImGui::EndDragDropSource();
    }
    if (tree.doc && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("YK_ENTITY")) {
            EntityId dragged;
            std::memcpy(&dragged, payload->Data, sizeof dragged);
            reparentDropped(tree, dragged, id);
        }
        ImGui::EndDragDropTarget();
    }

    markItem("hierarchy/" + entity->name());
    // The row's contents, laid over the (label-less) tree node.
    ImGui::SameLine(0.0F, 2.0F);
    const RowStyle style = styleOf(*entity);
    const float alpha = entity->activeInHierarchy() ? 1.0F : 0.45F;
    if (state.renaming == id) {
        drawRename(tree, id);
    } else {
        const std::string name = entityLabel(tree.scene, id);
        ImGui::PushStyleColor(ImGuiCol_Text, faded({216, 220, 230, 255}, alpha));
        iconLabel(style.icon, name.c_str(), faded(style.color, alpha));
        ImGui::PopStyleColor();
    }
    rowToggles(tree, id, *entity);
    if (hasChildren && open) {
        drawChildren(tree, *entity);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void header(EditorState &state, EditorDocument *doc) {
    const float button = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button -
                            ImGui::GetStyle().ItemSpacing.x);
    inputText("##filter", state.hierarchyFilter, 0, "Search");
    markItem("hierarchy/filter");
    ImGui::SameLine();
    ImGui::BeginDisabled(!doc);
    if (ImGui::Button("+", {button, button}))
        ImGui::OpenPopup("hierarchy_create");
    markItem("hierarchy/create");
    ImGui::EndDisabled();
    tooltip("Create an entity");
    if (ImGui::BeginPopup("hierarchy_create")) {
        createEntityMenu(state, {}, std::nullopt);
        ImGui::EndPopup();
    }
}
} // namespace

void hierarchyPanel(EditorState &state) {
    const Scene *scene = state.visibleScene();
    if (!scene) {
        ImGui::TextDisabled(state.project ? "No scene is open." : "No project is open.");
        return;
    }
    EditorDocument *doc = state.playing() ? nullptr : state.document.get();
    header(state, doc);
    if (state.playing())
        ImGui::TextColored(imColor(palette::good), "Running copy (read-only)");

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        state.pressedRow = {}; // Rows that are pressed this frame set it again below.
    Tree tree{state, *scene, doc, {}, {}};
    ImGui::BeginChild("##tree", {0.0F, 0.0F}, ImGuiChildFlags_None);
    if (!state.hierarchyFilter.empty()) {
        for (const EntityId id : scene->hierarchyOrder())
            if (matchesFilter(*scene->find(id), state.hierarchyFilter))
                drawRow(tree, id, true);
        if (tree.shown.empty())
            ImGui::TextDisabled("Nothing matches '%s'.", state.hierarchyFilter.c_str());
    } else {
        for (const EntityId root : std::vector<EntityId>(scene->roots()))
            drawRow(tree, root, false);
        if (scene->size() == 0)
            ImGui::TextDisabled("The scene is empty.\nRight-click to create entities.");
    }
    // The empty area below the rows: unparent by dropping, create by right-clicking.
    const ImVec2 rest = ImGui::GetContentRegionAvail();
    if (rest.y > 4.0F) {
        ImGui::InvisibleButton("##tree_background",
                               {std::max(1.0F, rest.x), std::max(1.0F, rest.y)});
        if (doc) {
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyCtrl)
                doc->clearSelection();
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("YK_ENTITY")) {
                    EntityId dragged;
                    std::memcpy(&dragged, payload->Data, sizeof dragged);
                    reparentDropped(tree, dragged, {});
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem("tree_menu")) {
                createEntityMenu(state, {}, std::nullopt);
                ImGui::EndPopup();
            }
        }
    }
    ImGui::EndChild();
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        state.pressedRow = {};

    if (tree.toFrame) {
        if (doc)
            doc->select(tree.toFrame);
        else
            state.inspect(tree.toFrame);
        state.interaction.frameSelection();
        if (!doc) {
            // While playing there is no document selection to frame; center on the entity.
            if (const Entity *entity = scene->find(tree.toFrame))
                state.interaction.camera.center = entity->worldPosition();
        }
    }
    if (doc && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput) {
        const EntityId primary = doc->primary();
        if (primary && ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
            state.renaming = primary;
            state.renameBuffer = scene->find(primary)->name();
            state.renameFocus = true;
        }
    }
}
} // namespace yk::editor::ui
