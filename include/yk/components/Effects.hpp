#pragma once
#include "yk/components/Components.hpp"
#include <cstdint>
#include <random>
#include <vector>

// Visual effects built from components: particles, glows, procedural motion and self-destroying
// effect entities. None of them knows any game; gameplay components spawn effect *prefabs* (a
// sparkle, a puff of dust) at the right moment, so what an effect looks like is data.
namespace yk {
enum class EmitterArea { Point, Circle, Box };
enum class ParticleShape { Soft, Circle, Square };

// Emits and simulates small sprites. Particles live in world space (they stay where they were
// born) unless `localSpace` is set (they follow the emitter, for a flame on a torch). It draws
// through the SceneRenderer, updates on the variable step and is deterministic for a given entity
// and sequence of updates.
class ParticleEmitter final : public Component {
  public:
    AssetRef texture; // Empty: a placeholder `particleShape`.
    ParticleShape particleShape{ParticleShape::Soft};
    bool playOnStart{true};
    bool loop{true};         // false: a one-shot that stops after `duration`.
    float duration{1.0F};    // Seconds a non-looping emitter keeps emitting.
    float rate{20.0F};       // Particles per second while playing.
    int burst{0};            // Particles emitted at once when playback starts (and by emitBurst()).
    int maxParticles{100};
    Vec2 lifetime{0.5F, 1.0F}; // Seconds, minimum and maximum.
    EmitterArea area{EmitterArea::Point};
    Vec2 areaSize{1.0F, 1.0F}; // Circle: width is the diameter. Box: full extents.
    float direction{0.0F};     // Degrees clockwise from up (0 up, 90 right, 180 down).
    float spread{360.0F};      // Width of the cone in degrees (360: every direction).
    Vec2 speed{1.0F, 2.0F};    // m/s, minimum and maximum.
    Vec2 gravity{0.0F, 0.0F};  // m/s^2 (+Y is down).
    float drag{0.0F};          // Fraction of speed lost per second.
    Vec2 startSize{0.1F, 0.2F};
    float endScale{0.0F}; // Size multiplier reached at the end of a particle's life.
    Vec2 startRotation{0.0F, 0.0F}; // Degrees.
    Vec2 rotationSpeed{0.0F, 0.0F}; // Degrees per second.
    Color startColor{255, 255, 255, 255};
    Color endColor{255, 255, 255, 0};
    SpriteBlend blend{SpriteBlend::Additive};
    int layer{6};
    float order{0.0F};
    bool localSpace{false};
    static void describe(TypeBuilder<ParticleEmitter> &type);

    struct Particle {
        Vec2 position; // World, or local to the emitter in local-space mode.
        Vec2 velocity;
        float age{};
        float lifetime{1.0F};
        float size{0.1F};
        float rotation{};
        float rotationSpeed{};
    };
    const std::vector<Particle> &particles() const {
        return particles_;
    }
    bool playing() const {
        return playing_;
    }
    // A one-shot emitter that has stopped and whose particles have all died.
    bool finished() const {
        return !playing_ && particles_.empty();
    }
    void play();
    void stop() {
        playing_ = false;
    }
    // Emits `count` particles now, whether or not the emitter is playing.
    void emitBurst(int count);
    // Advances the simulation by `seconds` for an emitter at `world` (the entity's world
    // transform). onUpdate calls it; the editor can call it to preview effects.
    void step(float seconds, const Transform2D &world);
    void onStart(GameContext &context) override;
    void onUpdate(GameContext &context, float seconds) override;

  private:
    void spawn(const Transform2D &world);
    float random();
    float random(float low, float high);

    std::vector<Particle> particles_;
    std::mt19937 rng_;
    bool seeded_{};
    bool playing_{};
    float elapsed_{};
    float carry_{}; // Fractional particles owed by the emission rate.
    Transform2D last_;
    bool hasLast_{};
};

// A soft additive glow: a radial falloff sprite that adds light to what is behind it (torches,
// lava, gems, a character's aura). It is decoration, not a lighting model: nothing is darkened and
// nothing casts shadows.
class Light2D final : public Component {
  public:
    Color color{255, 200, 120, 255};
    float intensity{1.0F}; // Scales the color; above 1 saturates toward white at the center.
    float radius{2.0F};    // World units from the center to the edge of the glow.
    Vec2 offset{0.0F, 0.0F};
    float flicker{0.0F};      // 0 steady .. 1 fully unsteady.
    float flickerSpeed{6.0F}; // Roughly flickers per second.
    int layer{7};
    float order{0.0F};
    static void describe(TypeBuilder<Light2D> &type);

    // The intensity right now (base intensity with flicker applied).
    float currentIntensity() const {
        return flickering_ ? current_ : intensity;
    }
    void onUpdate(GameContext &context, float seconds) override;

  private:
    float phase_{};
    float current_{};
    bool flickering_{};
};

// Moves, rotates and/or pulses the entity's own transform in a repeating wave: swaying grass,
// bobbing gems, pulsing glows. It offsets the transform the entity had when the game started, so it
// never changes the authored scene. Not for entities with a RigidBody (physics owns their motion).
enum class Wave { Sine, Triangle };
class Oscillator final : public Component {
  public:
    Vec2 position{0.0F, 0.0F}; // Peak offset in local units.
    float rotation{0.0F};      // Peak rotation in degrees.
    Vec2 scale{0.0F, 0.0F};    // Peak scale change (added to 1).
    float frequency{0.5F};     // Cycles per second.
    float phase{0.0F};         // 0..1 of a cycle.
    bool randomPhase{true};    // Also offset by a per-entity amount, so copies do not move in step.
    Wave wave{Wave::Sine};
    static void describe(TypeBuilder<Oscillator> &type);

    void onStart(GameContext &context) override;
    void onUpdate(GameContext &context, float seconds) override;

  private:
    Transform2D base_;
    float time_{};
    float offset_{};
    bool active_{};
};

// Destroys its entity after a while: what an effect prefab needs so it cleans up after itself.
class Lifetime final : public Component {
  public:
    float seconds{1.5F};
    // Also destroy as soon as this entity's ParticleEmitter has finished, if it has one.
    bool untilEmitterFinished{false};
    static void describe(TypeBuilder<Lifetime> &type);

    void onUpdate(GameContext &context, float seconds) override;

  private:
    float age_{};
};

void registerEffectComponents(ComponentRegistry &registry);
} // namespace yk
