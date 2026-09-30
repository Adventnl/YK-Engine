#include "yk/core/Diagnostics.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <stdexcept>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#if __has_include(<execinfo.h>)
#include <execinfo.h> // macOS and glibc; musl has no stack traces.
#define YK_HAS_BACKTRACE 1
#endif
#endif

namespace yk {
namespace fs = std::filesystem;

namespace {
long processId() {
#if defined(_WIN32)
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(getpid());
#endif
}

bool processAlive(long pid) {
#if defined(_WIN32)
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (handle == nullptr)
        return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(handle, &code) != 0 && code == STILL_ACTIVE;
    CloseHandle(handle);
    return alive;
#else
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

const char *platformName() {
#if defined(__APPLE__)
#if defined(__aarch64__)
    return "macOS arm64";
#else
    return "macOS x86_64";
#endif
#elif defined(_WIN32)
    return "Windows x64";
#else
#if defined(__aarch64__)
    return "Linux arm64";
#else
    return "Linux x86_64";
#endif
#endif
}

std::string localTimeText() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &local);
    return text;
}

// ------------------------------------------------------------------------------- crash reports
// Everything a crash handler needs is prepared when it is installed: a signal handler may not
// allocate, take locks or format text, so it only fills in a file name from these buffers, writes
// them and asks the C library for a stack trace.
struct CrashState {
    char directory[768]{};
    char stem[160]{}; // "crash-editor-"
    char header[2048]{};
    std::size_t headerLength{};
    bool installed{};
};
CrashState crashState;

std::size_t append(char *buffer, std::size_t capacity, std::size_t length, const char *text) {
    while (*text != '\0' && length + 1 < capacity)
        buffer[length++] = *text++;
    buffer[length] = '\0';
    return length;
}
std::size_t appendNumber(char *buffer, std::size_t capacity, std::size_t length,
                         unsigned long long value) {
    char digits[24];
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value != 0 && count < sizeof digits);
    while (count > 0 && length + 1 < capacity)
        buffer[length++] = digits[--count];
    buffer[length] = '\0';
    return length;
}

const char *signalDescription(int number) {
    switch (number) {
    case SIGSEGV:
        return "SIGSEGV (invalid memory access)";
    case SIGABRT:
        return "SIGABRT (abort, a failed assertion or an unhandled exception)";
    case SIGILL:
        return "SIGILL (illegal instruction)";
    case SIGFPE:
        return "SIGFPE (arithmetic error)";
#if !defined(_WIN32)
    case SIGBUS:
        return "SIGBUS (invalid memory access)";
#endif
    default:
        return "fatal signal";
    }
}

void writeAll(int descriptor, const char *data, std::size_t length) {
#if !defined(_WIN32)
    while (length > 0) {
        const ssize_t written = ::write(descriptor, data, length);
        if (written <= 0)
            return;
        data += written;
        length -= static_cast<std::size_t>(written);
    }
#else
    (void)descriptor;
    (void)data;
    (void)length;
#endif
}

#if !defined(_WIN32)
void onFatalSignal(int number, siginfo_t *, void *) {
    char path[1100];
    std::size_t length = append(path, sizeof path, 0, crashState.directory);
    length = append(path, sizeof path, length, "/");
    length = append(path, sizeof path, length, crashState.stem);
    length = appendNumber(path, sizeof path, length, static_cast<unsigned long long>(getpid()));
    length = append(path, sizeof path, length, "-");
    length = appendNumber(path, sizeof path, length,
                          static_cast<unsigned long long>(std::time(nullptr)));
    length = append(path, sizeof path, length, ".txt");
    const int descriptor = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (descriptor >= 0) {
        writeAll(descriptor, crashState.header, crashState.headerLength);
        char line[160];
        std::size_t used = append(line, sizeof line, 0, "Signal: ");
        used = appendNumber(line, sizeof line, used, static_cast<unsigned long long>(number));
        used = append(line, sizeof line, used, " ");
        used = append(line, sizeof line, used, signalDescription(number));
        used = append(line, sizeof line, used, "\n\nStack trace (innermost first):\n");
        writeAll(descriptor, line, used);
#if defined(YK_HAS_BACKTRACE)
        void *frames[64];
        const int count = backtrace(frames, 64);
        backtrace_symbols_fd(frames, count, descriptor);
#else
        writeAll(descriptor, "(this C library cannot produce a stack trace)\n", 46);
#endif
        ::close(descriptor);
    }
    char note[1300];
    std::size_t noteLength =
        append(note, sizeof note, 0, "\nThe program crashed. A report was written to ");
    noteLength = append(note, sizeof note, noteLength, path);
    noteLength = append(note, sizeof note, noteLength, "\n");
    writeAll(2, note, noteLength);
    // The default action ends the process (and lets the system's own crash reporter see it).
    std::signal(number, SIG_DFL);
    std::raise(number);
}

