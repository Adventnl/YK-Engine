#pragma once
#include "yk/core/Rng.hpp"
#include "yk/runtime/Services.hpp"

namespace yk {
// The running game's dice. Everything that rolls (loot, chance conditions, who a guard picks to
// search) draws from this one generator, seeded from RuntimeOptions::randomSeed, so a run is
// reproducible and a save game can store the generator's state with the rest of the world.
class RandomService final : public Service {
  public:
    Rng rng{0x5EED5EEDULL};
    const char *name() const override {
        return "random";
    }
    std::string saveKey() const override {
        return "random";
    }
    Json saveState() const override {
        Json state = Json::object();
        state.set("a", std::to_string(rng.state(0)));
        state.set("b", std::to_string(rng.state(1)));
        return state;
    }
    Status loadState(GameContext &, const Json &state) override {
        try {
            rng.setState(std::stoull(state.get("a").asString()),
                         std::stoull(state.get("b").asString()));
        } catch (const std::exception &) {
            return Error{"random: the saved generator state is not valid"};
        }
        return success();
    }
};
} // namespace yk
