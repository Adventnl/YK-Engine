#pragma once
#include "yk/rules/Rules.hpp"
#include "yk/runtime/Services.hpp"
#include "yk/scene/Registry.hpp"
#include <string>
#include <vector>

// What characters notice. A Perceiver looks (a range, a field of view, a short all-round range) and
// listens (a range and a sensitivity); everything with an Identity can be noticed. Seeing needs a
// clear line over the navigation grid, so a wall, a closed door or an opaque obstacle hides what is
// behind it; sound is damped by the cells it crosses. Noticing is not instant: each subject builds
// an awareness (0..1) while in view, which falls while out of view, and crosses two thresholds:
// suspicious and aware. Aware is kept until awareness falls under the suspicious threshold again.
// What was last seen is remembered for a while (the place, the time), so a guard can search where
// it last saw someone. Nothing here decides what a guard does about it: events and facts carry it
// out (perception.suspicious, perception.noticed, perception.lost, perception.heard).
namespace yk {
class GameContext;
class NavigationService;
class Entity;

enum class AwarenessState { Unaware, Suspicious, Aware };
const char *awarenessName(AwarenessState state);

// A character's tracks. What it does for the others: how visible it is, how loud its steps are,
// whether it is hidden. Optional: without it a character is seen and heard at normal strength.
class Perceivable final : public Component {
  public:
    float visibility{1.0F};  // Multiplies the distance it can be seen from.
    float walkNoise{3.0F};   // Meters its footsteps carry when walking; 0: silent.
    float runNoise{9.0F};    // Meters its footsteps carry when running.
    bool hidden{false};      // In a locker, under a bed: seen only from very close.
    float hiddenRange{0.8F}; // Meters it can still be seen from while hidden.
    static void describe(TypeBuilder<Perceivable> &type);
    void onFixedUpdate(GameContext &context, float seconds) override;

  private:
    Vec2 last_{};
    bool primed_{false};
    float stepTimer_{0.0F};
};

struct Awareness {
    EntityId subject;
    std::string who; // The subject's persistent id, so a saved game finds it again.
    float level{0.0F};
    AwarenessState state{AwarenessState::Unaware};
    bool seeing{false}; // In view this look.
    Vec2 lastKnown{};   // Where it was last seen or heard.
    int lastLevel{0};
    double lastSensed{0.0}; // Simulation time of that.
    std::string lastSense;  // "sight" or "sound"
};

struct NoiseHeard {
    Vec2 position{};
    int level{0};
    double time{0.0};
    std::string kind;
    EntityId source;
    float loudness{0.0F};
};

class Perceiver final : public Component {
  public:
    float sightRange{10.0F};        // Meters, in good light, at a subject of normal visibility.
    float fieldOfView{110.0F};      // Degrees, the whole cone in front.
    float peripheralRange{1.5F};    // Meters it notices in every direction.
    float hearingRange{14.0F};      // Farthest sound it can hear.
    float hearingSensitivity{1.0F}; // Multiplies how far a noise carries for it.
    float noticeSeconds{1.0F};      // In full view at the edge of its range it takes this long.
    float loseSeconds{5.0F};        // Out of view, awareness falls from 1 to 0 in this long.
    float memorySeconds{30.0F};     // What it last saw is remembered this long.
    float suspiciousAt{0.35F};
    float awareAt{1.0F};
    float lookInterval{0.1F};           // Seconds between looks.
    std::string interest{"unfriendly"}; // Who it watches: any, unfriendly (not friends), hostile.
    bool seeThroughDisguise{false};     // Faction by what they are, not what they wear.
    bool blind{false};                  // Switches the eyes off (a scripted blindfold).
    bool deaf{false};
    Vec2 lookDirection{0.0F, 1.0F}; // Where it faces without a CharacterMotor (a camera, a turret).
    static void describe(TypeBuilder<Perceiver> &type);

    void onStart(GameContext &context) override;
    void onDestroy(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;

    // ---- What it knows -------------------------------------------------------------------------
    const std::vector<Awareness> &awareness() const {
        return known_;
    }
    const Awareness *about(EntityId subject) const;
    float awarenessOf(EntityId subject) const;
    AwarenessState stateOf(EntityId subject) const;
    // The most aware subject at or above `atLeast` (null: none).
    const Awareness *strongest(AwarenessState atLeast = AwarenessState::Suspicious) const;
    const NoiseHeard *lastNoise() const {
        return heardAny_ ? &lastNoise_ : nullptr;
    }
    // True when the subject is in view right now.
    bool sees(EntityId subject) const;
    void forget(GameContext &context, EntityId subject);
    void forgetAll(GameContext &context);

    // The direction it faces.
    Vec2 facing() const;
    // The service delivers noises here.
    void hear(GameContext &context, const NoiseHeard &noise, float apparent);

  private:
    void look(GameContext &context, float seconds);
    Awareness &entry(EntityId subject);
    void classify(GameContext &context, Awareness &entry, AwarenessState before);
    std::vector<Awareness> known_;
    NoiseHeard lastNoise_;
    bool heardAny_{false};
    float carry_{0.0F};
    float sinceLook_{0.0F};
    bool started_{false};
};

// Perceivers register with the service; it carries noises to them. Footsteps come from Perceivable.
class PerceptionService final : public Service {
  public:
    const char *name() const override {
        return "perception";
    }
    void describe(std::vector<std::pair<std::string, std::string>> &rows) const override;

    void add(EntityId perceiver);
    void remove(EntityId perceiver);
    const std::vector<EntityId> &perceivers() const {
        return perceivers_;
    }
    // A sound at a place: `loudness` is how many meters it carries through open air to an ear of
    // sensitivity 1. `source` (may be empty) is who made it. Returns how many heard it.
    int makeNoise(GameContext &context, Vec2 position, int level, float loudness,
                  const std::string &kind, EntityId source = {});
    std::uint64_t noises() const {
        return noises_;
    }
    std::uint64_t looks() const {
        return looks_;
    }
    void countLook() {
        ++looks_;
    }

  private:
    std::vector<EntityId> perceivers_;
    std::uint64_t noises_{0};
    std::uint64_t looks_{0};
};

// Whether the perceiver could see the subject this instant (range, cone, light, line of sight).
bool canSee(GameContext &context, const Entity &perceiver, const Entity &subject);

void registerPerceptionRules(RuleCatalog &catalog);
void registerPerceptionComponents(ComponentRegistry &registry);
} // namespace yk
