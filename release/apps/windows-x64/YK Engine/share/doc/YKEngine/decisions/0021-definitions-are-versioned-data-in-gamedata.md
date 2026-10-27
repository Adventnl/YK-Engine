# ADR 0021: Definitions are versioned data files merged into `GameData`

Date: 2026-10-01. Status: accepted.

## Context

Items, effects, stats, recipes, loot, quests, schedules, brains and the rest are the *content* of a
simulation game and must be authorable without recompiling, validated before anything runs, and
shared by the runtime, the validator, the editor and the command line.

## Decision

- **Files.** A definition file is JSON with an optional `format` (`yk.data`, `yk.item`, `yk.recipe`,
  `yk.loot`) and `version`. `.ykdata` may hold any sections (`stats`, `effects`, `items`,
  `lootTables`, `lootPools`, `recipes`, `tables`); the specialised extensions allow shorter shapes
  (a `.ykitem` or `.ykrecipe` that is just the definition, a `.ykloot` with `tables` and `pools`).
  A project may use any number of files; they are merged. A newer `version` than the engine reads is
  an error, unknown keys are warnings (a typo must not silently do nothing).
- **`GameData`** loads every file the `AssetSource` lists (`AssetSource::list(extension)`), keeps
  what is good and reports each problem with its file; the validator (`validateProject`) turns
  problems into errors and warnings, checks definitions against each other (an item's equip
  modifier names a stat that exists) and walks every rule inside them against the `RuleCatalog`.
  `DataService` serves it to the running game (or a host passes one in `RuntimeOptions::data`).
- **Stable ids.** Everything is referred to by string id (`ItemId`), validated by `[a-zA-Z0-9_-]`.
  Nothing saved or serialized holds a pointer or an index.
- **Derived definitions.** An item that can be worn gets a status effect `equip:<id>` synthesized
  at load, so stats, flags, permissions and appearance of equipment use the effect system.
- **Extension, not fork.** New sections are added to `GameData::add`; old files load unchanged.

## Consequences

Every new definition kind needs a parser with errors, a catalog `check`, `visitRules`/`visitAssets`
hooks, a summary for the Inspector, tests and documentation (STATUS.md tracks which kinds have
which). A game-specific table that is not worth a kind goes in `tables`.
