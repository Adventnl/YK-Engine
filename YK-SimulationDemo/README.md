# Night Shift

A compact, original top-down prison escape demo for YK Engine. Open `project.ykproj` in the editor or run:

```sh
./build/dev/yk_player YK-SimulationDemo
```

The full route is designed to take about a minute on a first playthrough:

1. In the cell, take the yellow keycard beside the locker and the orange wire by the desk.
2. Use the keycard to open the first gate.
3. Repair the blue control panel with the wire.
4. Open the outer gate and step into the green exit. `ESCAPED!` is the win state.

Use WASD or arrow keys to move, E or Space to interact, and R to restart. The on-screen objective panel records keycard, wire and power progress. The interaction prompt appears when an object is in range. Both locked gates have solid collision until their prerequisites are complete; the exit stays behind them.

The level uses data-only YK components and colored placeholder shapes. `tools/build_level.py` regenerates `scenes/night_shift.ykscene`; no prison-specific engine code is needed. The headless `simulation_demo` test checks the locked route, each interaction, the exit, and restart.
