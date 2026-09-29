#!/usr/bin/env sh
# Builds the release preset and packs the editor, player, sample project, docs and license notices
# with CPack: build/package/YKEngine-<version>-<system>.tar.gz (a .zip on Windows).
#
#   scripts/package.sh
#
# Behind a restricted network run scripts/fetch-deps.sh first and export YK_DEPS_DIR.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
cmake --preset release
cmake --build --preset release
rm -rf build/package
cpack --config build/release/CPackConfig.cmake -B build/package
