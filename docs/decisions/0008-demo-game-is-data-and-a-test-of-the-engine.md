# ADR 0008: Games are independent engine projects

Games are project data rather than built-in engine modes. The repository keeps the finished
Brothers Run game and Invisible Castle in `games/`, including assets, authoring tools
and game-specific checks. The earlier Cinder Vale and Blackwater prototypes were removed.

Engine libraries contain reusable systems. Project files supply content, labels and story;
the editor discovers project manifests rather than naming particular games. Engine tests create
small fixtures, and `YK_BUILD_GAMES=OFF` excludes game data and game-specific checks from builds
and packaging. A game can also live entirely outside the engine checkout.
