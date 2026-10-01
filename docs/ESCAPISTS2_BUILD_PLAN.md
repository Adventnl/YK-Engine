# Build plan: from a platformer engine to a simulation-adventure engine

**Goal.** YK Engine keeps everything it can do today (Fireboy & Watergirl-style puzzle platformers,
see *Cinder Vale*) and gains the reusable tools and runtime systems from which an entire
*Escapists 2*-class game (top-down / 2.5D, multi-floor, NPC routines, inventory and crafting
stealth, combat, quests, save games, one to four players) could be authored as a **separate YK
project**. The test of success is not "does this repository run something similar to a prison
game" but "does the engine contain generic tools so that such a game is *content*, not engine
code". The reference project (`YK-SimulationDemo/`, an original facility with original
placeholder art) exists to prove exactly that.

This file is the roadmap. Current truth is in [STATUS.md](STATUS.md) (what is real and how it was
verified), the next steps are in [NEXT.md](NEXT.md), known problems in
[KNOWN_ISSUES.md](KNOWN_ISSUES.md), and the reasons behind each big choice in
[decisions/](decisions/). A session with no memory should read those four, then this file.

## Ground rules (from the brief, restated as constraints)

1. **Engine and game stay separate.** Nothing in `include/yk`, `src/`, `editor/` names a game
   (no `Prisoner`, `Guard`, `Fireboy`). Generic vocabulary only: `NavigationAgent`, `Inventory`,
   `Schedule`, `Faction`, `AccessPolicy`, `Zone`, `Perception`, `AIController`, `Combatant`,
   `Objective`, `WorldLayer`, `SecurityState`, `DialogueGraph`, `SaveGame`, `Script`.
2. **Extend, never fork.** The component registry, reflection, scenes, prefabs, runtime, editor
   and tests keep their shape. Serialized formats gain *optional* fields (old files load
   unchanged) or get a version bump with a migration.
3. **Data first.** Items, recipes, loot, quests, schedules, brains, factions, violations,
   security levels, dialogue, tilesets are assets validated at project load and by `yk validate`.
   Behaviour that varies per game is data, rules or Lua, not another C++ component.
4. **A feature is not done until** it has runtime, serialization, validation, an authoring path
   (Inspector from reflection or an editor tool), debug visualization where it makes sense, tests,
   and documentation. STATUS.md says which of those exist.
5. **No fake claims.** STATUS.md marks each capability NOT STARTED / FOUNDATION / PARTIAL /
   FUNCTIONAL / VERIFIED; VERIFIED needs an automated test that exercises it.

## Architecture overview

```
yk_engine   (include/yk/*, src/*)          no game, no editor, SDL/Box2D private
  core        Json, Result, Log, Math ...   + Value (typed variant), JsonReader, Rng
  scene       Entity/Component/Registry     + extension slots, Json property, prefab overrides
  runtime     GameRuntime, GameSession      + Services, update phases, interpolation
  world       WorldLevels, WorldGrid, Tileset/Tilemap data, SpatialHash, line of sight
  navigation  NavigationWorld (grid + links + A*), path requests with budgets
  rules       Value/Facts, Condition, Action, RuleCatalog, Rule (WHEN/IF/THEN)
  data        GameData: item/recipe/loot/quest/schedule/brain/ruleset/dialogue definitions
  stats       StatDefinition/StatSet/Modifier, status effects
  items       ItemDatabase, Inventory, Equipment, Container, Loot, Crafting
  sim         WorldClock, Schedule, Faction, Zone/Room, Crime/Witness/Alert, Security, Quest/Job
  ai          Brain (HSM + utility + tasks), Perception math, noise
  save        SaveGame (yk.save), migration
  scripting   Lua VM (sandboxed), script API           (vendored Lua 5.4, MIT)
  network     Transport, replication                    (vendored ENet, MIT) - last
yk_gameplay (include/yk/gameplay, src/gameplay)   components built on the above
  existing platformer + exploration components (unchanged behaviour)
  + CharacterMotor, NavigationAgent, AIController, Perception, Inventory, Container, ...
editor/core + editor/ui                    tools: tilemap, navigation overlay, AI debug, definition editors
player/ tools/yk/                          yk_player, `yk` (validate, nav-check, script-check, pack, ...)
YK-DemoGame/  YK-ExplorationDemo/  YK-SimulationDemo/   projects (data), consume the engine
```

