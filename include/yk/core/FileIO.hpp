#pragma once
#include "yk/core/Result.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace yk {
// Whole-file text IO with contextual errors. Text is read and written byte-for-byte (UTF-8).
Result<std::string> readTextFile(const std::filesystem::path &path);
// Writes a same-directory temporary file, then replaces the target so a crash never leaves a
// truncated file. Missing parent directories are created.
Status writeTextFileAtomic(const std::filesystem::path &path, std::string_view contents);
// Path with '/' separators regardless of platform, for identifiers stored in data files.
std::string toPortablePath(const std::filesystem::path &path);
// A file:// URL for an absolute path, with the characters a URL cannot hold escaped, for asking the
// operating system to open a folder (SDL_OpenURL).
std::string toFileUrl(const std::filesystem::path &path);
// The value of an environment variable, or nothing when it is not set (std::getenv is rejected by
// MSVC's deprecation warnings, which this project treats as errors).
std::optional<std::string> environmentVariable(std::string_view name);
} // namespace yk
