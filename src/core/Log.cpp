#include "yk/core/Log.hpp"
#include <cstdio>
#include <mutex>

namespace yk {
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
} // namespace
void log(LogLevel level, std::string_view subsystem, std::string_view message) {
    const char *name = level == LogLevel::Info      ? "info"
                       : level == LogLevel::Warning ? "warning"
                                                    : "error";
    std::lock_guard lock(logMutex());
    if (stderrEnabled())
        std::fprintf(stderr, "[%s][%.*s] %.*s\n", name, static_cast<int>(subsystem.size()),
                     subsystem.data(), static_cast<int>(message.size()), message.data());
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
} // namespace yk
