#include "yk/core/Process.hpp"

#if defined(_WIN32)
namespace yk {
Result<ProcessResult> runProcess(const std::vector<std::string> &) {
    return Error{"Running external tools is not supported on Windows"};
}
} // namespace yk
#else
#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#else
extern char *
    *environ; // Declared at global scope: inside namespace yk it would name another symbol.
#endif

namespace yk {
namespace {
char **environment() {
#if defined(__APPLE__)
    return *_NSGetEnviron();
#else
    return environ;
#endif
}
} // namespace

Result<ProcessResult> runProcess(const std::vector<std::string> &arguments) {
    if (arguments.empty() || arguments.front().empty())
        return Error{"No program to run"};
    int pipes[2];
    if (pipe(pipes) != 0)
        return Error{std::string("Cannot create a pipe: ") + std::strerror(errno)};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipes[1], 1);
    posix_spawn_file_actions_adddup2(&actions, pipes[1], 2);
    posix_spawn_file_actions_addclose(&actions, pipes[0]);
    posix_spawn_file_actions_addclose(&actions, pipes[1]);
    std::vector<char *> argv;
    for (const std::string &argument : arguments)
        argv.push_back(const_cast<char *>(argument.c_str()));
    argv.push_back(nullptr);
    pid_t child = 0;
    const int started =
        posix_spawnp(&child, argv[0], &actions, nullptr, argv.data(), environment());
    posix_spawn_file_actions_destroy(&actions);
    close(pipes[1]);
    if (started != 0) {
        close(pipes[0]);
        return Error{"Cannot start '" + arguments.front() + "': " + std::strerror(started)};
    }
    ProcessResult result;
    char buffer[4096];
    for (;;) {
        const ssize_t count = read(pipes[0], buffer, sizeof buffer);
        if (count > 0) {
            result.output.append(buffer, static_cast<std::size_t>(count));
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    close(pipes[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status)
                                        : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    return result;
}
} // namespace yk
#endif
