#include "yk/runtime/EventBus.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>

namespace yk {
EventBus::Subscription EventBus::subscribe(std::string name, Handler handler) {
    subscribers_.push_back({nextId_, std::move(name), std::move(handler)});
    return nextId_++;
}
void EventBus::unsubscribe(Subscription subscription) {
    std::erase_if(subscribers_, [&](const Entry &entry) { return entry.id == subscription; });
}
void EventBus::emit(GameEvent event) {
    queue_.push_back(std::move(event));
}
std::size_t EventBus::dispatch() {
    constexpr std::size_t limit = 1024;
    std::size_t delivered = 0;
    while (!queue_.empty()) {
        if (delivered >= limit) {
            log(LogLevel::Error, "events", "Event dispatch aborted: handlers keep emitting events");
            queue_.clear();
            break;
        }
        const GameEvent event = std::move(queue_.front());
        queue_.pop_front();
        // Handlers may subscribe or unsubscribe: iterate a snapshot of ids and re-check each.
        std::vector<Subscription> targets;
        for (const Entry &entry : subscribers_)
            if (entry.name == event.name || entry.name == anyEvent)
                targets.push_back(entry.id);
        for (const Subscription id : targets) {
            const auto found = std::find_if(subscribers_.begin(), subscribers_.end(),
                                            [&](const Entry &entry) { return entry.id == id; });
            if (found != subscribers_.end()) {
                const Handler handler = found->handler; // The handler may unsubscribe itself.
                handler(event);
            }
        }
        recent_.push_back(event);
        if (recent_.size() > 64)
            recent_.pop_front();
        ++delivered;
    }
    return delivered;
}
void EventBus::clear() {
    subscribers_.clear();
    queue_.clear();
    recent_.clear();
}
} // namespace yk
