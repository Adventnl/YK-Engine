#pragma once
#include "yk/scene/Registry.hpp"
#include <string>
#include <vector>

// A character's look as layers drawn over its body sprite: an outfit, hair, a hat, a held tool. The
// body is the entity's own SpriteRenderer (usually animated by an AnimatedSprite); every layer is a
// sprite sheet laid out like the body's, drawn with the same cell, flip, size and place, so
// whatever the body does (walk, turn, flash) the layers do. What a layer shows comes from the
// character: the equipment worn in its Inventory (an item's `equip.appearance` is {"outfit":
// "assets/guard.png"}) over the defaults given here; an item that names a layer with an empty path
// hides it (hair under a hood). Taking the outfit off puts the default back, so a character needs
// art for "wearing nothing" only in its own `defaults`.
//
// The layers are child entities made when the game starts (named "<entity>.<layer>"); they are not
// part of the scene file.
namespace yk {
class GameContext;

class AppearanceLayers final : public Component {
  public:
    std::vector<std::string> layers; // Names, bottom to top.
    Json defaults{Json::object()};   // Layer name -> texture shown when nothing worn names one.
    float orderStep{0.001F};         // Draw order added for each layer above the body.
    static void describe(TypeBuilder<AppearanceLayers> &type);

    void onStart(GameContext &context) override;
    void onLateUpdate(GameContext &context, float seconds) override;

    // The texture a layer shows now (empty: nothing is drawn for it), and the entity that draws it.
    std::string textureOf(const std::string &layer) const;
    EntityId layerEntity(const std::string &layer) const;

  private:
    void refresh(GameContext &context);
    std::vector<EntityId> children_;
    std::vector<std::string> textures_;
    std::uint64_t seenRevision_{~std::uint64_t{0}};
};
} // namespace yk
