#include "core/ConsoleLog.hpp"
#include <algorithm>

namespace yk::editor {
ConsoleLog::ConsoleLog() {
    setLogSink([this](LogLevel level, std::string_view subsystem, std::string_view message) {
        add(level, subsystem, message);
    });
}

ConsoleLog::~ConsoleLog() {
    setLogSink({});
}

void ConsoleLog::add(LogLevel level, std::string_view subsystem, std::string_view message) {
    std::lock_guard lock(mutex_);
    entries_.push_back({level, std::string(subsystem), std::string(message), ++serial_});
    if (entries_.size() > capacity)
        entries_.pop_front();
    ++version_;
}

void ConsoleLog::clear() {
    std::lock_guard lock(mutex_);
    entries_.clear();
    ++version_;
}

std::uint64_t ConsoleLog::version() const {
    std::lock_guard lock(mutex_);
    return version_;
}

std::vector<ConsoleLog::Entry> ConsoleLog::snapshot() const {
    std::lock_guard lock(mutex_);
    return {entries_.begin(), entries_.end()};
}

ConsoleLog::Entry ConsoleLog::latest() const {
    std::lock_guard lock(mutex_);
    return entries_.empty() ? Entry{} : entries_.back();
}

std::size_t ConsoleLog::count(LogLevel level) const {
    std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(
        std::count_if(entries_.begin(), entries_.end(),
                      [level](const Entry &entry) { return entry.level == level; }));
}
} // namespace yk::editor
