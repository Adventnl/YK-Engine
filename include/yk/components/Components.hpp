#pragma once
#include "yk/animation/AnimationController.hpp"
#include "yk/input/Input.hpp"
#include "yk/scene/Entity.hpp"
#include <array>
#include <string>
#include <string_view>
#include <vector>

// Standard components every game gets: rendering data, physics bodies and colliders, camera, UI,
// audio and sprite animation. They are data plus (where needed) small runtime behaviour; drawing is
// done by SceneRenderer and simulation by GameRuntime.
namespace yk {
// ----- Input -------------------------------------------------------------------------------
// Says which set of the project's input map ("Player1", "Player2") controls this entity. Components
// that respond to a person (the platformer controller, levers with an Interact action) read their
// named actions through it, so two characters need two PlayerInput components and no code.
class PlayerInput final : public Component {
  public:
    std::string actionSet{"Player1"};
    static void describe(TypeBuilder<PlayerInput> &type);

    ButtonState button(const GameContext &context, std::string_view action) const;
    float value(const GameContext &context, std::string_view action) const;
};

// ----- Rendering ---------------------------------------------------------------------------
enum class SpriteShape { Rectangle, Ellipse };
// Simple stretches the texture over `size`. Tiled repeats it (cropping the last row and column) so
// a platform can be resized without distorting its art. Sliced keeps the corners of a nine-slice
// texture (its .ykmeta `border`) at their natural size and fills the edges and the middle.
enum class SpriteDrawMode { Simple, Tiled, Sliced };
enum class SpriteFill { Stretch, Tile }; // How a Sliced sprite fills its edges and middle.
enum class SpriteBlend { Alpha, Additive };

// Draws a texture, or a colored placeholder shape while no art exists. Swapping in final art is
// assigning `texture`; nothing else in the game depends on how a sprite looks.
class SpriteRenderer final : public Component {
  public:
    AssetRef texture; // Empty: draw `shape` in `color`.
    SpriteShape shape{SpriteShape::Rectangle};
    Vec2 size{1.0F, 1.0F}; // World units.
    Vec2 offset{0.0F, 0.0F};
    Color color{255, 255, 255, 255};
    int layer{0};
    float order{0.0F};
    bool flipX{false};
    bool visible{true};
    int columns{
        1}; // Sprite sheet grid; `frame` selects the cell. 1 x 1 defers to the texture's meta.
    int rows{1};
    int frame{0};
    SpriteDrawMode drawMode{SpriteDrawMode::Simple};
    Vec2 tileSize{0.0F, 0.0F}; // Tiled: world size of one repeat; 0 uses the texture's own size.
    SpriteFill sliceFill{SpriteFill::Tile};
    SpriteBlend blend{SpriteBlend::Alpha};
    // 1 moves with the world; 0 stays fixed on screen (far background); in between scrolls slower
    // than the camera. Applied by the game view, not by the editor's scene view.
    Vec2 parallax{1.0F, 1.0F};
    static void describe(TypeBuilder<SpriteRenderer> &type);
};

enum class UiAnchor {
    TopLeft,
    Top,
    TopRight,
    Left,
    Center,
    Right,
    BottomLeft,
    Bottom,
    BottomRight
};
const std::vector<std::string> &uiAnchorNames();

// Screen-space text. `text` may contain {variable} placeholders resolved from the Blackboard.
class UiText final : public Component {
  public:
    std::string text{"Text"};
    UiAnchor anchor{UiAnchor::TopLeft};
    Vec2 offset{16.0F, 16.0F}; // Pixels from the anchor point, toward the screen center.
    float scale{2.0F};         // Font pixel multiplier.
    Color color{255, 255, 255, 255};
    bool shadow{true};
    int layer{0};
    static void describe(TypeBuilder<UiText> &type);
};

// Screen-space textured rectangle (HUD icons, frames, logos). A sprite sheet cell can be picked
// with columns/rows/frame, like SpriteRenderer.
class UiImage final : public Component {
  public:
    AssetRef texture; // Empty draws a solid rectangle in `color`.
    UiAnchor anchor{UiAnchor::TopLeft};
    Vec2 size{48.0F, 48.0F}; // Pixels.
    Vec2 offset{16.0F, 16.0F};
    Color color{255, 255, 255, 255};
    int layer{0};
    int columns{1};
    int rows{1};
    int frame{0};
    bool fillScreen{false}; // Stretch over the whole viewport (size, anchor and offset unused).
    static void describe(TypeBuilder<UiImage> &type);
};

// Screen-space filled rectangle (HUD backgrounds, overlays).
class UiPanel final : public Component {
  public:
    UiAnchor anchor{UiAnchor::TopLeft};
    Vec2 size{200.0F, 48.0F}; // Pixels.
    Vec2 offset{8.0F, 8.0F};
    Color color{0, 0, 0, 140};
    int layer{-1};
    static void describe(TypeBuilder<UiPanel> &type);
};

// ----- Physics -----------------------------------------------------------------------------
enum class RigidBodyType { Static, Kinematic, Dynamic };

// Gives an entity a physics body. Colliders on this entity and on its descendants become the body's
// shapes. A collider with no RigidBody in its hierarchy is treated as static geometry.
class RigidBody final : public Component {
  public:
    RigidBodyType type{RigidBodyType::Dynamic};
    float gravityScale{1.0F};
    float linearDamping{0.0F};
    float angularDamping{0.05F};
    bool fixedRotation{false};
    bool bullet{false};
    bool allowSleep{true};
    static void describe(TypeBuilder<RigidBody> &type);
};

// A Wedge is a right triangle filling its size box, with the right angle at the bottom right, so it
// rises to the right: a ramp. Mirror the entity (scale x = -1) for a ramp that rises to the left,
// or scale y = -1 for a sloped ceiling. Its sprite mirrors the same way.
enum class ColliderShape { Box, Circle, Capsule, Wedge };

// The three corners of a wedge of `halfExtents` in the collider's own frame (before rotation),
// mirrored by the signs of `scaleSign`. Shared by physics and the editor's outline drawing.
std::array<Vec2, 3> wedgePoints(Vec2 halfExtents, Vec2 scaleSign);

// Collision or trigger geometry. `size` is the full extent in world units (before entity scale);
// circles use the width as diameter; capsules are vertical with rounded ends.
class Collider final : public Component {
  public:
    ColliderShape shape{ColliderShape::Box};
    Vec2 size{1.0F, 1.0F};
    Vec2 offset{0.0F, 0.0F};
    bool isTrigger{false}; // Overlap events only, no physical response.
    // A jump-through platform: it blocks only what comes down onto its top side; things below or
    // beside it, or moving up through it, pass. Not for triggers.
    bool oneWay{false};
    bool detectTriggers{false};   // A trigger normally ignores other triggers.
    std::string layer{"Default"}; // Project collision layer name.
    float friction{0.6F};
    float restitution{0.0F};
    float density{1.0F};
    static void describe(TypeBuilder<Collider> &type);
};

// ----- Camera ------------------------------------------------------------------------------
enum class CameraMode { Fixed, Follow, FitTargets };

struct CameraView {
    Vec2 position;
    float visibleHeight{}; // World units visible vertically; width follows the viewport's aspect.
};

class Camera final : public Component {
  public:
    bool primary{true};
    CameraMode mode{CameraMode::Fixed};
    float orthographicHeight{18.0F};
    std::vector<EntityRef> targets; // Follow: their midpoint. FitTargets: also zoom to contain all.
    float padding{3.0F};            // Margin around fitted targets, world units.
    float smoothTime{0.15F};        // Seconds to close about 63% of the distance; 0 snaps.
    float minHeight{8.0F};
    float maxHeight{60.0F};
    bool clampToBounds{false};
    Vec2 boundsMin{0.0F, 0.0F};
    Vec2 boundsMax{32.0F, 18.0F};
    static void describe(TypeBuilder<Camera> &type);

