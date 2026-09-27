#pragma once
#include <string_view>
namespace yk {
enum class LogLevel { Info, Warning, Error };
void log(LogLevel level, std::string_view subsystem, std::string_view message);
} // namespace yk