void installSignalHandlers() {
    // A stack overflow leaves no stack to run the handler on, so give it one of its own.
    static char alternateStack[65536];
    stack_t stack{};
    stack.ss_sp = alternateStack;
    stack.ss_size = sizeof alternateStack;
    sigaltstack(&stack, nullptr);
#if defined(YK_HAS_BACKTRACE)
    void *warm[4];
    backtrace(warm,
              4); // The first call may load libgcc and allocate; do it now, not while crashing.
#endif
    struct sigaction action {};
    action.sa_sigaction = onFatalSignal;
    sigemptyset(&action.sa_mask);
    // glibc defines SA_RESETHAND as an unsigned constant, the field is an int.
    action.sa_flags =
        static_cast<int>(static_cast<unsigned>(SA_SIGINFO) | static_cast<unsigned>(SA_ONSTACK) |
                         static_cast<unsigned>(SA_RESETHAND));
    for (const int number : {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE})
        sigaction(number, &action, nullptr);
}
#else
void writeWindowsReport(const char *reason, unsigned long code, const void *address) {
    char path[1100];
    std::size_t length = append(path, sizeof path, 0, crashState.directory);
    length = append(path, sizeof path, length, "\\");
    length = append(path, sizeof path, length, crashState.stem);
    length = appendNumber(path, sizeof path, length, GetCurrentProcessId());
    length = append(path, sizeof path, length, "-");
    length = appendNumber(path, sizeof path, length,
                          static_cast<unsigned long long>(std::time(nullptr)));
    length = append(path, sizeof path, length, ".txt");
    HANDLE file =
        CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    const auto put = [&](const char *text, std::size_t size) {
        DWORD written = 0;
        WriteFile(file, text, static_cast<DWORD>(size), &written, nullptr);
    };
    put(crashState.header, crashState.headerLength);
    char line[256];
    std::size_t used = append(line, sizeof line, 0, "Reason: ");
    used = append(line, sizeof line, used, reason);
    used = append(line, sizeof line, used, "\nException code: ");
    used = appendNumber(line, sizeof line, used, code);
    used = append(line, sizeof line, used, "\nAddress: ");
    used = appendNumber(line, sizeof line, used,
                        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(address)));
    used = append(line, sizeof line, used, "\n\nStack trace (raw addresses, innermost first):\n");
    put(line, used);
    void *frames[48];
    const USHORT count = CaptureStackBackTrace(0, 48, frames, nullptr);
    for (USHORT index = 0; index < count; ++index) {
        std::size_t frameLength = appendNumber(
            line, sizeof line, 0,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(frames[index])));
        frameLength = append(line, sizeof line, frameLength, "\n");
        put(line, frameLength);
    }
    CloseHandle(file);
}
LONG WINAPI onUnhandledException(EXCEPTION_POINTERS *info) {
    writeWindowsReport("unhandled exception", info->ExceptionRecord->ExceptionCode,
                       info->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_CONTINUE_SEARCH; // Windows' own error reporting still gets its turn.
}
void onAbort(int) {
    writeWindowsReport("abort or a failed assertion", 0, nullptr);
    std::signal(SIGABRT, SIG_DFL);
    std::raise(SIGABRT);
}
void installSignalHandlers() {
    SetUnhandledExceptionFilter(onUnhandledException);
    std::signal(SIGABRT, onAbort);
}
#endif

// An exception that nothing caught goes to the log with its message before the process aborts (and
// the abort produces the stack trace).
[[noreturn]] void onTerminate() {
    try {
        if (const std::exception_ptr pending = std::current_exception())
            std::rethrow_exception(pending);
        log(LogLevel::Error, "crash", "std::terminate was called");
    } catch (const std::exception &error) {
        log(LogLevel::Error, "crash", std::string("Unhandled exception: ") + error.what());
    } catch (...) {
        log(LogLevel::Error, "crash", "Unhandled exception of an unknown type");
    }
    std::abort();
}

void installCrashHandler(const SessionInfo &info, const fs::path &logFile) {
    const std::string directory = info.logDirectory.string();
    const std::string stem = "crash-" + info.logName + "-";
    std::snprintf(crashState.directory, sizeof crashState.directory, "%s", directory.c_str());
    std::snprintf(crashState.stem, sizeof crashState.stem, "%s", stem.c_str());
    const int written = std::snprintf(
        crashState.header, sizeof crashState.header,
        "YK Engine crash report\nApplication: %s %s\nPlatform: %s\nStarted: %s\nProcess: %ld\n"
        "Log file: %s\n",
        info.application.c_str(), info.version.c_str(), platformName(), localTimeText().c_str(),
        processId(), logFile.string().c_str());
    crashState.headerLength =
        written < 0 ? 0
                    : std::min(static_cast<std::size_t>(written), sizeof crashState.header - 1U);
    crashState.installed = true;
    installSignalHandlers();
    std::set_terminate(onTerminate);
}

// ------------------------------------------------------------------------------ session marker
std::string markerName(const std::string &logName, long pid) {
    return logName + "-" + std::to_string(pid) + ".session";
}

// The pid in "<logName>-<pid>.session", or -1.
long markerPid(const std::string &logName, const std::string &fileName) {
    const std::string prefix = logName + "-";
    const std::string suffix = ".session";
    if (fileName.size() <= prefix.size() + suffix.size() || !fileName.starts_with(prefix) ||
        !fileName.ends_with(suffix))
        return -1;
    const std::string digits =
        fileName.substr(prefix.size(), fileName.size() - prefix.size() - suffix.size());
    if (digits.empty() || digits.size() > 10 ||
        !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return -1;
    return std::stol(digits);
}

// The newest crash report a process left in `directory`, or an empty path.
fs::path crashReportOf(const fs::path &directory, const std::string &logName, long pid) {
    const std::string prefix = "crash-" + logName + "-" + std::to_string(pid) + "-";
    fs::path newest;
    fs::file_time_type newestTime{};
    std::error_code error;
    for (fs::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
        const std::string name = it->path().filename().string();
        if (!name.starts_with(prefix) || !name.ends_with(".txt"))
            continue;
        std::error_code timeError;
        const auto time = fs::last_write_time(it->path(), timeError);
        if (newest.empty() || (!timeError && time > newestTime)) {
            newest = it->path();
            newestTime = time;
        }
    }
    return newest;
}
} // namespace

DiagnosticsSession::DiagnosticsSession(SessionInfo info) : info_(std::move(info)) {
    if (info_.logName.empty())
        info_.logName = "app";
    if (info_.logDirectory.empty())
        return; // Nowhere to write: run without files.
    const fs::path logPath = info_.logDirectory / (info_.logName + ".log");
    std::error_code error;
    fs::create_directories(info_.logDirectory, error);

    // Sessions that never removed their marker and are not running any more ended badly. Another
    // running instance's marker is not a crash.
    for (fs::directory_iterator it(info_.logDirectory, error), end; !error && it != end;
         it.increment(error)) {
        const long pid = markerPid(info_.logName, it->path().filename().string());
        if (pid < 0 || pid == processId() || processAlive(pid))
            continue;
        previousUnclean_ = true;
        if (const fs::path report = crashReportOf(info_.logDirectory, info_.logName, pid);
            !report.empty())
            previousReport_ = report;
        std::error_code removeError;
        fs::remove(it->path(), removeError);
    }

    if (auto opened = openLogFile({logPath}); opened)
        logFile_ = logPath;
    else
        log(LogLevel::Warning, "diagnostics", opened.error());

    marker_ = info_.logDirectory / markerName(info_.logName, processId());
    (void)writeTextFileAtomic(marker_, info_.application + " " + info_.version + " started " +
                                           localTimeText() + "\n");
    if (info_.installHandlers) {
#if !defined(_WIN32)
        std::signal(SIGPIPE, SIG_IGN); // A closed terminal or pipe on stderr is not fatal.
        std::signal(SIGHUP, SIG_IGN);  // Nor is closing the terminal the program was started from.
#endif
        installCrashHandler(info_, logFile_);
    }
    log(LogLevel::Info, "diagnostics",
        info_.application + " " + info_.version + " on " + platformName() + ", process " +
            std::to_string(processId()));
    if (previousUnclean_)
        log(LogLevel::Warning, "diagnostics",
            previousReport_.empty()
                ? std::string("The previous session ended without shutting down (no crash report)")
                : "The previous session crashed; its report is " + previousReport_.string());
}

DiagnosticsSession::~DiagnosticsSession() {
    log(LogLevel::Info, "diagnostics", "Clean shutdown");
    std::error_code error;
    if (!marker_.empty())
        fs::remove(marker_, error);
    closeLogFile();
}

bool crashOnPurpose(std::string_view kind) {
    if (kind == "segv") {
        volatile int *nothing = nullptr;
        *nothing = 1;
        return true;
    }
    if (kind == "abort")
        std::abort();
    if (kind == "throw")
        throw std::runtime_error("crash on purpose (--debug-crash throw)");
    return false;
}
} // namespace yk
