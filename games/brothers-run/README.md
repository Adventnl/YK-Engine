# Brothers Run

A local two-player racing platformer starring the brothers Ember and Tide. Level 1 uses the original coloured
block kit in the furnished room; Level 2 is a classroom with original generated
wooden desk cabinets, bookcases, textbook platforms and a chalkboard backdrop.
Both retain the same switch, gem, hazard and door language. Every block and moving deck is solid on all sides. The floor
reaches the screen edges and continues below the view. Buttons have
a fixed housing and a solid moving cap that carries the player down.

Open this folder in `yk_editor`, or run `yk_player games/brothers-run`.
Exported games open the same title screen. **Click Start the Race**, then click
either level card; hover and press feedback show which control is active.
Keyboard shortcuts also work: press **Enter** to play, then **1**
(or Enter) for **Level 1**, or **2** for **Level 2**. Controller equivalents are
East to play, South for Level 1 and North for Level 2.

| Player | Move | Jump | Use lever |
| --- | --- | --- | --- |
| Ember | A / D | W | S or E |
| Tide | Left / Right | Up | Down or Right Ctrl |

**R** retries, **P** pauses and **M** returns to the menu. Players collide with
each other. Red liquid hurts Tide, blue hurts Ember, and green hurts both.
Collect the matching gems along each route. Each level has one shared exit:
the first brother to reach it wins the race and advances the level for both. Gems are optional.
Press Enter to skip the completion delay.

**Level 1** introduces the two coloured pools. Step on each floor button to
start its shuttle. Climb the three solid stair steps on the right, use the upper
lever to start the green crossing, then take the left stair to the exit deck.
Levers visibly throw from left to right when switched. The small left ledge holds
an optional Tide gem. Completion advances to Level 2.

**Level 2** follows three lanes through the classroom. The starting
button starts a shuttle wide enough for both players to cross the green channel
together. On the far bank, use the lever and board the lift at its lower stop.
The first elevator boards at floor height and rises to the middle landing.
Press its button to retract the security gate and start the blue crossing back
left. On the left bank, press the return-lift button and ride the second elevator
up to the final lane. Press the final button, then cross the shorter
red pool to the exit deck on the right. Pressed button housings turn green and
stay on. Completion returns to the title screen.

The floor buttons latch after a full press so either player can catch up. No
mechanism requires a player to stay behind permanently. Shuttle endpoints pause
to give riders time to board. Hold jump for full height on the taller ledges.

To rebuild the current scenes and modular pieces:

```sh
python games/brothers-run/tools/build_game.py
```

`build_level2.py` rebuilds only Level 2. These scripts require Pillow; playing and
exporting require no Python or source asset folders. The original bulk import script,
unused art, old prefabs and development previews have been removed. Current character
sheets, the room backdrop and the classroom artwork are retained.

Classroom terrain art uses the built-in ImageGen tool. The terrain originals and
their prompts are in `source/classroom-generated/`; the Level 2 backdrop is the
supplied classroom image stored alongside them. `tools/classroom_art.py` exports
the playable textures into `assets/classroom/generated/`. Cabinets are
384x80, bookcases 96x80, book platforms 192x28, and the background 1672x941,
matching Level 1's canvas. Exports crop transparent padding and resize with
nearest-neighbour sampling; all texture metadata uses nearest filtering at
50 pixels per unit. Rebuilding either the full game or Level 2 re-exports these
assets without calling an image service. The gameplay collision and route
dimensions are unchanged.

The `brothers_run` test plays both routes with each character through real
physics, verifies either can finish alone at the single shared exit, checks mouse and
keyboard title/selection flow, verifies head collisions and solid terrain,
checks lever poses and the retracting gate, verifies buttons start platforms, and
checks every platform's full travel with room for a standing rider. It also
boards both characters on Level 2's starting shuttle and rides them together.
