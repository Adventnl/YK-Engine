# Editor resources

Fonts the editor embeds (CMake converts them to a generated source file with `cmake/YkEmbed.cmake`,
so `yk_editor` reads no font files at run time).

| File | What | Source | License |
|---|---|---|---|
| `fonts/Inter-Regular.ttf`, `fonts/Inter-SemiBold.ttf` | UI text | `@fontsource/inter` 5.3.0, Latin subset, weights 400 and 600 | SIL OFL 1.1 (`LICENSES/Inter-OFL-1.1.txt`) |
| `fonts/JetBrainsMono-Regular.ttf` | paths, console, values | `@fontsource/jetbrains-mono` 5.3.0, Latin subset, weight 400 | SIL OFL 1.1 (`LICENSES/JetBrainsMono-OFL-1.1.txt`) |
| `fonts/codicon.ttf`, `fonts/codicon.csv` | icons and their glyph table | `@vscode/codicons` 0.0.46-24 | CC BY 4.0 (`LICENSES/Codicons-CC-BY-4.0.txt`) |

The Fontsource packages ship WOFF2; the files here are the same glyphs converted to TrueType with
`fontTools` (Dear ImGui's font loader reads TrueType, not WOFF2). Nothing else was changed.

`gen_codicons.py` writes `../ui/Codicons.hpp`, the UTF-8 strings of the icons the editor uses; add a
name to its list, run it and commit the header when a new icon is needed.
