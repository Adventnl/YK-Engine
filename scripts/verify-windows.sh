#!/usr/bin/env sh
# Checks the Windows build from Linux: cross-compiles everything with MinGW-w64, runs every test
# executable under Wine, exports the demo game for Windows with the Windows `yk.exe` and starts the
# exported game for a few seconds.
#
#   scripts/verify-windows.sh            # everything
#   WINE=/path/to/wine64 scripts/verify-windows.sh
#
# Needs mingw-w64 (posix threads) and wine64 (Debian/Ubuntu: apt install mingw-w64 wine64). SDL
# uses its dummy video and audio drivers and the software renderer, so no display is needed.
# Behind a restricted network run scripts/fetch-deps.sh first and export YK_DEPS_DIR.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

wine=${WINE:-}
if [ -z "$wine" ]; then
    for candidate in wine64 wine /usr/lib/wine/wine64; do
        if command -v "$candidate" >/dev/null 2>&1; then
            wine=$candidate
            break
        fi
    done
fi
if [ -z "$wine" ]; then
    echo "verify-windows: wine64 was not found (set WINE=...)" >&2
    exit 2
fi

cmake --preset windows-cross
cmake --build --preset windows-cross

export WINEDEBUG=-all
export SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
build=build/windows-cross

failed=0
for test in "$build"/yk_*_tests.exe; do
    name=$(basename "$test")
    # The editor scripts and the install test need a Windows machine; these are the unit and
    # integration suites (the demo suite plays the whole level, so it checks physics parity).
    if (cd "$build" && timeout 600 "$wine" "./$name" >"$name.log" 2>&1); then
        echo "ok      $name"
    else
        echo "FAILED  $name (see $build/$name.log)"
        failed=$((failed + 1))
    fi
done

out="$build/export"
rm -rf "$out"
"$wine" "$build/yk.exe" export YK-DemoGame --target windows --out "$out" --zip
game=$(ls -d "$out"/*-windows)
(cd "$game" && "$wine" ./*.exe --frames 120 --fixed --no-audio --capture "../shot.bmp")
if [ -s "$out/shot.bmp" ]; then
    echo "ok      exported game ran and drew a frame ($out/shot.bmp)"
else
    echo "FAILED  the exported game did not produce a frame"
    failed=$((failed + 1))
fi

if [ "$failed" -ne 0 ]; then
    echo "verify-windows: $failed problem(s)" >&2
    exit 1
fi
echo "verify-windows: everything passed"
