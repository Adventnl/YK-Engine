#!/usr/bin/env sh
# Everything a change has to pass: formatting, whitespace, then a build and the whole test suite in
# every preset (dev, release, asan, headless). Takes a while on the first run.
#
#   scripts/verify.sh              # all presets
#   scripts/verify.sh dev release  # only these
#
# Behind a restricted network run scripts/fetch-deps.sh first and export YK_DEPS_DIR.
# scripts/verify-windows.sh checks the Windows build (cross-compiled, run under Wine).
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

presets=${*:-dev release asan headless}

cmake --preset dev >/dev/null
if command -v clang-format >/dev/null 2>&1; then
    cmake --build --preset dev --target format-check
else
    echo "verify: clang-format not found, skipping the format check" >&2
fi
git diff --check

for preset in $presets; do
    echo "== $preset"
    cmake --preset "$preset"
    cmake --build --preset "$preset"
    ctest --preset "$preset"
done
echo "verify: everything passed"
