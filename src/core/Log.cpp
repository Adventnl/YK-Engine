#include "yk/core/Log.hpp"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>

namespace yk {
namespace fs = std::filesystem;
namespace {
std::mutex &logMutex() {
    static std::mutex mutex;
    return mutex;
}
LogSink &sink() {
    static LogSink value;
    return value;
}
bool &stderrEnabled() {
    static bool value = true;
    return value;
}
struct LogFile {
    std::ofstream stream;
    fs::path path;
};
LogFile &file() {
    static LogFile value;
    return value;
}

const char *levelName(LogLevel level) {
    return level == LogLevel::Info ? "info" : level == LogLevel::Warning ? "warning" : "error";
}

// "2026-09-30 14:03:11.123" in local time.
std::string timeStamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() %
        1000;
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char text[96]; // Roomy: the compiler sizes worst-case ints, not real dates.
    std::snprintf(text, sizeof text, "%04d-%02d-%02d %02d:%02d:%02d.%03d", local.tm_year + 1900,
                  local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec,
                  static_cast<int>(millis));
    return text;
}

// editor.log -> editor.1.log, editor.1.log -> editor.2.log, ...; the one past `keep` is deleted.
void rotate(const fs::path &path, int keep) {
    std::error_code ignored;
    const fs::path directory = path.parent_path();
    const std::string stem = path.stem().string();
    const std::string extension = path.extension().string();
    const auto numbered = [&](int index) {
        return directory / (stem + "." + std::to_string(index) + extension);
    };
    if (keep < 1)
        keep = 1;
    fs::remove(numbered(keep), ignored);
    for (int index = keep - 1; index >= 1; --index)
        if (fs::exists(numbered(index), ignored))
            fs::rename(numbered(index), numbered(index + 1), ignored);
    if (fs::exists(path, ignored))
        fs::rename(path, numbered(1), ignored);
}
} // namespace

void log(LogLevel level, std::string_view subsystem, std::string_view message) {
    const char *name = levelName(level);
    std::lock_guard lock(logMutex());
    if (stderrEnabled())
        std::fprintf(stderr, "[%s][%.*s] %.*s\n", name, static_cast<int>(subsystem.size()),
                     subsystem.data(), static_cast<int>(message.size()), message.data());
    if (file().stream.is_open()) {
        file().stream << timeStamp() << " [" << name << "][" << subsystem << "] " << message
                      << std::endl; // std::endl flushes: a crash must not lose the last lines.
    }
    if (sink())
        sink()(level, subsystem, message);
}
void setLogSink(LogSink value) {
    std::lock_guard lock(logMutex());
    sink() = std::move(value);
}
void setLogStderrEnabled(bool enabled) {
    std::lock_guard lock(logMutex());
    stderrEnabled() = enabled;
}

Status openLogFile(const LogFileOptions &options) {
    if (options.path.empty())
        return Error{"No log file path given"};
    std::lock_guard lock(logMutex());
    file().stream.close();
    file().path.clear();
    std::error_code error;
    if (options.path.has_parent_path()) {
        fs::create_directories(options.path.parent_path(), error);
        if (error)
            return Error{"Cannot create the log folder '" + options.path.parent_path().string() +
                         "': " + error.message()};
    }
    rotate(options.path, options.keep);
    file().stream.open(options.path, std::ios::out | std::ios::trunc);
    if (!file().stream.is_open())
        return Error{"Cannot open the log file '" + options.path.string() + "' for writing"};
    file().path = options.path;
    return success();
}
void closeLogFile() {
    std::lock_guard lock(logMutex());
    file().stream.close();
    file().path.clear();
}
fs::path logFilePath() {
    std::lock_guard lock(logMutex());
    return file().path;
}
} // namespace yk
