#include "yk/runtime/EventBus.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>

namespace yk {
std::string GameEvent::category() const {
    const auto dot = name.find('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

bool EventBus::matches(const std::string &pattern, const std::string &name) {
    if (pattern == name || pattern == anyEvent)
        return true;
    // "crime.*" matches everything below the prefix "crime.".
    if (pattern.size() >= 2 && pattern.ends_with(".*"))
        return name.size() > pattern.size() - 1 &&
               name.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
    return false;
}
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
void EventBus::emitAfter(GameEvent event, double delaySeconds) {
    if (!(delaySeconds > 0.0)) {
        emit(std::move(event));
        return;
    }
    delayed_.push_back({clock_ + delaySeconds, std::move(event)});
}
void EventBus::advance(double seconds) {
    clock_ += seconds;
    if (delayed_.empty())
        return;
    // Events due at the same time keep the order they were scheduled in.
    std::vector<Delayed> remaining;
    for (Delayed &item : delayed_) {
        if (item.due <= clock_ + 1e-9)
            queue_.push_back(std::move(item.event));
        else
            remaining.push_back(std::move(item));
    }
    delayed_ = std::move(remaining);
}
void EventBus::setHistoryLimit(std::size_t limit) {
    historyLimit_ = std::max<std::size_t>(1, limit);
    while (recent_.size() > historyLimit_)
        recent_.pop_front();
}
std::size_t EventBus::dispatch() {
    constexpr std::size_t limit = dispatchLimit;
    std::size_t delivered = 0;
    while (!queue_.empty()) {
        if (delivered >= limit) {
            log(LogLevel::Error, "events", "Event dispatch aborted: handlers keep emitting events");
            queue_.clear();
            break;
        }
        GameEvent event = std::move(queue_.front());
        queue_.pop_front();
        event.tick = tick_;
        event.time = time_;
        // Handlers may subscribe or unsubscribe: iterate a snapshot of ids and re-check each.
        std::vector<Subscription> targets;
        for (const Entry &entry : subscribers_)
            if (matches(entry.name, event.name))
                targets.push_back(entry.id);
        for (const Subscription id : targets) {
            const auto found = std::find_if(subscribers_.begin(), subscribers_.end(),
                                            [&](const Entry &entry) { return entry.id == id; });
            if (found != subscribers_.end()) {
                const Handler handler = found->handler; // The handler may unsubscribe itself.
                handler(event);
            }
        }
        if (logging_)
            log(LogLevel::Info, "events",
                event.name + " source=" + toString(event.source) +
                    " other=" + toString(event.other));
        recent_.push_back(std::move(event));
        if (recent_.size() > historyLimit_)
            recent_.pop_front();
        ++delivered;
        ++delivered_;
    }
    return delivered;
}
void EventBus::clear() {
    subscribers_.clear();
    queue_.clear();
    recent_.clear();
    delayed_.clear();
    delivered_ = 0;
    clock_ = 0;
}
} // namespace yk
