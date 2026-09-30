#pragma once
#include "yk/core/Result.hpp"
#include <filesystem>
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

// A program that a person starts by double-clicking has no terminal, so its messages also go to a
// file: one line per message with a local time stamp, written and flushed at once so a crash loses
// nothing.
struct LogFileOptions {
    std::filesystem::path path; // The current log, e.g. <logs>/editor.log.
    int keep{4}; // Earlier logs kept as editor.1.log, editor.2.log, ... (newest first).
};
// Rotates the earlier logs and starts writing to `options.path`. Replaces a log file that is
// already open. A failure (unwritable folder) leaves logging to stderr and the sink as before.
Status openLogFile(const LogFileOptions &options);
void closeLogFile();
// The file being written, or an empty path when there is none.
std::filesystem::path logFilePath();
} // namespace yk
