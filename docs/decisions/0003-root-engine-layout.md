# ADR 0003: Root engine layout

Date: 2026-09-27
Status: Accepted; follows repository reorganization f20e619. Its "no application or game target" clause
is superseded by ADR 0004; the root layout stands.

## Context

The engine-only implementation originally lived under engine/. The latest repository
commit moved its sources, headers, tests, docs and licenses to the root, but omitted
CMake's project initialization and left presets/documentation pointing to the old layout.

## Decision

Keep the current root layout. The root CMakeLists.txt initializes the C/C++ project,
CTest and the engine target. Presets put generated output in ignored build/<preset>.
This supersedes ADR 0002's directory placement only; its library-only scope and private
physics dependency remain unchanged. No application or game target is added.

## Consequences

README links, build instructions and persistent engineering rules use actual root paths.
Consumers still add this repository as a subdirectory and link yk::engine. Native macOS
verification complements historical Windows evidence; it does not claim current MSVC
verification without executing that toolchain.
