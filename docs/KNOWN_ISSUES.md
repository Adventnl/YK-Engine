# Known issues

Real, current problems and limits, most important first. Each entry says whether it blocks
anything. Items fixed move to the "Fixed" list at the bottom with the commit's subject, so a later
session can see what was learned. The older, longer list of design limitations of the platformer
engine is in [STATUS.md](STATUS.md#known-limitations).

## Open

| # | Area | Issue | Blocks |
|---|---|---|---|
| 1 | Verification | Online play, notarization, Windows-native runs of the new systems, Retina rendering, real GPUs and controllers could not be exercised in the build environment (Linux, software renderer, dummy audio). | Release claims only |
| 2 | Environment | `cmake --preset dev` cannot download the pinned archives here (release hosts are filtered); use `scripts/fetch-deps.sh <dir>` and `-DYK_DEPS_DIR=<dir>`. CI has normal network access. | Local setup |
| 3 | Editor tests | The editor UI scripts take 1-3 minutes each in a Debug build under the software renderer; the whole suite is about 8 minutes with three jobs in parallel. Use `ctest -R <name>` while developing. | Iteration speed |

## Fixed

| Where | What |
|---|---|
| `yk info` | Crashed (segmentation fault) on any project that holds a file the engine does not classify, because a name table with eight entries was indexed by the nine-value `AssetKind` enum after `Dialogue` was added. Found by the baseline run of the `external_project` test; `assetKindName()` now has a switch over every kind. |
