#include "yk/components/Effects.hpp"
#include "yk/core/Log.hpp"
#include "yk/runtime/GameContext.hpp"
#include <algorithm>
#include <cmath>

namespace yk {
namespace {
constexpr int hardParticleLimit = 5000;

// Uniform in [0, 1) from raw mt19937 output. mt19937 is exactly specified by the standard, and
// std::uniform_real_distribution is not, so this keeps effects identical across compilers.
float unit(std::mt19937 &rng) {
    return static_cast<float>(rng() >> 8) * (1.0F / 16777216.0F);
}
float waveValue(Wave kind, float cycle) {
    const float t = cycle - std::floor(cycle);
    if (kind == Wave::Triangle)
        return 4.0F * std::fabs(t - 0.5F) - 1.0F;
    return std::sin(2.0F * pi * t);
}
} // namespace

// ----- ParticleEmitter ---------------------------------------------------------------------------
void ParticleEmitter::describe(TypeBuilder<ParticleEmitter> &type) {
    type.category("Effects")
        .description("Emits small sprites (sparks, dust, bubbles). Use a one-shot emitter inside an "
                     "effect prefab that gameplay components spawn.");
    type.field("texture", &ParticleEmitter::texture)
        .asset("texture")
        .tooltip("Leave empty for a placeholder particle.");
    type.field("particleShape", &ParticleEmitter::particleShape)
        .options({"Soft", "Circle", "Square"});
    type.field("playOnStart", &ParticleEmitter::playOnStart);
    type.field("loop", &ParticleEmitter::loop)
        .tooltip("Off: emit for `duration` seconds, then stop (a one-shot).");
    type.field("duration", &ParticleEmitter::duration).range(0, 600, 0.05);
    type.field("rate", &ParticleEmitter::rate).range(0, 2000, 1).tooltip("Particles per second.");
    type.field("burst", &ParticleEmitter::burst)
        .range(0, hardParticleLimit)
        .tooltip("Particles emitted at once when playback starts.");
    type.field("maxParticles", &ParticleEmitter::maxParticles).range(1, hardParticleLimit);
    type.field("lifetime", &ParticleEmitter::lifetime)
        .range(0.01, 600, 0.05)
        .tooltip("Minimum and maximum seconds a particle lives.");
    type.field("area", &ParticleEmitter::area).options({"Point", "Circle", "Box"});
    type.field("areaSize", &ParticleEmitter::areaSize).range(0, 1000, 0.05);
    type.field("direction", &ParticleEmitter::direction)
        .range(-360, 360, 1)
        .tooltip("Degrees clockwise from up.");
    type.field("spread", &ParticleEmitter::spread).range(0, 360, 1);
    type.field("speed", &ParticleEmitter::speed).range(0, 500, 0.05);
    type.field("gravity", &ParticleEmitter::gravity).range(-500, 500, 0.1);
    type.field("drag", &ParticleEmitter::drag).range(0, 50, 0.05);
    type.field("startSize", &ParticleEmitter::startSize).range(0.001, 100, 0.01);
    type.field("endScale", &ParticleEmitter::endScale)
        .range(0, 20, 0.05)
        .tooltip("Size multiplier reached at the end of a particle's life.");
    type.field("startRotation", &ParticleEmitter::startRotation).range(-720, 720, 1);
    type.field("rotationSpeed", &ParticleEmitter::rotationSpeed).range(-3600, 3600, 1);
    type.field("startColor", &ParticleEmitter::startColor);
    type.field("endColor", &ParticleEmitter::endColor);
    type.field("blend", &ParticleEmitter::blend).options({"Alpha", "Additive"});
    type.field("layer", &ParticleEmitter::layer).range(-1000, 1000);
    type.field("order", &ParticleEmitter::order).range(-1000, 1000, 0.1);
    type.field("localSpace", &ParticleEmitter::localSpace)
        .tooltip("Particles follow the emitter instead of staying where they were born.");
}

float ParticleEmitter::random() {
    return unit(rng_);
}
float ParticleEmitter::random(float low, float high) {
    return low + (high - low) * random();
}

void ParticleEmitter::play() {
    playing_ = true;
    elapsed_ = 0.0F;
    carry_ = 0.0F;
    hasLast_ = false;
    if (burst > 0)
        emitBurst(burst);
}

void ParticleEmitter::emitBurst(int count) {
    if (!seeded_) { // Also covers bursts requested before the emitter started.
        rng_.seed(static_cast<std::uint32_t>(entity().id().value ^ (entity().id().value >> 32)));
        seeded_ = true;
    }
    const Transform2D world = hasLast_ ? last_ : entity().worldTransform();
    for (int i = 0; i < count; ++i)
        spawn(world);
}

void ParticleEmitter::spawn(const Transform2D &world) {
    const std::size_t cap = static_cast<std::size_t>(std::clamp(maxParticles, 1, hardParticleLimit));
    if (particles_.size() >= cap)
        return;
    Particle particle;
    // Emission point inside the area, in the emitter's frame.
    Vec2 origin{};
    if (area == EmitterArea::Circle) {
        const float angle = random(0.0F, 2.0F * pi);
        const float distance = std::sqrt(random());
        origin = {std::cos(angle) * distance * areaSize.x * 0.5F,
                  std::sin(angle) * distance * areaSize.x * 0.5F};
    } else if (area == EmitterArea::Box) {
        origin = {random(-0.5F, 0.5F) * areaSize.x, random(-0.5F, 0.5F) * areaSize.y};
    }
    // Direction: degrees clockwise from up -> a vector (+Y is down, so up is (0, -1)).
    const float degrees = direction + random(-0.5F, 0.5F) * spread;
    const float radians = degreesToRadians(degrees);
    const Vec2 heading{std::sin(radians), -std::cos(radians)};
    const float speedNow = random(std::min(speed.x, speed.y), std::max(speed.x, speed.y));
    Vec2 velocity = heading * speedNow;
    if (localSpace) {
        particle.position = origin;
    } else {
        // World space: the emitter's rotation and scale apply once, at birth.
        particle.position = transformPoint(world, origin);
        velocity = rotated(velocity, degreesToRadians(world.rotationDegrees));
    }
    particle.velocity = velocity;
    particle.lifetime = std::max(0.01F, random(std::min(lifetime.x, lifetime.y),
                                               std::max(lifetime.x, lifetime.y)));
    particle.size = random(std::min(startSize.x, startSize.y), std::max(startSize.x, startSize.y));
    particle.rotation = random(std::min(startRotation.x, startRotation.y),
                               std::max(startRotation.x, startRotation.y));
    particle.rotationSpeed = random(std::min(rotationSpeed.x, rotationSpeed.y),
                                    std::max(rotationSpeed.x, rotationSpeed.y));
    particles_.push_back(particle);
}

void ParticleEmitter::step(float seconds, const Transform2D &world) {
    if (!(seconds > 0.0F) || !std::isfinite(seconds))
        return;
    if (!seeded_) {
        rng_.seed(static_cast<std::uint32_t>(entity().id().value ^ (entity().id().value >> 32)));
        seeded_ = true;
    }
    last_ = world;
    hasLast_ = true;
    if (playing_) {
        elapsed_ += seconds;
        carry_ += rate * seconds;
        while (carry_ >= 1.0F) {
            carry_ -= 1.0F;
            spawn(world);
        }
        if (!loop && elapsed_ >= duration)
            playing_ = false;
    }
    const float keep = std::max(0.0F, 1.0F - drag * seconds);
    for (std::size_t i = 0; i < particles_.size();) {
        Particle &particle = particles_[i];
        particle.age += seconds;
        if (particle.age >= particle.lifetime) {
            particle = particles_.back(); // Order does not matter: unstable erase.
            particles_.pop_back();
            continue;
        }
        particle.velocity += gravity * seconds;
        particle.velocity *= keep;
        particle.position += particle.velocity * seconds;
        particle.rotation += particle.rotationSpeed * seconds;
        ++i;
    }
}

void ParticleEmitter::onStart(GameContext &) {
    particles_.clear();
    if (playOnStart)
        play();
}

void ParticleEmitter::onUpdate(GameContext &, float seconds) {
    step(seconds, entity().worldTransform());
}

// ----- Light2D -----------------------------------------------------------------------------------
void Light2D::describe(TypeBuilder<Light2D> &type) {
    type.category("Effects").description(
        "A soft additive glow (torch, lava, gem). Decoration only: it lights nothing else.");
    type.field("color", &Light2D::color);
    type.field("intensity", &Light2D::intensity).range(0, 10, 0.05);
    type.field("radius", &Light2D::radius).range(0.05, 100, 0.05);
    type.field("offset", &Light2D::offset).range(-100, 100, 0.05);
    type.field("flicker", &Light2D::flicker).range(0, 1, 0.01);
    type.field("flickerSpeed", &Light2D::flickerSpeed).range(0, 60, 0.5);
    type.field("layer", &Light2D::layer).range(-1000, 1000);
    type.field("order", &Light2D::order).range(-1000, 1000, 0.1);
}

void Light2D::onUpdate(GameContext &, float seconds) {
    flickering_ = flicker > 0.0F;
    if (!flickering_)
        return; // Steady: currentIntensity() is the authored intensity.
    phase_ += seconds * flickerSpeed;
    // Two incommensurate sines make a pleasantly irregular but bounded flicker in 0..1.
    const float noise = 0.5F + 0.25F * std::sin(phase_ * 2.0F * pi) +
                        0.25F * std::sin(phase_ * 2.0F * pi * 2.618F + 1.3F);
    current_ = intensity * (1.0F - flicker * (1.0F - noise));
}

// ----- Oscillator --------------------------------------------------------------------------------
void Oscillator::describe(TypeBuilder<Oscillator> &type) {
    type.category("Effects").description(
        "Sways, bobs or pulses the entity's transform in a repeating wave (grass, gems, glows).");
    type.field("position", &Oscillator::position).range(-100, 100, 0.01);
    type.field("rotation", &Oscillator::rotation).range(-360, 360, 0.1);
    type.field("scale", &Oscillator::scale).range(-10, 10, 0.01);
    type.field("frequency", &Oscillator::frequency).range(0, 20, 0.01);
    type.field("phase", &Oscillator::phase).range(0, 1, 0.01);
    type.field("randomPhase", &Oscillator::randomPhase)
        .tooltip("Offset each copy by its own amount so a row of grass does not sway in step.");
    type.field("wave", &Oscillator::wave).options({"Sine", "Triangle"});
}

void Oscillator::onStart(GameContext &) {
    active_ = !entity().has<RigidBody>(); // Physics owns the transform of a body.
    if (!active_) {
        log(LogLevel::Warning, "effects",
            "'" + entity().name() + "': an Oscillator cannot move an entity with a RigidBody");
        return;
    }
    base_ = entity().transform();
    time_ = 0.0F;
    // A stable per-entity fraction (from the id, not from a random source).
    const std::uint64_t id = entity().id().value;
    offset_ = static_cast<float>((id ^ (id >> 29)) % 1000U) / 1000.0F;
}

void Oscillator::onUpdate(GameContext &, float seconds) {
    if (!active_)
        return;
    time_ += seconds;
    const float cycle = time_ * frequency + phase + (randomPhase ? offset_ : 0.0F);
    const float w = waveValue(wave, cycle);
    Transform2D &transform = entity().transform();
    transform.position = base_.position + position * w;
    transform.rotationDegrees = base_.rotationDegrees + rotation * w;
    transform.scale = {base_.scale.x * (1.0F + scale.x * w), base_.scale.y * (1.0F + scale.y * w)};
}

// ----- Lifetime ----------------------------------------------------------------------------------
void Lifetime::describe(TypeBuilder<Lifetime> &type) {
    type.category("Effects").description("Destroys the entity after a while (for effect prefabs).");
    type.field("seconds", &Lifetime::seconds).range(0, 3600, 0.05);
    type.field("untilEmitterFinished", &Lifetime::untilEmitterFinished)
        .tooltip("Also destroy as soon as this entity's particle emitter has finished.");
}

void Lifetime::onUpdate(GameContext &context, float delta) {
    age_ += delta;
    bool done = age_ >= seconds;
    if (!done && untilEmitterFinished)
        if (const auto *emitter = entity().get<ParticleEmitter>())
            done = emitter->finished();
    if (done)
        context.destroyLater(entity().id());
}

void registerEffectComponents(ComponentRegistry &registry) {
    registry.add<ParticleEmitter>("ParticleEmitter");
    registry.add<Light2D>("Light2D");
    registry.add<Oscillator>("Oscillator");
    registry.add<Lifetime>("Lifetime");
    registry.addTemplate({"Particle Emitter", "Effects", [](Scene &scene, Vec2 at) {
                              Entity &entity = scene.createEntity("Particle Emitter");
                              entity.setWorldPosition(at);
                              entity.add<ParticleEmitter>();
                              return entity.id();
                          }});
    registry.addTemplate({"Glow", "Effects", [](Scene &scene, Vec2 at) {
                              Entity &entity = scene.createEntity("Glow");
                              entity.setWorldPosition(at);
                              entity.add<Light2D>();
                              return entity.id();
                          }});
}
} // namespace yk