Dependency direction never reverses: `world`, `navigation`, `rules`, `stats`, `items`, `sim`,
`ai`, `save` are plain C++ libraries over `core` (headless, unit-testable without a scene);
components in `yk_gameplay` bind them to scenes and to `GameContext`.

### Cross-cutting decisions (each has an ADR when it lands)

| Topic | Decision | ADR |
|---|---|---|
| One world, many floors | A scene is one continuous simulation. `WorldLevelDef`s live in the scene settings; entities occupy a level through `WorldLayer`; physics filters by level; separate scenes are for separate maps. | 0016 |
| Services and update order | `GameContext::services()` holds explicitly-owned, typed, lazily-created services ticked in documented phases; components also have a phase. | 0017 |
| Conditions, actions, rules | One predicate/action catalogue (`RuleCatalog`, stored as an extension of the `ComponentRegistry`), one `Value` type, one fact resolver. Doors, quests, dialogue, interactions, events, AI and UI bindings all use it. | 0018 |
| Render interpolation | Runtime keeps previous/current world transforms per tick; renderer and camera blend by the accumulator alpha; teleports snap. | 0019 |
| Navigation | Multi-level uniform grid with area costs, links (stairs, ladders, vents, doors, drops), resumable budgeted A*, dynamic blockers by reference count, path cache and local avoidance steering. Not a navmesh: maps are tile based. | 0020 |
| Definitions | Versioned JSON assets (`.ykitem`, `.ykrecipe`, `.ykloot`, `.ykquest`, `.ykschedule`, `.ykbrain`, `.ykrules`, `.ykdialogue`, `.yktileset`) loaded into `GameData`; one definition per file or a list per file. Stable string ids. | 0021 |
| Tilemap | `Tilemap` component, chunked sparse int32 tiles per layer, tileset metadata drives collision/navigation/sight; stored as a JSON property so undo snapshots include it. | 0022 |
| Prefab overrides | Instances record the prefab entity each of their entities came from and a list of explicit overrides; revert/apply/update operate on that list. | 0023 |
| AI | Data-driven brain: hierarchical states, utility scoring, task primitives, the shared condition engine; Lua can add tasks. | 0024 |
| Scripting | Lua 5.4 vendored, sandboxed (no io/os/debug/package, instruction budget), versioned API table, per-component environments. | 0025 |
| Save games | `yk.save` versioned JSON assembled from a `Saveable` contract on components and services; persistent actor ids; scene file + deltas. | 0026 |
| Multiplayer | `PlayerId` 1-4 owns a controlled entity, input set and view; host-authoritative replication; ENet transport behind an interface; clients send intent only. | 0027 |

## Phases, dependency order and completion criteria

Phases follow the brief's order. A phase is *complete* when its row in STATUS.md is FUNCTIONAL or
better, with tests named. "Where" lists the main directories.

