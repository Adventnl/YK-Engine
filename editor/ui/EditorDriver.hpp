#pragma once
#include "ui/EditorApp.hpp"
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace yk::editor {
// clang-format off
// Drives the real editor UI from a script, by sending it the same SDL mouse and keyboard events a
// person would. It finds buttons and menu items through the widget registry ("toolbar/Play",
// "hierarchy/Door"), so scripts survive layout changes, and checks results against the editor's
// state. Used by the automated UI tests and to take screenshots of a workflow. A scripted run uses
// fixed time steps (EditorOptions::fixedStep), so what a script sees does not depend on how fast the
// machine draws frames.
//
// One command per line; '#' starts a comment; words may be "quoted"; ${NAME} expands an environment
// variable. Targets are widget ids, world:X,Y (a point in the scene view) or px:X,Y (window
// coordinates). Targets that are scrolled out of their panel are scrolled into view.
//
//   wait N | settle                     wait N frames (settle: a few)
//   move T | click T [ctrl|shift|alt]   point at / click a target (left button)
//   doubleclick T | rightclick T
//   press T | release                   hold the left button down / let go
//   drag A B [ctrl|shift|alt]           press at A, move in steps to B, release
//   wheel T AMOUNT                      scroll (positive zooms in)
//   key COMBO                           e.g. ctrl+s, f5, delete, shift+f5, escape
//   keydown K | keyup K | hold K N      keys for the game: hold d 60
//   type TEXT                           text into the focused field
//   edit T TEXT                         double-click a drag/text field, replace its value, Enter
//   menu File/Save Scene                click through a menu path (use | when an item has a slash)
//   create Level/Platform               "+" in the hierarchy, then category and template
//   capture FILE.bmp                    save a screenshot
//   timeout N                           frames a target or expectation may take to appear
//   report ENTITY | log TEXT | closewindow | quit
//   expect ...                          see check() in EditorDriver.cpp: selected, prop, position,
//                                       size, links, playing, dirty, dialog, runtime-moved, ...
// clang-format on
class EditorDriver {
  public:
    static Result<std::unique_ptr<EditorDriver>> parse(const std::string &script);
    static Result<std::unique_ptr<EditorDriver>> load(const std::filesystem::path &file);

    // Runs one frame of the script. Returns false once it has finished.
    bool step(EditorApp &app);
    int failures() const {
        return failures_;
    }
    bool finished() const {
        return index_ >= commands_.size();
    }
    // True when only waiting, logging or quitting is left: a script may end because the editor
    // closed itself (closing the window after answering "discard changes").
    bool onlyTrivialCommandsLeft() const;
    // Where screenshots of failed expectations go ("" disables them).
    void setFailureDirectory(std::filesystem::path directory) {
        failureDirectory_ = std::move(directory);
    }

  private:
    struct Command {
        std::vector<std::string> words;
        int line{};
    };
    struct Point {
        float x{}, y{};
    };

    void fail(EditorApp &app, const Command &command, const std::string &detail);
    bool resolve(EditorApp &app, const std::string &target, Point &out, std::string &problem);
    bool execute(EditorApp &app, const Command &command);
    // Returns +1 when the expectation holds, 0 when it does not (yet), -1 when the command is
    // malformed (detail says why).
    int check(EditorApp &app, const Command &command, std::string &detail);
    void rememberRuntime(EditorApp &app);

    std::vector<Command> commands_;
    std::size_t index_{};
    int phase_{};
    int waited_{};
    int timeout_{240};
    int failures_{};
    bool focused_{};
    Point mouse_{};
    Point from_{}, to_{};
    std::filesystem::path failureDirectory_;
    bool wasPlaying_{};
    std::map<std::string, Point> runtimeStart_; // Entity name -> position when Play started.
};
} // namespace yk::editor
