# Fireboy and Watergirl: Two Levels

A single-screen, two-player puzzle platformer for YK Engine. Open this folder as a
project in `yk_editor`, or run `yk_player Fireboy-Watergirl-Demo` from the repository
root. The game starts in `scenes/reference_level.ykscene` and moves to
`scenes/level02.ykscene` after both players reach their doors.

| Player | Move | Jump | Use lever |
| --- | --- | --- | --- |
| Fireboy (orange) | A / D | W | S or E |
| Watergirl (blue) | Left / Right | Up | Down or Right Ctrl |

R restarts, P pauses. Red liquid hurts Watergirl, blue liquid hurts Fireboy, and
green liquid hurts both. Fireboy can hold the left red button to run the striped
platform while Watergirl rides across. The central lever and two upper buttons
run the platform over the green channel. Both players must stand in their own
doors to finish.

Level Two, **Classroom**, recreates the supplied classroom image with its desk
platforms, bookcase column, colored liquid basins, glowing gems, and two exit
doors. Fireboy can press the left red button to carry Watergirl across the orange
basin. The middle lever opens the striped bookcase panel; the yellow button calls
the orange elevator to the upper desk. The upper yellow button moves the cyan
platform across the blue basin. Purple liquid hurts both players. Completing
Level Two leaves the completion screen visible.

The room backdrop comes from `Image_20261002095859_162_30.png`; the second root
image was the composition reference. All 30 cutouts in `game_assets_level1` are
placed in the scene. The sixteen `character_sprites` poses are assembled into
animated sheets. Sprite pixels use nearest-neighbour filtering; the colliders
are separate shapes matched to playable surfaces, since the cutouts include
decorative borders and transparent margins.

The project contains its own copied textures, sounds, prefabs, and scenes so it
can be opened or exported without referring to the source folders. The classroom
cutouts came from `classroom_level_assets.zip`; their manifest and imported PNGs
are stored in `assets/classroom`. To rebuild the scenes:

```sh
python Fireboy-Watergirl-Demo/tools/build_level.py
python Fireboy-Watergirl-Demo/tools/build_level2.py
```

The Level One authoring script requires Pillow. Both generated scenes and their
assets are included; Python is not required to play the game.
