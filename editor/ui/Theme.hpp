#pragma once
#include "imgui.h"
#include "yk/core/Color.hpp"

// The editor's look, taken from a current VS Code: a cool neutral dark, the parts drawn as thin
// outlined cards on a darker canvas, compact type (Inter 13), JetBrains Mono for code-like text and
// Codicons for icons. All three fonts are embedded in the executable (LICENSES/, THIRD_PARTY.md).
// The values were measured from the reference screenshot (docs/EDITOR.md, "Look").
namespace yk::editor::ui {
namespace vs {
// Surfaces.
inline constexpr Color editorBg{18, 21, 20,
                                255}; // #121514 viewport surround, toolbar rows, welcome
inline constexpr Color chromeBg{24, 26, 28, 255};   // #181A1C the canvas, side bars and panels
inline constexpr Color raisedBg{31, 33, 35, 255};   // #1F2123 tab strip, menus, popups, dialogs
inline constexpr Color border{43, 45, 46, 255};     // #2B2D2E outline of a card
inline constexpr Color menuBorder{62, 64, 66, 255}; // #3E4042 outline of a popup
// Text.
inline constexpr Color text{204, 204, 204, 255};    // #CCCCCC
inline constexpr Color textDim{142, 144, 146, 255}; // #8E9092
inline constexpr Color textFaint{96, 98, 100, 255}; // disabled
inline constexpr Color textBright{255, 255, 255, 255};
inline constexpr Color link{77, 170, 252, 255}; // #4DAAFC
// Inputs and buttons.
inline constexpr Color inputBg{36, 38, 40, 255};     // #242628
inline constexpr Color inputBorder{54, 56, 58, 255}; // #36383A
inline constexpr Color focus{0, 120, 212, 255};      // #0078D4
inline constexpr Color buttonBg{0, 120, 212, 255};
inline constexpr Color buttonHover{2, 110, 193, 255}; // #026EC1
inline constexpr Color buttonActive{0, 95, 170, 255};
inline constexpr Color secondaryBg{44, 46, 48, 255};
inline constexpr Color secondaryHover{56, 58, 60, 255};
// Lists: selection is a quiet gray, like the reference, not a blue bar.
inline constexpr Color listHover{34, 36, 38, 255};             // #222426
inline constexpr Color listSelection{44, 46, 47, 255};         // #2C2E2F
inline constexpr Color listSelectionInactive{38, 40, 41, 255}; // #262829
// Chrome details.
inline constexpr Color pill{46, 48, 50, 255};                // #2E3032 selected tab, active view
inline constexpr Color pillHover{38, 40, 42, 255};           // #26282A
inline constexpr Color activityInactive{134, 136, 138, 255}; // #86888A
inline constexpr Color activityActive{215, 215, 215, 255};   // #D7D7D7
inline constexpr Color tabActiveTop{0, 120, 212, 255};
inline constexpr Color statusPlaying{202, 81, 0, 255}; // #CA5100 (debugging)
inline constexpr Color statusPaused{204, 167, 0, 255};
inline constexpr Color scrollbar{121, 121, 121, 90};
inline constexpr Color scrollbarHover{110, 110, 110, 150};
inline constexpr Color scrollbarActive{191, 191, 191, 110};
// Feedback.
inline constexpr Color error{241, 76, 76, 255};     // #F14C4C
inline constexpr Color warning{204, 167, 0, 255};   // #CCA700
inline constexpr Color info{55, 148, 255, 255};     // #3794FF
inline constexpr Color success{115, 201, 145, 255}; // #73C991
} // namespace vs

// Metrics, in points at 100% display scale (ImGui scales them for HiDPI displays).
namespace metrics {
inline constexpr float titleBar = 36.0F;
inline constexpr float activityBar = 46.0F;
inline constexpr float statusBar = 26.0F;
inline constexpr float tabBar = 34.0F;
inline constexpr float panelTabBar = 34.0F;
inline constexpr float sideBarHeader = 36.0F;
inline constexpr float row = 22.0F; // A list row
inline constexpr float sash = 5.0F; // Grab area of a splitter
inline constexpr float gap = 4.0F;  // Between cards (see WorkbenchMetrics::gap)
inline constexpr float cardRadius = 8.0F;
inline constexpr float controlRadius = 5.0F; // Selected tab, active icon, pills, small buttons
inline constexpr float fontSize = 13.0F;
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
