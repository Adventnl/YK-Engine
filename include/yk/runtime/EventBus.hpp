#pragma once
#include "yk/scene/EntityId.hpp"
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace yk {
struct GameEvent {
    std::string name;
    EntityId source{}; // Entity that raised the event.
    EntityId
        other{}; // Optional second party (the visitor of a trigger, the victim of a hazard...).
};

// Named events between gameplay components. Emission only queues; the runtime delivers events at
// safe points, so a handler may freely change entities without invalidating the code that raised
// the event. Handlers may emit further events; delivery is bounded to stop runaway loops.
class EventBus {
  public:
    using Handler = std::function<void(const GameEvent &)>;
    using Subscription = std::uint64_t;
    static constexpr const char *anyEvent = "*";

    Subscription subscribe(std::string name, Handler handler);
    void unsubscribe(Subscription subscription);
    void emit(GameEvent event);
    std::size_t dispatch(); // Returns the number of events delivered.
    std::size_t pending() const {
        return queue_.size();
    }
    // The most recently delivered events (oldest first), for debugging and tests.
    const std::deque<GameEvent> &recent() const {
        return recent_;
    }
    void clear();

  private:
    struct Entry {
        Subscription id;
        std::string name;
        Handler handler;
    };
    std::vector<Entry> subscribers_;
    std::deque<GameEvent> queue_;
    std::deque<GameEvent> recent_;
    Subscription nextId_{1};
};
} // namespace yk
