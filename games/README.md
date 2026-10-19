# Games

Each child folder is a complete engine project. The generic editor and player load it through
`project.ykproj`; neither game is compiled into engine libraries.

- [Fireboy and Watergirl](fireboy-watergirl/README.md): finished co-op game, including its title
  screen and level selector.
- [Blackthorn Keep](blackthorn-keep/README.md): castle escape game, opening in Level 2: The Inner Keep.

From the repository root:

```sh
./build/dev/yk_player games/fireboy-watergirl
./build/dev/yk_player games/blackthorn-keep
```

Open the same folders in `yk_editor` and press F5. A new game can be added as another project
folder; the welcome page reads its name from its manifest. Authoring tools and original art stay
inside the corresponding game. They are excluded from installed runtime projects and exports.
`YK_BUILD_GAMES=OFF` excludes these projects and their checks from the engine build and packaging.
