# Fireboy and Watergirl

A two-player puzzle platformer with two levels built from the original coloured
block, striped platform, switch, gem and door kit. Both levels use the furnished
room background. Terrain has separate, aligned collision surfaces; buttons have
a fixed housing and a solid moving cap that carries the player down.

Open this folder in `yk_editor`, or run `yk_player Fireboy-Watergirl-Demo`.
Exported games open the same title screen. Press **Enter** to play, then **1**
(or Enter) for **Level 1**, or **2** for **Level 2**. Controller equivalents are
East to play, South for Level 1 and North for Level 2.

| Player | Move | Jump | Use lever |
| --- | --- | --- | --- |
| Fireboy | A / D | W | S or E |
| Watergirl | Left / Right | Up | Down or Right Ctrl |

**R** retries, **P** pauses and **M** returns to the menu. Players collide with
each other. Red liquid hurts Watergirl, blue hurts Fireboy, and green hurts both.
Collect the matching gems along each route. Both players must reach their own
doors to finish; gems are optional. Press Enter to skip the completion delay.

**Level 1** introduces the two coloured pools. Step on each floor button to
start its shuttle. Climb the ledges on the right, use the upper lever to start
the green crossing, then climb left to the exit deck. The small left ledge holds
an optional Watergirl gem. Completion advances to Level 2.

**Level 2** has a long green channel. The starting button starts a shuttle wide
enough for both players. Ride to the far bank, use the lever, then board the lift
when it returns to its lower stop. The lift has a clear shaft beside the upper
landing. Cross the upper pools from right to left, starting the shuttles with
the buttons, to reach the exits. Completion returns to the title screen.

The floor buttons latch after a full press so either player can catch up. No
mechanism requires a player to stay behind permanently. Shuttle endpoints pause
to give riders time to board. Hold jump for full height on the taller ledges.

To regenerate the scenes and extracted modular pieces from the imported art:

```sh
python Fireboy-Watergirl-Demo/tools/build_game.py
```

`build_level.py` also reimports the original art and character poses, then rebuilds
the whole game. `build_level2.py` rebuilds only Level 2. These scripts require
Pillow; playing and exporting require no Python or source asset folders.

The `fireboy_demo` test plays both routes through real physics, checks the
title/selection flow, presses the two-part buttons and checks every platform's
full travel against the static terrain.
