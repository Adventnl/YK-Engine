#pragma once
#include "yk/core/Log.hpp"
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace yk::editor {
// Collects everything the engine logs so the editor can show it in its console panel. Installing it
// takes over the engine's log sink; only one instance should be alive at a time.
class ConsoleLog {
  public:
    struct Entry {
        LogLevel level{LogLevel::Info};
        std::string subsystem;
        std::string message;
        std::uint64_t serial{};
    };
    static constexpr std::size_t capacity = 2000;

    ConsoleLog();
    ~ConsoleLog();
    ConsoleLog(const ConsoleLog &) = delete;
    ConsoleLog &operator=(const ConsoleLog &) = delete;

    void clear();
    // Bumps whenever entries change; lets the UI skip copying when nothing is new.
    std::uint64_t version() const;
    std::vector<Entry> snapshot() const;
    // The newest entry, or a default one when empty.
    Entry latest() const;
    std::size_t count(LogLevel level) const;

  private:
    void add(LogLevel level, std::string_view subsystem, std::string_view message);

    mutable std::mutex mutex_;
    std::deque<Entry> entries_;
    std::uint64_t serial_{};
    std::uint64_t version_{};
};
} // namespace yk::editor
