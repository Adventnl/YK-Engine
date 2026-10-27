# ADR 0018: One condition/action language for every kind of game logic

Date: 2026-10-01. Status: accepted.

## Context

Doors, quests, dialogue choices, interactions, AI transitions, UI bindings, item uses, recipes and
scenario rules all need "if this holds, then do that". The existing `EventAction` covered one
shape (an event leads to a fixed list of consequences). Building five separate condition systems
would have meant five validators, five editors and five sets of bugs.

## Decision

- **A `RuleCatalog`** (stored as an extension slot of the `ComponentRegistry`) holds the predicates
  (conditions), actions and fact namespaces. Every module adds its own at registration
  (`registerStatRules`, `registerItemRules`, ...); `registerCoreRules` brings variables, events,
  control flow (`If`, `Sequence`, `Delay`, `Repeat`) and entity actions.
- **Conditions** are JSON: `{"all": [...]}`, `{"any": [...]}`, `{"not": ...}`, a comparison of a
  fact (`{"var": "power", "op": "==", "value": 0}`, `{"fact": "actor.stat.health.pct", "op": "<",
  "value": 50}`) or a registered predicate (`{"type": "HasItem", "item": "key"}`). A fact nobody
  set reads as the zero of what it is compared with. `"$fact.path"` works anywhere a value does.
- **A `Rule`** is WHEN (event pattern with source/other/data filters, or a timer) IF condition THEN
  actions ELSE actions, with `once`, `cooldown` and `priority`. `RuleSet` is the component;
  `RuleService` runs the rules of every set, schedules `Delay`/`Repeat` continuations and saves what
  is used up.
- **Parameters are declared** (`ParamSpec`: kind, required, ref kind, options) so the validator
  checks types and references (`known(kind, id)` against `GameData`) and an editor can build a form
  from the same list.
- **`RuleContext`** names `self`, `actor`, `target` and the event, so a rule reads the same wherever
  it lives.

## Consequences

Features add predicates and actions instead of conditions of their own. Rules on entities, in item
and effect definitions, in recipes and in container permissions are all validated by one code
path. `EventAction` keeps working unchanged; converting it to a `RuleSet` is listed in NEXT.md.
