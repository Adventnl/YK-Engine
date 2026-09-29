#pragma once
#include <functional>
#include <string_view>

namespace yk {
enum class LogLevel { Info, Warning, Error };
void log(LogLevel level, std::string_view subsystem, std::string_view message);

// Receives every message after it is written to stderr (tools use this for an in-app console).
// The sink is invoked while holding the logging lock: it must not call yk::log. Pass an empty
// function to remove it. Logging is safe to call from any thread.
using LogSink = std::function<void(LogLevel, std::string_view subsystem, std::string_view message)>;
void setLogSink(LogSink sink);
// Tests silence stderr; the sink still receives messages.
void setLogStderrEnabled(bool enabled);
} // namespace yk
