#include "yk/core/Log.hpp"
#include <cstdio>
namespace yk {
void log(LogLevel level, std::string_view subsystem, std::string_view message) {
    const char *name = level == LogLevel::Info      ? "info"
                       : level == LogLevel::Warning ? "warning"
                                                    : "error";
    std::fprintf(stderr, "[%s][%.*s] %.*s\n", name, static_cast<int>(subsystem.size()),
                 subsystem.data(), static_cast<int>(message.size()), message.data());
}
} // namespace yk