    // Where this camera looks: the smoothed runtime state once the game is running, the authored
    // position and height before that.
    CameraView view() const;
    void onLateUpdate(GameContext &context, float seconds) override;

  private:
    Vec2 position_{};
    float height_{};
    bool initialized_{};
};

// ----- Audio -------------------------------------------------------------------------------
class AudioSource final : public Component {
  public:
    AssetRef sound;
    float volume{1.0F};
    bool loop{false};
    bool playOnStart{false};
    static void describe(TypeBuilder<AudioSource> &type);
    void play(GameContext &context) const;
    void onStart(GameContext &context) override;
};

// ----- Animation ---------------------------------------------------------------------------
// Animates the entity's SpriteRenderer from a ".ykanim" asset (clips over a sprite sheet), either
// by playing a clip or by running a ".ykctl" state machine over generic parameters. Gameplay never
// names a clip: components such as PlatformerController and Killable publish parameters (speed,
// grounded, a "jumped" trigger, dead...) and the controller asset decides what plays.
class AnimatedSprite final : public Component {
  public:
    AssetRef animation;  // Clips, sheet layout and texture.
    AssetRef controller; // Optional state machine over the clips.
    std::string clip;    // Without a controller: the clip to play (the first when empty).
    float speed{1.0F};   // Playback speed multiplier for everything.
    bool playOnStart{true};
    // The sprite is mirrored when this parameter is negative ("facing" is published by the
    // platformer controller); empty never flips. Set artFacesLeft when the art is drawn facing
    // left.
    std::string flipParameter{"facing"};
    bool artFacesLeft{false};
    static void describe(TypeBuilder<AnimatedSprite> &type);

    // Controller parameters. Names the controller does not declare are ignored.
    void setFloat(const std::string &name, double value) {
        player_.setFloat(name, value);
    }
    void setBool(const std::string &name, bool value) {
        player_.setBool(name, value);
    }
    void trigger(const std::string &name) {
        player_.trigger(name);
    }
    double value(const std::string &name) const {
        return player_.value(name);
    }
    // Plays a clip directly (ignored when it does not exist); starts playback if it was off.
    void play(const std::string &name);
    const AnimationPlayer &player() const {
        return player_;
    }
    // Current state of the controller (or the clip when there is none).
    const std::string &state() const {
        return player_.state();
    }
    void onStart(GameContext &context) override;
    void onUpdate(GameContext &context, float seconds) override;

  private:
    AnimationPlayer player_;
    bool playing_{};
};

class ComponentRegistry;
void registerEngineComponents(ComponentRegistry &registry);
} // namespace yk
