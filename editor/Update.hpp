#pragma once

namespace yk::editor {
// A packaged editor checks the newest completed GitHub release before opening a project.
// Returns true only when the operating system has accepted a newer installer.
bool startUpdateIfAvailable(int argc, char **argv);
} // namespace yk::editor
