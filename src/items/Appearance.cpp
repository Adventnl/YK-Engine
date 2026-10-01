#include "yk/items/Appearance.hpp"
#include "yk/components/Components.hpp"
#include "yk/data/GameData.hpp"
#include "yk/items/Inventory.hpp"
#include "yk/runtime/GameContext.hpp"
#include <algorithm>

namespace yk {
void AppearanceLayers::describe(TypeBuilder<AppearanceLayers> &type) {
    type.category("Character")
        .description("Draws the character's look as layers over its body sprite (outfit, hair, "
                     "hat...): each layer follows the body's frame and shows what the equipment "
                     "worn in the Inventory says, else the default.")
        .dependsOn("SpriteRenderer");
    type.field("layers", &AppearanceLayers::layers)
        .tooltip("Layer names, bottom to top. Items name them in equip.appearance.");
    type.field("defaults", &AppearanceLayers::defaults)
        .tooltip("Layer name -> texture shown when nothing worn names one.");
    type.field("orderStep", &AppearanceLayers::orderStep).range(0.0001, 0.1, 0.0001);
    type.check([](const Entity &, const AppearanceLayers &look, const CheckContext &,
                  std::vector<std::string> &problems) {
        for (std::size_t i = 0; i < look.layers.size(); ++i) {
            if (look.layers[i].empty())
                problems.push_back("has a layer without a name");
            for (std::size_t j = 0; j < i; ++j)
                if (look.layers[i] == look.layers[j]) {
                    problems.push_back("names the layer '" + look.layers[i] + "' twice");
                    break;
                }
        }
        if (!look.defaults.isObject() && !look.defaults.isNull()) {
            problems.push_back("'defaults' must be an object of layer name -> texture");
            return;
        }
        for (std::size_t i = 0; i < look.defaults.size(); ++i) {
            const std::string &key = look.defaults.keyAt(i);
            if (std::find(look.layers.begin(), look.layers.end(), key) == look.layers.end())
                problems.push_back("has a default for '" + key +
                                   "', which is not one of its layers");
            if (!look.defaults.valueAt(i).isString())
                problems.push_back("the default for '" + key + "' must be a texture path");
        }
    });
}

std::string AppearanceLayers::textureOf(const std::string &layer) const {
    for (std::size_t i = 0; i < layers.size() && i < textures_.size(); ++i)
        if (layers[i] == layer)
            return textures_[i];
    return {};
}

EntityId AppearanceLayers::layerEntity(const std::string &layer) const {
    for (std::size_t i = 0; i < layers.size() && i < children_.size(); ++i)
        if (layers[i] == layer)
            return children_[i];
    return {};
}

void AppearanceLayers::onStart(GameContext &context) {
    children_.clear();
    for (const std::string &layer : layers) {
        Entity &child = context.scene().createEntity(entity().name() + "." + layer, entity().id());
        child.add<SpriteRenderer>();
        children_.push_back(child.id());
    }
    seenRevision_ = ~std::uint64_t{0};
    refresh(context);
}

// What each layer shows: the default, then what the equipment worn says (a later slot is drawn over
// an earlier one, so it wins).
void AppearanceLayers::refresh(GameContext &context) {
    textures_.assign(layers.size(), {});
    const auto *inventory = entity().get<Inventory>();
    const GameData &data = gameData(context);
    for (std::size_t i = 0; i < layers.size(); ++i) {
        std::string path = defaults.get(layers[i]).asString();
        if (inventory)
            for (const std::string &slot : inventory->equipmentSlots) {
                const ItemStack *worn = inventory->equipped(slot);
                if (!worn || worn->empty())
                    continue;
                const ItemDefinition *item = data.items.items.find(worn->item.str());
                if (!item || !item->equip)
                    continue;
                const Json &look = item->equip->appearance.get(layers[i]);
                if (look.isString()) // An empty one hides the layer (hair under a hood).
                    path = look.asString();
            }
        textures_[i] = std::move(path);
    }
    seenRevision_ = inventory ? inventory->revision() : 0;
}

void AppearanceLayers::onLateUpdate(GameContext &context, float) {
    const auto *inventory = entity().get<Inventory>();
    if ((inventory ? inventory->revision() : 0) != seenRevision_)
        refresh(context);
    const auto *body = entity().get<SpriteRenderer>();
    if (!body)
        return;
    for (std::size_t i = 0; i < children_.size(); ++i) {
        Entity *child = context.scene().find(children_[i]);
        auto *sprite = child ? child->get<SpriteRenderer>() : nullptr;
        if (!sprite)
            continue;
        if (sprite->texture.path != textures_[i])
            sprite->texture.path = textures_[i];
        sprite->visible = body->visible && !textures_[i].empty();
        sprite->shape = body->shape;
        sprite->size = body->size;
        sprite->offset = body->offset;
        sprite->color = body->color;
        sprite->blend = body->blend;
        sprite->layer = body->layer;
        sprite->order = body->order + orderStep * static_cast<float>(i + 1);
        sprite->ySort = body->ySort;
        sprite->sortOffset = body->sortOffset;
        sprite->flipX = body->flipX;
        sprite->columns = body->columns;
        sprite->rows = body->rows;
        sprite->frame = body->frame;
        sprite->parallax = body->parallax;
    }
}
} // namespace yk
