#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
cmake --preset release
cmake --build --preset release
rm -rf build/package/ElementalEscape build/package/ElementalEscape-linux-x86_64.tar.gz
cmake --install build/release --prefix build/package/ElementalEscape
rm -rf build/package/ElementalEscape/include build/package/ElementalEscape/lib
cmake -E tar cfvz build/package/ElementalEscape-linux-x86_64.tar.gz --format=gnutar build/package/ElementalEscape
printf '%s\n' "$root/build/package/ElementalEscape-linux-x86_64.tar.gz"
