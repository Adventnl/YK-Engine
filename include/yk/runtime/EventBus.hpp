#pragma once
#include "yk/core/Json.hpp"
#include "yk/scene/EntityId.hpp"
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace yk {
struct GameEvent {
    GameEvent() = default;
    GameEvent(std::string eventName, EntityId from = {}, EntityId to = {}, Json payload = {})
        : name(std::move(eventName)), source(from), other(to), data(std::move(payload)) {}
    std::string name;
    EntityId source{}; // Entity that raised the event.
    EntityId
        other{}; // Optional second party (the visitor of a trigger, the victim of a hazard...).
    // Optional payload: an object of named values ("severity", "item", ...). Null when there is
    // none. Consumers (rules, scripts, the event inspector) read it by key.
    Json data;
    // Stamped on delivery: the fixed tick and the simulated time at which handlers saw it.
    std::uint64_t tick{};
    double time{};

    // The part of the name before the first '.', or the whole name ("crime.witnessed" -> "crime").
    // Events with dotted names group into categories the inspector can filter by.
    std::string category() const;
};

// Named events between gameplay components. Emission only queues; the runtime delivers events at
// safe points, so a handler may freely change entities without invalidating the code that raised
// the event. Handlers may emit further events; delivery is bounded to stop runaway loops.
//
// A subscription names one event ("plate_pressed"), every event ("*") or a category prefix
// ("crime.*" matches "crime.witnessed"). Handlers of one event run in the order they subscribed.
class EventBus {
  public:
    using Handler = std::function<void(const GameEvent &)>;
    using Subscription = std::uint64_t;
    static constexpr const char *anyEvent = "*";
    // Events delivered by one dispatch() before it gives up (handlers that keep emitting events).
    // Large simulations legitimately deliver many events a tick, so the cap is generous.
    static constexpr std::size_t dispatchLimit = 4096;

    Subscription subscribe(std::string name, Handler handler);
    void unsubscribe(Subscription subscription);
    void emit(GameEvent event);
    // Delivers the event after `delaySeconds` of simulated time (advance() counts it down).
    void emitAfter(GameEvent event, double delaySeconds);
    // Moves delayed events whose time has come into the queue. Called once per fixed tick.
    void advance(double seconds);
    std::size_t dispatch(); // Returns the number of events delivered.
    std::size_t pending() const {
        return queue_.size();
    }
    std::size_t delayed() const {
        return delayed_.size();
    }
    // Set by the runtime for the stamps on delivered events.
    void setClock(std::uint64_t tick, double time) {
        tick_ = tick;
        time_ = time;
    }
    // The most recently delivered events (oldest first), for debugging and tests. The capacity is
    // 64 events unless a tool (the editor's event inspector) asks for more.
    const std::deque<GameEvent> &recent() const {
        return recent_;
    }
    void setHistoryLimit(std::size_t limit);
    // Total events delivered since creation or clear(); the profiler shows the difference per tick.
    std::uint64_t deliveredTotal() const {
        return delivered_;
    }
    // Logs every delivered event at info level (name, source, other) while enabled.
    void setLogging(bool enabled) {
        logging_ = enabled;
    }
    void clear();

  private:
    struct Entry {
        Subscription id;
        std::string name;
        Handler handler;
    };
    struct Delayed {
        double due;
        GameEvent event;
    };
    static bool matches(const std::string &pattern, const std::string &name);
    std::vector<Entry> subscribers_;
    std::deque<GameEvent> queue_;
    std::deque<GameEvent> recent_;
    std::vector<Delayed> delayed_;
    Subscription nextId_{1};
    std::size_t historyLimit_{64};
    std::uint64_t tick_{};
    double time_{};
    double clock_{}; // Seconds advanced, for delayed events.
    std::uint64_t delivered_{};
    bool logging_{};
};
} // namespace yk
