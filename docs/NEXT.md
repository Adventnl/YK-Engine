# Next

What the last pass finished, what is partial, what failed, what to do next, which files matter and
which commands to run. Written at the end of every pass; the newest pass is first.

## Pass 1 (this document's author: the first long simulation pass)

### Completed (see [STATUS.md](STATUS.md) for the evidence)

- Phase A: audit, baseline, [build plan](ESCAPISTS2_BUILD_PLAN.md), ADRs 0016-0022, status files.
- Phase B (core): interpolation, typed values and blackboard, event payloads/wildcards/delays,
  services and update phases, `Json` property, extension slots on the registry.
- Phase C: world levels and per-level physics, spatial hash and `SpatialIndexService`, tilesets and
  tilemaps (runtime, collision, rendering), world grid, multi-level navigation, `NavigationAgent`,
  `CharacterMotor`.
- Phase D (partial) and E: stats, status effects, health with knockout, items, inventories,
  equipment, containers, pickups, loot tables and pools, crafting.
- Phase J (partial): the rule language, `RuleSet`, `RuleService`.

### Partial

- Tilemap painting and navigation overlays in the editor do not exist; maps are authored as JSON.
- `Json` properties are edited as text.
- Stamina exists as a stat; it has no consumers until movement and combat exist.
- The save contract exists on components; nothing assembles a save game yet.
- Equipment appearance layers are stored in item data (`equip.appearance`) but nothing draws them.

### Failed or dropped

- Nothing was abandoned. One audit finding (`yk info` crash) was fixed. Known limits are in
  [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

### Next, in order

1. Phase J remainder: quests and objectives on the rule language, the dialogue graph (extending
   `.ykdialogue`), cutscene sequences; convert `EventAction` to rules (`yk` command and an editor
   action) while keeping the component.
2. Phase D remainder: `PlayerCharacterController` on `CharacterMotor`, appearance layers.
3. Phase K: vendored Lua 5.4, sandbox, `ScriptComponent`, API, hot reload.
4. Phases F-H: world clock, schedules, zones, factions, perception and noise, AI brains and the AI
   debugger, crime and security, access policy.
5. Phase I and L: combat, carrying, tool actions on the world, destructibles, vents, hiding, search.
6. Phase M: `SaveGame` assembled from the component and service contract, slots, migration.
7. Phase N: runtime UI framework with reference-resolution scaling, inventory/container/crafting
   screens, HUD, minimap.
8. Phase O: `YK-SimulationDemo/` and its two escape routes as headless tests.
9. Phases P-S: local multiplayer, networking, authoring tools, packages, installer, CI.

### Files that matter

`include/yk/rules/`, `include/yk/stats/`, `include/yk/items/`, `include/yk/data/`,
`include/yk/world/`, `include/yk/navigation/`, `include/yk/runtime/Services.hpp`,
`include/yk/scene/Component.hpp` (update phases, save contract), `src/data/GameData.cpp`
(file kinds and sections), `src/assets/Validation.cpp` (project validation), `tests/unit/*_tests.cpp`.

### Commands

```sh
cmake --preset dev && cmake --build --preset dev
ctest --preset dev -R "^(foundation|world|tilemap|navigation|navigation_agent|rules|stats|items|loot_crafting)$"
ctest --preset dev            # everything (~15 minutes; the editor scripts dominate)
./build/dev/yk components > docs/components.md   # after any component or field change
clang-format -i <changed files>
```
Dependencies cannot be downloaded from GitHub archives on the build machine used so far: fetch them
with `scripts/fetch-deps.sh <dir>` and configure with `-DYK_DEPS_DIR=<dir>`.
