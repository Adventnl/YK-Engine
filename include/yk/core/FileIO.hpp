#pragma once
#include "yk/core/Result.hpp"
#include <filesystem>
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
} // namespace yk
