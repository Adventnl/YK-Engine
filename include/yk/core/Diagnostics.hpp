#pragma once
#include <filesystem>
#include <string>
#include <string_view>

// What a program that nobody watches in a terminal needs to leave behind when something goes wrong:
// a log file, a crash report with a stack trace, and a way for the next start to notice that the
// last session did not end normally. The stock programs (yk_editor, yk_player) start one session
// each; games with their own hosts do the same (yk/host/Hosts.hpp).
namespace yk {
struct SessionInfo {
    std::string application; // Named in reports and the log: "YK Editor".
    std::string version;
    std::filesystem::path logDirectory; // Log files, crash reports and the session marker.
    std::string logName{"app"}; // File stem: <logName>.log, crash-<logName>-<pid>-<time>.txt.
    // Install the crash handler and ignore SIGPIPE and SIGHUP. Closing the terminal a program was
    // started from (or a closed pipe on stderr) must not kill a window that is still in use.
    bool installHandlers{true};
};

class DiagnosticsSession {
  public:
    // Opens <logDirectory>/<logName>.log (earlier logs are kept), installs the handlers and marks
    // the session as running. Problems with the folder are logged to stderr and never stop the
    // program: diagnostics must not be the reason something fails to start.
    explicit DiagnosticsSession(SessionInfo info);
    // A normal end: removes the running marker and closes the log. A crash, a kill or a power cut
    // never gets here, which is how the next start knows.
    ~DiagnosticsSession();
    DiagnosticsSession(const DiagnosticsSession &) = delete;
    DiagnosticsSession &operator=(const DiagnosticsSession &) = delete;

    // An earlier session of this program (same log name) left its marker behind and is no longer
    // running. previousCrashReport() is that session's report, empty when it wrote none (killed).
    bool previousEndedUncleanly() const {
        return previousUnclean_;
    }
    const std::filesystem::path &previousCrashReport() const {
        return previousReport_;
    }
    const std::filesystem::path &logFile() const {
        return logFile_;
    }
    const std::filesystem::path &logDirectory() const {
        return info_.logDirectory;
    }

  private:
    SessionInfo info_;
    std::filesystem::path logFile_, marker_, previousReport_;
    bool previousUnclean_{};
};

// Crashes on purpose so the reporting can be tested (`--debug-crash <kind>` on the programs):
// "segv" (invalid memory access), "abort", "throw" (an exception nobody catches). Returns false for
// an unknown kind; otherwise does not return.
bool crashOnPurpose(std::string_view kind);
} // namespace yk
