# Games

Each child folder is a complete engine project. The generic editor and player load it through
`project.ykproj`; neither game is compiled into engine libraries.

- [Brothers Run](brothers-run/README.md): two brothers racing through a workshop, including its title
  screen and level selector.
- [Invisible Castle](invisible-castle/README.md): castle escape game with a title screen, two-chapter selector,
  controls screen and in-game pause menu.

From the repository root:

```sh
./build/dev/yk_player games/brothers-run
./build/dev/yk_player games/invisible-castle
```

Open the same folders in `yk_editor` and press F5. A new game can be added as another project
folder; the welcome page reads its name from its manifest. Authoring tools and required source art stay
inside the corresponding game. They are excluded from installed runtime projects and exports.
Engine packages never contain either demo. `YK_BUILD_GAMES=ON` enables their independent checks.
