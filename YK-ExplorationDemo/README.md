# Castle Paths

A data-only YK Engine exploration game. Open this folder in the editor or run `yk_player YK-ExplorationDemo`. Use WASD or arrows to move and E or Space to interact. Talk to Mara, activate the brass torch, open the gate, and enter the hall.

The courtyard and hall use the supplied Invisible Castle pixel art: five explorers, tiled stone floors and walls, plants, furniture, statues, banners, torches, a gold threshold, and a green portal. The player and Mara are separate sprites; the other explorers can be spoken to. Collision is authored around their feet and around furniture rather than inferred from the image transparency.

The two maps retain a follow camera, top-down collision, Y-sorted sprites, proximity interactions, dialogue portraits, a persistent gate, and named arrival spawns. The explorer art has one pose per character, so movement currently uses a facing-independent sprite.
