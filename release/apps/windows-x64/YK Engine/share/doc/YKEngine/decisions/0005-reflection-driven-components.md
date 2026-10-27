# ADR 0005: Reflection-driven components are the single source of truth

Date: 2026-09-29. Status: accepted.

## Context

Serialization, an inspector, prefabs, undo, copy/paste, documentation and validation all need to
know what a component's fields are. Writing that knowledge per component and per feature makes every
new component cost several files and lets the parts drift apart.

## Decision

Components are plain classes with public members. A `ComponentRegistry` (an explicit object, no
global) holds a `ComponentType` per class with a `PropertyInfo` per field (name, type, range, enum
options, asset kind, flags such as `size`, `offset`, `layer`, `displacement`, `readOnly`) plus
getter/setter closures created by `TypeBuilder<T>::field(name, &T::member)`. Property types are a
closed set: bool, int, float, string, vec2, color, enum, entity reference, entity reference list,
string list, asset path. All writes go through `PropertyInfo::assign`, which validates and clamps.

Serializers, the inspector, prefab remapping of entity references, the resize gizmo, picking,
project validation and `docs/components.md` are generic code over that data. A registry is composed
by the host (`registerAllModules`), so a game module extends what the editor can edit without editor
changes. Enums serialize by option name, entities by hex id, assets by project-relative path.

## Consequences

Adding a component or field is one declaration. A field type outside the closed set needs a new
`PropertyType` and inspector widget (deliberately not free-form). Reflection is by member pointer,
so components stay ordinary aggregates that tests construct directly. Runtime-only state is exposed
as `readOnly` properties so the inspector shows it while playing and files never store it.
Registration mistakes (duplicate names, enum without options, dependency cycles) are programming
errors and throw at startup; `registry.validate()` checks cross references.
