#pragma once
#include "yk/core/Math.hpp"
#include "yk/core/Result.hpp"
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace yk::game {
enum class Tile : char {
    Empty = '.',
    Solid = '#',
    OneWay = '=',
    SlopeUp = '/',
    SlopeDown = '\\',
    Hazard = '!'
};
enum class EntityKind { Key, Door, Checkpoint, Transition, MovingPlatform, Decoration };
struct EntityDef {
    EntityKind kind{};
    std::string id;
    Rect bounds{};
    std::string target;
    Vec2 destination{};
};
struct Level {
    std::string id;
    int width{}, height{};
    float tileSize{32};
    std::vector<Tile> collision;
    std::map<std::string, Vec2> spawns;
    std::vector<EntityDef> entities;
    Tile tile(int x, int y) const;
    Vec2 tileToWorld(int x, int y) const;
};
Result<Level> loadLevel(const std::filesystem::path &path);
Status validateLinks(const std::map<std::string, Level> &levels);

struct InputFrame {
    float move{};
    bool jump{}, jumpReleased{}, drop{}, interact{}, pause{};
};
class InputBuffer {
  public:
    void push(InputFrame frame);
    InputFrame consumeTick();
    void clear();

  private:
    InputFrame pending_{};
};
struct ControllerConfig {
    float speed{190}, groundAcceleration{1500}, airAcceleration{850}, deceleration{1800};
    float gravity{1150}, maximumFallSpeed{620}, jumpSpeed{430};
    float coyoteSeconds{0.09F}, jumpBufferSeconds{0.12F}, dropSeconds{0.18F};
};
struct PlayerState {
    Vec2 position{}, previous{}, velocity{};
    bool grounded{};
    float coyote{}, jumpBuffer{}, drop{};
};
class CharacterController {
  public:
    explicit CharacterController(ControllerConfig config = {}) : config_(config) {}
    void reset(PlayerState &player, Vec2 spawn) const;
    void tick(PlayerState &player, const Level &level, InputFrame input, float seconds) const;

  private:
    ControllerConfig config_;
};
struct AnimationClip {
    std::string name;
    int firstFrame{}, frameCount{1};
    float frameSeconds{0.1F};
    bool loop{true};
};
class Animator {
  public:
    Status define(AnimationClip clip);
    Status play(const std::string &name);
    void tick(float seconds);
    int frame() const;
    bool takeCompletion();

  private:
    std::map<std::string, AnimationClip> clips_;
    const AnimationClip *active_{};
    float elapsed_{};
    int index_{};
    bool completed_{}, emitted_{};
};
struct Progress {
    unsigned version{1};
    std::string level{"prison"}, spawn{"start"};
    std::set<std::string> collected, unlocked, checkpoints;
    int keys{};
};
Result<Progress> loadProgress(const std::filesystem::path &path);
Status saveProgress(const std::filesystem::path &path, const Progress &progress);
} // namespace yk::game
