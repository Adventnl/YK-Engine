#pragma once
#include <cstdint>

namespace yk {
// A small deterministic random number generator (splitmix64 seeding a xoshiro-style state of two
// words, the PCG-like "xorshift128+"). Two numbers are its whole state, so a save game can store it
// and a replay of the same inputs reproduces the same dice. Not for cryptography.
class Rng {
  public:
    explicit Rng(std::uint64_t seed = 1) {
        reseed(seed);
    }
    void reseed(std::uint64_t seed) {
        state_[0] = splitmix(seed);
        state_[1] = splitmix(seed);
        if (state_[0] == 0 && state_[1] == 0)
            state_[1] = 0x9E3779B97F4A7C15ULL;
    }
    std::uint64_t next() {
        std::uint64_t a = state_[0];
        const std::uint64_t b = state_[1];
        state_[0] = b;
        a ^= a << 23;
        state_[1] = a ^ b ^ (a >> 18) ^ (b >> 5);
        return state_[1] + b;
    }
    // In [0, 1).
    double uniform() {
        return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0);
    }
    // In [low, high] (inclusive); low when the range is empty.
    int range(int low, int high) {
        if (high <= low)
            return low;
        const auto span = static_cast<std::uint64_t>(static_cast<std::int64_t>(high) - low + 1);
        return static_cast<int>(static_cast<std::int64_t>(next() % span) + low);
    }
    bool chance(double probability) {
        return uniform() < probability;
    }
    std::uint64_t state(int word) const {
        return state_[word & 1];
    }
    void setState(std::uint64_t first, std::uint64_t second) {
        state_[0] = first;
        state_[1] = second;
        if (state_[0] == 0 && state_[1] == 0)
            state_[1] = 1;
    }

  private:
    static std::uint64_t splitmix(std::uint64_t &seed) {
        std::uint64_t z = (seed += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    std::uint64_t state_[2]{};
};
} // namespace yk
