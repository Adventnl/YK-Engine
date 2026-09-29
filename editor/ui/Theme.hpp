#pragma once
#include "imgui.h"
#include "yk/core/Color.hpp"

// The editor's look: VS Code's "Dark Modern" colors, compact metrics, Inter for text, JetBrains
// Mono for code-like text and Codicons for icons. All three are embedded in the executable (see
// LICENSES/ and THIRD_PARTY.md).
namespace yk::editor::ui {
namespace vs {
// Surfaces.
inline constexpr Color editorBg{31, 31, 31, 255}; // #1F1F1F the editor area, menus, dialogs
inline constexpr Color chromeBg{24, 24, 24, 255}; // #181818 title, activity, side bars, panel, tabs
inline constexpr Color border{43, 43, 43, 255};   // #2B2B2B
inline constexpr Color menuBorder{69, 69, 69, 255}; // #454545
// Text.
inline constexpr Color text{204, 204, 204, 255};      // #CCCCCC
inline constexpr Color textDim{157, 157, 157, 255};   // #9D9D9D
inline constexpr Color textFaint{110, 110, 110, 255}; // disabled
inline constexpr Color textBright{255, 255, 255, 255};
inline constexpr Color link{77, 170, 252, 255}; // #4DAAFC
// Inputs and buttons.
inline constexpr Color inputBg{49, 49, 49, 255};     // #313131
inline constexpr Color inputBorder{60, 60, 60, 255}; // #3C3C3C
inline constexpr Color focus{0, 120, 212, 255};      // #0078D4
inline constexpr Color buttonBg{0, 120, 212, 255};
inline constexpr Color buttonHover{2, 110, 193, 255}; // #026EC1
inline constexpr Color buttonActive{0, 95, 170, 255};
inline constexpr Color secondaryBg{49, 49, 49, 255};
inline constexpr Color secondaryHover{60, 60, 60, 255};
// Lists.
inline constexpr Color listHover{42, 45, 46, 255};             // #2A2D2E
inline constexpr Color listSelection{4, 57, 94, 255};          // #04395E focused
inline constexpr Color listSelectionInactive{55, 55, 61, 255}; // #37373D
// Chrome details.
inline constexpr Color activityInactive{134, 134, 134, 255}; // #868686
inline constexpr Color activityActive{215, 215, 215, 255};   // #D7D7D7
inline constexpr Color tabActiveTop{0, 120, 212, 255};
inline constexpr Color statusPlaying{202, 81, 0, 255}; // #CA5100 (debugging)
inline constexpr Color statusPaused{204, 167, 0, 255};
inline constexpr Color scrollbar{121, 121, 121, 102};
inline constexpr Color scrollbarHover{100, 100, 100, 179};
inline constexpr Color scrollbarActive{191, 191, 191, 102};
// Feedback.
inline constexpr Color error{241, 76, 76, 255};     // #F14C4C
inline constexpr Color warning{204, 167, 0, 255};   // #CCA700
inline constexpr Color info{55, 148, 255, 255};     // #3794FF
inline constexpr Color success{115, 201, 145, 255}; // #73C991
} // namespace vs

// Metrics, in points at 100% display scale (ImGui scales them for HiDPI displays).
namespace metrics {
inline constexpr float titleBar = 30.0F;
inline constexpr float activityBar = 46.0F;
inline constexpr float statusBar = 22.0F;
inline constexpr float tabBar = 32.0F;
inline constexpr float panelTabBar = 30.0F;
inline constexpr float sideBarHeader = 30.0F;
inline constexpr float row = 22.0F; // A list row
inline constexpr float sash = 5.0F; // Grab area of a splitter
} // namespace metrics

struct Fonts {
    ImFont *ui{};       // Inter Regular with Codicons merged in.
    ImFont *semibold{}; // Inter SemiBold: headers, tabs.
    ImFont *mono{};     // JetBrains Mono: paths, console, code-like values.
};
Fonts &fonts();
// Adds the embedded fonts to `io`; call once, before the first frame.
void loadFonts(ImGuiIO &io);
// Applies the color scheme and the compact metrics to the current ImGui style.
void applyTheme();
// Fills `text` with the bold small-caps style used by side bar and panel headers ("EXPLORER").
std::string headerText(const std::string &text);
} // namespace yk::editor::ui
