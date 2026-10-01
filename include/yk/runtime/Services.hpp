#pragma once
#include "yk/core/Json.hpp"
#include "yk/core/Result.hpp"
#include "yk/scene/Component.hpp"
#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yk {
class GameContext;

// A piece of simulation that belongs to the running game as a whole rather than to one entity:
// the world clock, the navigation world, the spatial index, the security state. Components reach
// services through GameContext::services(), never through globals, so every running game (and every
// test) has its own, with an explicit owner (the runtime) and an explicit order (the phase).
//
// A service is created the first time something asks for it (`services().get<T>()`), starts
// immediately, is ticked once per fixed tick in its phase (before the components of that phase),
// and is shut down when the runtime is rebuilt or destroyed. Services that carry state a save game
// must restore override saveKey()/saveState()/loadState().
class Service {
  public:
    Service() = default;
    Service(const Service &) = delete;
    Service &operator=(const Service &) = delete;
    virtual ~Service() = default;

    virtual const char *name() const = 0;
    virtual UpdatePhase phase() const {
        return UpdatePhase::PostSimulation;
    }
    virtual void onStart(GameContext &) {}
    virtual void onFixedUpdate(GameContext &, float /*seconds*/) {}
    virtual void onShutdown(GameContext &) {}

    // Save games: a stable key (empty means "not saved"), the state, and how to put it back. The
    // state must be plain JSON the same code version can read; never raw memory.
    virtual std::string saveKey() const {
        return {};
    }
    virtual Json saveState() const {
        return Json();
    }
    virtual Status loadState(GameContext &, const Json &) {
        return success();
    }
    // Rows for the editor's Debug panel (label, value).
    virtual void describe(std::vector<std::pair<std::string, std::string>> &) const {}
};

class Services {
  public:
    explicit Services(GameContext &owner) : owner_(&owner) {}
    Services(const Services &) = delete;
    Services &operator=(const Services &) = delete;
    ~Services() = default;

    // The service of type T, creating (and starting) it on first use. T must derive from Service
    // and be default constructible.
    template <class T> T &get() {
        static_assert(std::is_base_of_v<Service, T>, "a service derives from Service");
        static_assert(std::is_default_constructible_v<T>, "a service is default constructible");
        const std::type_index key(typeid(T));
        if (const auto found = byType_.find(key); found != byType_.end())
            return static_cast<T &>(*found->second);
        auto created = std::make_unique<T>();
        T &reference = *created;
        byType_.emplace(key, &reference);
        ordered_.push_back(std::move(created));
        start(reference);
        return reference;
    }
    template <class T> T *find() const {
        const auto found = byType_.find(std::type_index(typeid(T)));
        return found == byType_.end() ? nullptr : static_cast<T *>(found->second);
    }
    template <class T> bool has() const {
        return find<T>() != nullptr;
    }

    // Runs onFixedUpdate of every service of `phase` (services created meanwhile wait for the
    // next tick).
    void tick(UpdatePhase phase, float seconds);
    // Shuts every service down in reverse creation order and forgets them.
    void shutdown();
    // Creation order; for tools (the Debug panel, save games).
    std::vector<Service *> all() const;
    Service *byKey(const std::string &saveKey) const;

  private:
    void start(Service &service);
    GameContext *owner_;
    std::vector<std::unique_ptr<Service>> ordered_;
    std::unordered_map<std::type_index, Service *> byType_;
};
} // namespace yk
