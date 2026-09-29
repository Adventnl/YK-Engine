#pragma once
#include "yk/gameplay/Gameplay.hpp"

// The Elemental Prototype: a two-character cooperative platformer used to validate the engine. This
// is a game module: it depends on the engine and gameplay libraries, never the other way around.
// The editor and the player link it so its components and entity templates appear in the Add
// Component and Create menus.
namespace yk::prototype {
// Tags that give characters their elements. Hazards and exits filter on them.
inline constexpr const char *fireTag = "fire";
inline constexpr const char *waterTag = "water";

// Entity builders, shared by the editor's Create menu and the level generator.
EntityId createFireCharacter(Scene &scene, Vec2 at);
EntityId createWaterCharacter(Scene &scene, Vec2 at);
EntityId createFireExit(Scene &scene, Vec2 at);
EntityId createWaterExit(Scene &scene, Vec2 at);
EntityId createLavaPool(Scene &scene, Vec2 at, Vec2 size);
EntityId createWaterPool(Scene &scene, Vec2 at, Vec2 size);
EntityId createGooPool(Scene &scene, Vec2 at, Vec2 size);
EntityId createFireGem(Scene &scene, Vec2 at);
EntityId createWaterGem(Scene &scene, Vec2 at);

// Registers the prototype's entity templates. Requires the gameplay components.
void registerPrototypeGame(ComponentRegistry &registry);
} // namespace yk::prototype
