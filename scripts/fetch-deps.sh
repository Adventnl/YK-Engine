#!/usr/bin/env sh
# Fetch the pinned third-party sources as verified shallow git checkouts.
#
# Use this when the build machine cannot download release archives (restricted networks, CI
# mirrors). Every checkout is verified against the exact commit recorded below, so the result is
# as content-pinned as the SHA-256 archive downloads used by default.
#
#   scripts/fetch-deps.sh [destination]          default: $YK_DEPS_DIR, else <repo>/.deps
#   cmake --preset dev -DYK_DEPS_DIR=<destination>     (or export YK_DEPS_DIR)
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dest=${1:-${YK_DEPS_DIR:-$root/.deps}}
mkdir -p "$dest"

fetch() { # name url tag commit
    name=$1 url=$2 tag=$3 commit=$4
    if [ -d "$dest/$name/.git" ] && [ "$(git -C "$dest/$name" rev-parse HEAD)" = "$commit" ]; then
        echo "$name: already at $tag ($commit)"
        return
    fi
    rm -rf "${dest:?}/$name"
    GIT_LFS_SKIP_SMUDGE=1 git clone --quiet --depth 1 --branch "$tag" "$url" "$dest/$name"
    actual=$(git -C "$dest/$name" rev-parse HEAD)
    if [ "$actual" != "$commit" ]; then
        echo "$name: tag $tag resolved to $actual, expected $commit" >&2
        rm -rf "${dest:?}/$name"
        exit 1
    fi
    echo "$name: $tag ($commit)"
}

fetch box2d https://github.com/erincatto/box2d v3.1.1 8c661469c9507d3ad6fbd2fea3f1aa71669c2fe3
fetch SDL https://github.com/libsdl-org/SDL release-3.2.28 7f3ae3d57459e59943a4ecfefc8f6277ec6bf540
fetch imgui https://github.com/ocornut/imgui v1.92.9-docking 9b4eb24cee2071e61dc1f9ef3e5228097cdde720
echo "Configure with: cmake --preset dev -DYK_DEPS_DIR=$dest"
