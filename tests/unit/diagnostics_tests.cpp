// Paths, log files, crash reports and the session marker: what a program nobody watches in a
// terminal leaves behind. The crash cases really crash a child process and read its report.
#include "support/check.hpp"
#include "yk/core/AppPaths.hpp"
#include "yk/core/Diagnostics.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#if !defined(_WIN32)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace yk;

namespace {
void setEnvironment(const char *name, const char *value) {
#if defined(_WIN32)
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

fs::path scratchFolder(const std::string &name) {
    const fs::path folder = fs::temp_directory_path() / ("yk-diagnostics-test-" + name);
    std::error_code error;
    fs::remove_all(folder, error);
    fs::create_directories(folder, error);
    return folder;
}

std::string text(const fs::path &file) {
    auto contents = readTextFile(file);
    return contents ? contents.value() : std::string{};
}

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

std::size_t countFiles(const fs::path &folder, const std::string &prefix) {
    std::size_t count = 0;
    std::error_code error;
    for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
        if (it->path().filename().string().starts_with(prefix))
            ++count;
    return count;
}

void paths() {
    const fs::path bundle = fs::path("/Apps") / "YK Engine.app" / "Contents";
    CHECK(bundleResourcesFor(bundle / "MacOS") == bundle / "Resources");
    CHECK(bundleResourcesFor("/build/release").empty());
    CHECK(bundleResourcesFor(fs::path("/Apps") / "Thing" / "Contents" / "MacOS").empty());
    CHECK(bundleResourcesFor(bundle / "Resources").empty());

    const fs::path program = executablePath();
    CHECK(!program.empty());
    std::error_code error;
    CHECK(fs::is_regular_file(program, error));
    CHECK(executableDirectory() == program.parent_path());
    CHECK(!homeDirectory().empty());

    setEnvironment("YK_LOG_DIR", nullptr);
    const UserDirectories editor = userDirectories("YKEngine", "Editor");
    CHECK(editor.support.filename() == "Editor" || editor.support.filename() == "Editor/");
    CHECK(!editor.logs.empty());
    const UserDirectories game = userDirectories("", "Cinder Vale");
    CHECK(contains(game.support.generic_string(), "Cinder Vale"));
    CHECK(!contains(game.support.generic_string(), "//"));
#if defined(__APPLE__)
    setEnvironment("HOME", "/Users/someone");
    CHECK(userDirectories("YKEngine", "Editor").logs ==
          fs::path("/Users/someone/Library/Logs/YKEngine/Editor"));
    CHECK(userDirectories("YKEngine", "Editor").support ==
          fs::path("/Users/someone/Library/Application Support/YKEngine/Editor"));
#elif !defined(_WIN32)
    setEnvironment("HOME", "/home/someone");
    setEnvironment("XDG_STATE_HOME", nullptr);
    setEnvironment("XDG_DATA_HOME", nullptr);
    CHECK(userDirectories("YKEngine", "Editor").logs ==
          fs::path("/home/someone/.local/state/YKEngine/Editor/logs"));
    CHECK(userDirectories("YKEngine", "Editor").support ==
          fs::path("/home/someone/.local/share/YKEngine/Editor"));
    setEnvironment("XDG_STATE_HOME", "/elsewhere/state");
    CHECK(userDirectories("YKEngine", "Editor").logs ==
          fs::path("/elsewhere/state/YKEngine/Editor/logs"));
    setEnvironment("XDG_STATE_HOME", "relative/state"); // The XDG rules ignore relative values.
    CHECK(userDirectories("YKEngine", "Editor").logs ==
          fs::path("/home/someone/.local/state/YKEngine/Editor/logs"));
    setEnvironment("XDG_STATE_HOME", nullptr);
#endif
    setEnvironment("YK_LOG_DIR", "/somewhere/logs");
    CHECK(userDirectories("YKEngine", "Editor").logs == fs::path("/somewhere/logs"));
    setEnvironment("YK_LOG_DIR", nullptr);
}

void logFiles() {
    const fs::path folder = scratchFolder("log");
    setLogStderrEnabled(false);
    CHECK(!openLogFile({}));
    CHECK(logFilePath().empty());

    const fs::path file = folder / "nested" / "run.log";
    CHECK(openLogFile({file, 2}));
    CHECK(logFilePath() == file);
    log(LogLevel::Info, "test", "first message");
    log(LogLevel::Warning, "test", "a warning");
    log(LogLevel::Error, "other", "an error");
    // Flushed at once: readable while the file is still open, which is what a crash needs.
    std::string contents = text(file);
    CHECK(contains(contents, "[info][test] first message"));
    CHECK(contains(contents, "[warning][test] a warning"));
    CHECK(contains(contents, "[error][other] an error"));
    CHECK(contents.size() > 30 && contents[0] == '2' && contents[4] == '-' && contents[13] == ':');

    // Each start rotates the earlier log; only `keep` earlier ones are kept.
    CHECK(openLogFile({file, 2}));
    log(LogLevel::Info, "test", "second run");
    CHECK(openLogFile({file, 2}));
    log(LogLevel::Info, "test", "third run");
    CHECK(openLogFile({file, 2}));
    log(LogLevel::Info, "test", "fourth run");
    closeLogFile();
    CHECK(logFilePath().empty());
    CHECK(contains(text(file), "fourth run"));
    CHECK(contains(text(folder / "nested" / "run.1.log"), "third run"));
    CHECK(contains(text(folder / "nested" / "run.2.log"), "second run"));
    CHECK(!fs::exists(folder / "nested" / "run.3.log"));
    // Not written once closed.
    log(LogLevel::Info, "test", "after close");
    CHECK(!contains(text(file), "after close"));

    // An unwritable place is an error the caller can report, not a crash.
    const fs::path blocker = folder / "not-a-folder";
    { std::ofstream(blocker) << "x"; }
    CHECK(!openLogFile({blocker / "sub" / "x.log"}));
    setLogStderrEnabled(true);
}

void sessionMarker() {
    const fs::path folder = scratchFolder("session");
    SessionInfo info;
    info.application = "YK Test";
    info.version = "1.2.3";
    info.logDirectory = folder;
    info.logName = "app";
    info.installHandlers = false;
    setLogStderrEnabled(false);
    {
        DiagnosticsSession first(info);
        CHECK(!first.previousEndedUncleanly());
        CHECK(first.logFile() == folder / "app.log");
        CHECK(countFiles(folder, "app-") == 1); // The running marker.
        CHECK(contains(text(folder / "app.log"), "YK Test 1.2.3"));
    }
    CHECK(countFiles(folder, "app-") == 0); // A clean end removes it.
    CHECK(contains(text(folder / "app.log"), "Clean shutdown"));

    // A marker of a process that is gone, and its report: the next start reports a crash once.
    const std::string deadPid = "2000000000";
    { std::ofstream(folder / ("app-" + deadPid + ".session")) << "started\n"; }
    { std::ofstream(folder / ("crash-app-" + deadPid + "-1700000000.txt")) << "report\n"; }
    {
        DiagnosticsSession second(info);
        CHECK(second.previousEndedUncleanly());
        CHECK(second.previousCrashReport().filename() ==
              "crash-app-" + deadPid + "-1700000000.txt");
        CHECK(contains(text(folder / "app.log"), "The previous session crashed"));
    }
    {
        DiagnosticsSession third(info);
        CHECK(!third.previousEndedUncleanly());
    }
    // A marker without a report (the process was killed).
    const std::string killedPid = "2000000001";
    { std::ofstream(folder / ("app-" + killedPid + ".session")) << "started\n"; }
    {
        DiagnosticsSession fourth(info);
        CHECK(fourth.previousEndedUncleanly());
        CHECK(fourth.previousCrashReport().empty());
    }
    // Another program's markers are not ours, and a running program is not a crash.
    { std::ofstream(folder / ("other-" + deadPid + ".session")) << "started\n"; }
    {
        DiagnosticsSession fifth(info);
        CHECK(!fifth.previousEndedUncleanly());
    }
    // No folder: diagnostics quietly do nothing.
    SessionInfo nowhere = info;
    nowhere.logDirectory.clear();
    {
        DiagnosticsSession sixth(nowhere);
        CHECK(sixth.logFile().empty());
    }
    setLogStderrEnabled(true);
}

#if !defined(_WIN32)
// Starts a child that installs the crash handler, then crashes it as `kind` says. Returns the
// signal that ended it.
int crashChild(const fs::path &folder, const std::string &kind) {
    std::fflush(nullptr);
    const pid_t child = fork();
    if (child == 0) {
        setLogStderrEnabled(false);
        SessionInfo info;
        info.application = "YK Test";
        info.version = "1.2.3";
        info.logDirectory = folder;
        info.logName = "app";
        DiagnosticsSession session(info);
        crashOnPurpose(kind);
        _exit(99); // Not reached: the crash ends the process.
    }
    int status = 0;
    waitpid(child, &status, 0);
    return WIFSIGNALED(status) ? WTERMSIG(status) : -1;
}

fs::path onlyCrashReport(const fs::path &folder) {
    fs::path found;
    std::error_code error;
    for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
        if (it->path().filename().string().starts_with("crash-app-"))
            found = it->path();
    return found;
}

void crashes() {
    CHECK(!crashOnPurpose("nonsense"));
    for (const char *kind : {"abort", "throw", "segv"}) {
        const fs::path folder = scratchFolder(std::string("crash-") + kind);
        const int expected = std::string(kind) == "segv" ? SIGSEGV : SIGABRT;
        const int signalNumber = crashChild(folder, kind);
        CHECK(signalNumber == expected);
        const fs::path report = onlyCrashReport(folder);
        CHECK(!report.empty());
        const std::string contents = text(report);
        CHECK(contains(contents, "YK Engine crash report"));
        CHECK(contains(contents, "Application: YK Test 1.2.3"));
        CHECK(contains(contents, "Log file: " + (folder / "app.log").string()));
        CHECK(contains(contents, std::string("Signal: ") + std::to_string(expected)));
#if defined(YK_TEST_EXPECT_STACK_TRACE)
        CHECK(contains(contents, "Stack trace"));
#endif
        if (std::string(kind) == "throw")
            CHECK(contains(text(folder / "app.log"), "Unhandled exception: crash on purpose"));

        // The crashed child never removed its marker, so the next session says what happened.
        SessionInfo info;
        info.application = "YK Test";
        info.version = "1.2.3";
        info.logDirectory = folder;
        info.logName = "app";
        info.installHandlers = false;
        setLogStderrEnabled(false);
        {
            DiagnosticsSession next(info);
            CHECK(next.previousEndedUncleanly());
            CHECK(next.previousCrashReport() == report);
        }
        setLogStderrEnabled(true);
    }
}
#endif
} // namespace

int main() {
    paths();
    logFiles();
    sessionMarker();
#if !defined(_WIN32)
    crashes();
#endif
    return yk::test::finish("diagnostics");
}
