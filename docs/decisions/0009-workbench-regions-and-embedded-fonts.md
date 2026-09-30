# ADR 0009: A fixed workbench instead of docking; embedded fonts and icons

Date: 2026-09-29. Status: accepted. Amends ADR 0007.

## Context

The first editor used ImGui's docking: free-floating windows with tab titles and a dark blue
palette. It did not read as a development tool and its layout was neither predictable nor
testable.

## Decision

- The window is a **workbench** in the sense VS Code uses: title bar, activity bar, side bar,
  editor groups with tabs, inspector, panel, status bar. `WorkbenchLayout` (editor core, no
  window) holds the state (visible views, sizes, split) and computes the rectangle of every
  region for a window size; the regions tile the window exactly and stored sizes are clamped. The
  UI draws each region as one borderless ImGui window pinned to its rectangle, and sashes are
  exact-size windows above the regions they separate. Nothing floats and nothing docks.
- The state is saved as `workbench.json` and tolerates missing or damaged files.
- The look is a neutral dark palette (VS Code's "Dark Modern" values), compact 22 point rows and
  no gradients. Text is Inter, code-like text JetBrains Mono, icons Codicons; all three are
  embedded in the executable (CMake turns the font files into a source file), so the editor reads
  no files at run time. The licenses (SIL OFL 1.1, CC BY 4.0) ship with every installation.
- Several scenes are open at once by swapping the active document with a map of background ones.

## Consequences

The layout arithmetic is unit tested and the UI scripts assert which views show. Two ImGui
behaviors bit during the work and are worth remembering: windows created with
`NoBringToFrontOnFocus` are placed *behind* the others (a sash must not carry that flag), and a
window's minimum size (32 points) would swallow clicks around a 5 point sash unless it is lowered.
Font licensing and the size of the executable (about 350 KB of fonts) are the cost.
