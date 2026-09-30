#pragma once
#include "yk/core/Result.hpp"
#include <functional>
#include <string>
#include <vector>

// Running another program and waiting for it, for the few packaging steps that only the operating
// system's own tools can do (hdiutil and codesign on macOS). Not a shell: the arguments are passed
// as they are, so file names with spaces or quotes are safe.
namespace yk {
struct ProcessResult {
    int exitCode{};
    std::string output; // Standard output and standard error together.
};
// Starts arguments[0] (looked up in PATH), waits for it and returns how it ended. A program that
// cannot be started is an error; one that ran and failed is a result with a non-zero exit code.
// Not available on Windows (always an error there).
Result<ProcessResult> runProcess(const std::vector<std::string> &arguments);

// How packaging code runs external tools; tests hand in a stand-in.
using ToolRunner = std::function<Result<ProcessResult>(const std::vector<std::string> &)>;
} // namespace yk