| Phase | Content | Where | Done when |
|---|---|---|---|
| **A** | Audit, baseline, plan, ADRs, status files | `docs/` | baseline recorded; plan/status/next/issues exist |
| **B** | Core upgrades: render interpolation; typed `Blackboard`; `Json` property type; richer `EventBus`; services + update phases; spatial hash; UI reference scaling | `src/runtime`, `src/core`, `src/scene`, `src/graphics` | `interpolation`, `core`, `runtime` tests; demo still passes |
| **B2** | Prefab overrides; tilemap (runtime, renderer, tools); editor crash recovery | `src/scene`, `src/world`, `editor/` | tests + editor scripts |
| **C** | World levels; world grid; navigation (A*, links, dynamic blockers, agent, avoidance, door queues); editor overlays; stress tests | `src/world`, `src/navigation`, `src/gameplay/Navigation.cpp` | `navigation` suite incl. 100-agent stress |
| **D** | CharacterMotor; stats; status effects; health/stamina; equipment | `src/stats`, `src/gameplay/Character.cpp` | `stats`, `character` suites |
| **E** | Item database; inventory; containers; pickups; loot; crafting; appearance | `src/items`, `src/gameplay/Items.cpp` | `items` suite incl. save round-trip |
| **J** | Conditions, actions, rules, EventAction compatibility, quests, objectives, dialogue graph, cutscene sequences | `src/rules`, `src/sim/Quest*`, `src/gameplay/Rules.cpp` | `rules`, `quests`, `dialogue` suites |
| **K** | Lua scripting, `ScriptComponent`, API, hot reload, sandbox | `src/scripting`, `third_party/lua` | `scripting` suite |
| **F** | AI brains, perception, awareness, noise, AI debugger, navigation integration | `src/ai`, `src/gameplay/Ai.cpp` | `ai` suite (routine, vision, chase, return) |
| **G** | World clock, schedules, schedule agent, zones, rooms, factions, relationships, jobs, economy, vendors | `src/sim` | `sim` suite |
| **H** | Violations, witnesses, alert meters, access policy, disguises, scanners, security levels, lockdown | `src/sim`, `src/gameplay/Security.cpp` | `crime_security` suite |
| **I** | Combat: actions, hitboxes, knockback, blocking, charge, stamina, target lock, KO, carry, AI combat | `src/gameplay/Combat.cpp` | `combat` suite |
| **L** | Tool actions, durability, breakable/diggable/cuttable, vents, underground, hiding, search | `src/gameplay/World*.cpp` | `world_modification` suite; nav updates after break |
| **M** | Save game: persistent ids, Saveable, slots, migration, validation | `src/save`, `src/gameplay/Save.cpp` | `save` suite (round trip of a substantial world) |
| **N** | Runtime UI framework and screens; minimap; settings; rebinding | `src/graphics/Ui*`, `src/gameplay/Ui*.cpp` | UI tests incl. flows (pickup, container, equip, craft, drop, save, reload) |
| **O** | Reference simulation project (data + Lua only) | `YK-SimulationDemo/` | headless playthrough of both escape routes |
| **P** | Local multiplayer: `PlayerManager`, 1-4 players, cameras, per-player UI | `src/gameplay/Players.cpp` | 2-player cooperative objective test |
| **Q** | Networking: transport, identity, replication, host/client | `src/network`, `third_party/enet` | automated host/client test |
| **R** | Authoring: specialized editors, asset browser, validation, CLI, content packages | `editor/`, `tools/yk` | `yk validate` catches the broken-reference matrix |
| **S** | Distribution: `.ykpak`, Windows installer/metadata, macOS hooks, CI | `src/assets`, `packaging/`, `.github` | export + install tests |

### Update order (documented in [ARCHITECTURE.md](ARCHITECTURE.md#update-order))

Each fixed tick runs these phases in order: input actions -> world clock and time services ->
script pre-update -> AI decisions -> movement intent (controllers, navigation steering) -> physics
step -> transform write-back -> triggers and contacts -> perception and noise -> gameplay events
-> post simulation (stats regeneration, status expiry, schedules) -> destroy flush and event
dispatch -> interpolation snapshot. Render preparation happens per frame with the interpolation
alpha.

## Reference targets (measured, see STATUS.md)

500-2000 scene entities, 50-100 active NPCs on three floors, a large tilemap, schedules, inventories
and navigation holding a 60 Hz fixed step: path requests are budgeted per tick (never one frame's
worth of A* in a tick), perception is staggered, interactions and perception use the spatial hash
rather than scanning all entities.

## Reference project (original content, no borrowed art or names)

`YK-SimulationDemo/`: a compact facility with Floor 0, Floor 1, Roof, a vent layer and an
underground area; 8-15 actors; routines (wake, roll call, breakfast, work, free time, dinner,
curfew) that travel by navigation; items (normal, contraband, tool, weapon, disguise, key,
ingredients, crafted, consumable); two substantially different victory routes assembled from
generic data and scripts. The critical test (item 155 of the brief): *did implementing an escape
route need engine C++ specific to it?* If yes, the missing behaviour is made generic.

## Honest limits of this program

Online play, platform notarization, Windows-native verification, high-DPI looks and the feel of
the game on real GPUs/controllers need external hardware or accounts and are recorded in
[STATUS.md](STATUS.md#not-verified) and [NEXT.md](NEXT.md). Nothing is claimed beyond what a test
ran.
