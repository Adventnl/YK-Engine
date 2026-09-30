#!/usr/bin/env bash
# Builds the macOS release of the engine: "YK Engine.app" and the disk image a user installs it from.
# Run it on a Mac with Xcode's command line tools, CMake 3.25+ and Ninja.
#
#   scripts/package-macos.sh                 build, assemble, sign ad hoc, write the .dmg
#   scripts/package-macos.sh --no-build      reuse build/release (only assemble, sign, package)
#
# Output: build/macos-dist/YK Engine.app and build/macos-dist/YKEngine-<version>-macos-<arch>.dmg
#
# Signing and notarization (where Apple's credentials enter the pipeline):
#   YK_CODESIGN_IDENTITY   a "Developer ID Application: Name (TEAMID)" identity in the keychain.
#                          Unset: the app is signed ad hoc, which runs on this Mac and, once
#                          downloaded elsewhere, needs Control-click > Open on first launch.
#   YK_NOTARY_PROFILE      a notarytool keychain profile (xcrun notarytool store-credentials ...).
#                          With it the .dmg is submitted to Apple, waited for and stapled, so it
#                          opens on any Mac without a warning. Needs YK_CODESIGN_IDENTITY.
# Other settings: YK_DEPS_DIR (pre-fetched dependencies, scripts/fetch-deps.sh).
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
[[ "$(uname -s)" == "Darwin" ]] || { echo "package-macos.sh must run on macOS" >&2; exit 1; }

build=1
[[ "${1:-}" == "--no-build" ]] && build=0

identity="${YK_CODESIGN_IDENTITY:-}"
profile="${YK_NOTARY_PROFILE:-}"
[[ -z "$profile" || -n "$identity" ]] || { echo "YK_NOTARY_PROFILE needs YK_CODESIGN_IDENTITY" >&2; exit 1; }

if [[ $build -eq 1 ]]; then
    cmake --preset release ${YK_DEPS_DIR:+-DYK_DEPS_DIR="$YK_DEPS_DIR"}
    cmake --build --preset release --target yk_engine_app
fi
version=$(sed -n 's/^project(YKEngine VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
arch=$(uname -m)
dist="build/macos-dist"
app="$dist/YK Engine.app"
dmg="$dist/YKEngine-$version-macos-$arch.dmg"
rm -rf "$dist"
mkdir -p "$dist"
# ditto keeps permissions, symlinks and extended attributes the way Finder does.
ditto "build/release/YK Engine.app" "$app"

echo "== Signing"
if [[ -n "$identity" ]]; then
    # Inside out: every program first, then the bundle that holds them. The hardened runtime and a
    # secure timestamp are what notarization requires.
    for program in "$app"/Contents/MacOS/*; do
        codesign --force --options runtime --timestamp --entitlements packaging/macos/entitlements.plist \
            --sign "$identity" "$program"
    done
    codesign --force --options runtime --timestamp --entitlements packaging/macos/entitlements.plist \
        --sign "$identity" "$app"
else
    codesign --force --deep --sign - "$app"
fi
plutil -lint "$app/Contents/Info.plist"
codesign --verify --deep --strict --verbose=2 "$app"

echo "== Disk image"
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
ditto "$app" "$stage/YK Engine.app"
ln -s /Applications "$stage/Applications"
hdiutil create -volname "YK Engine $version" -srcfolder "$stage" -ov -format UDZO "$dmg"
if [[ -n "$identity" ]]; then
    codesign --force --timestamp --sign "$identity" "$dmg"
fi

if [[ -n "$profile" ]]; then
    echo "== Notarizing (this waits for Apple)"
    xcrun notarytool submit "$dmg" --keychain-profile "$profile" --wait
    xcrun stapler staple "$dmg"
    xcrun stapler validate "$dmg"
    spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
fi

echo
echo "App:        $app"
echo "Disk image: $dmg"
shasum -a 256 "$dmg"
