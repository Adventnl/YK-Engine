#!/usr/bin/env bash
# Checks the packaged macOS application the way a user meets it: from the .dmg, copied somewhere
# else, launched through Launch Services (what Finder does) and closed the way the system asks
# programs to close. Run by CI after scripts/package-macos.sh; also usable on a Mac by hand.
#
#   scripts/verify-macos-app.sh [dmg]        default: the newest build/macos-dist/*.dmg
#
# Leaves screenshots and logs in build/macos-verify (CI uploads them).
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
[[ "$(uname -s)" == "Darwin" ]] || { echo "verify-macos-app.sh must run on macOS" >&2; exit 1; }
dmg="${1:-$(ls -t build/macos-dist/*.dmg | head -1)}"
pkg="${dmg%.dmg}.pkg"
[[ -f "$pkg" ]] || { echo "FAILED: missing Installer package $pkg" >&2; exit 1; }
pkgutil --payload-files "$pkg" | grep 'YK Engine.app/Contents/MacOS/yk_editor' > /dev/null || {
    echo "FAILED: Installer package has no editor" >&2; exit 1;
}
work="$root/build/macos-verify"
rm -rf "$work"
mkdir -p "$work/mount" "$work/installed" "$work/games" "$work/shots" "$work/logs"

pass() { echo "ok: $*"; }
fail() { echo "FAILED: $*" >&2; exit 1; }
# Waits (up to $2 seconds) until $1 succeeds.
wait_for() { local n=0; until eval "$1"; do sleep 1; n=$((n+1)); [[ $n -lt ${2:-30} ]] || return 1; done; }

echo "== The disk image"
hdiutil attach -nobrowse -readonly -mountpoint "$work/mount" "$dmg" > /dev/null
trap 'hdiutil detach "$work/mount" -force > /dev/null 2>&1 || true' EXIT
[[ -d "$work/mount/YK Engine.app" ]] || fail "the disk image has no YK Engine.app"
[[ -L "$work/mount/Applications" ]] || fail "the disk image has no Applications link"
pass "the disk image holds YK Engine.app and an Applications link"
# Copy it out like a user dragging it to Applications (a path with a space, far from the build tree).
app="$work/installed/YK Engine.app"
ditto "$work/mount/YK Engine.app" "$app"
hdiutil detach "$work/mount" > /dev/null
trap - EXIT

echo "== The application bundle"
plutil -lint "$app/Contents/Info.plist" > /dev/null && pass "Info.plist is valid"
codesign --verify --deep --strict "$app" && pass "the signature verifies after copying"
lipo -info "$app/Contents/MacOS/yk_editor"
for program in yk_editor yk_player yk; do
    [[ -x "$app/Contents/MacOS/$program" ]] || fail "$program is missing or not executable"
done
# Apple's own tool has to accept the icon file.
iconutil --convert iconset --output "$work/engine.iconset" "$app/Contents/Resources/AppIcon.icns" \
    && ls "$work/engine.iconset" | head -3 && pass "AppIcon.icns is accepted by iconutil"
otool -L "$app/Contents/MacOS/yk_editor" | grep -v "^/System\|/usr/lib\|^$app" || true
if otool -L "$app/Contents/MacOS/yk_editor" | grep -q "@rpath\|/usr/local\|/opt/homebrew"; then
    fail "the editor depends on a library outside the system"
fi
pass "the editor links only system libraries"

echo "== The editor, launched with a window (real video driver, real renderer)"
demo="$app/Contents/Resources/YK-DemoGame"
[[ -f "$demo/project.ykproj" ]] || fail "the demo game is not in Resources"
"$app/Contents/MacOS/yk_editor" "$demo" --no-audio --size 1600x900 --frames 90 \
    --capture "$work/shots/editor.bmp" --settings-dir "$work/editor-settings" \
    > "$work/logs/editor-run.txt" 2>&1 || { cat "$work/logs/editor-run.txt"; fail "the editor did not run"; }
grep -q "Opened project 'Cinder Vale'" "$work/logs/editor-run.txt" || fail "the editor did not open the demo game"
[[ -s "$work/shots/editor.bmp" ]] && sips -s format png "$work/shots/editor.bmp" --out "$work/shots/editor.png" > /dev/null
pass "the editor drew 90 frames and captured one"
grep -q "Clean shutdown" "$work/editor-settings/logs/editor.log" && pass "the editor shut down cleanly (log file)"

echo "== A game exported by the packaged tool: signed, with a disk image"
"$app/Contents/MacOS/yk" export "$demo" --target macos --out "$work/games" --zip --dmg
game="$work/games/Cinder Vale.app"
codesign --verify --deep --strict "$game" && pass "the exported game's signature verifies"
plutil -lint "$game/Contents/Info.plist" > /dev/null && pass "the game's Info.plist is valid"
iconutil --convert iconset --output "$work/game.iconset" "$game/Contents/Resources/AppIcon.icns" \
    && pass "the game's icon is accepted by iconutil"
[[ -f "$work/games/Cinder Vale.dmg" && -f "$work/games/Cinder Vale.app.zip" ]] || fail "missing .dmg or .zip"
hdiutil attach -nobrowse -readonly -mountpoint "$work/mount" "$work/games/Cinder Vale.dmg" > /dev/null
[[ -d "$work/mount/Cinder Vale.app/Contents/Resources/data" ]] || fail "the game's disk image is incomplete"
hdiutil detach "$work/mount" > /dev/null
pass "the game's disk image mounts and holds the app"
# Nothing of the editor is in a game: one program, no ImGui, no editor fonts.
[[ $(ls "$game/Contents/MacOS" | wc -l) -eq 1 ]] || fail "the game carries more than one program"
if strings "$game/Contents/MacOS/CinderVale" | grep -q "ImGui\|Dear ImGui"; then
    fail "the game contains the editor's UI library"
fi
ls -l "$game/Contents/MacOS/CinderVale" "$app/Contents/MacOS/yk_editor" | awk '{print $5, $9}'
pass "the game contains no editor code"

echo "== The game, launched like a double-click (Launch Services), from another folder"
mkdir -p "$work/somewhere else"
ditto "$game" "$work/somewhere else/Cinder Vale.app"
log_dir="$HOME/Library/Logs/CinderVale"
rm -rf "$log_dir"
open -n -W "$work/somewhere else/Cinder Vale.app" --args --frames 120 --fixed --capture "$work/shots/game.bmp"
[[ -s "$work/shots/game.bmp" ]] || fail "the launched game drew no frame"
sips -s format png "$work/shots/game.bmp" --out "$work/shots/game.png" > /dev/null
grep -q "Loaded scene scenes/level01.ykscene" "$log_dir/player.log" || fail "the game's log has no start-up in $log_dir"
grep -q "Clean shutdown" "$log_dir/player.log" || fail "the game did not shut down cleanly"
cp "$log_dir/player.log" "$work/logs/game-player.log"
pass "the game started from Launch Services, logged to ~/Library/Logs/CinderVale and closed cleanly"

echo "== The editor, launched by opening a project document, then asked to quit"
project_copy="$work/Project Copy"
mkdir -p "$project_copy"
ditto "$demo" "$project_copy/Cinder Vale"
editor_logs="$HOME/Library/Logs/YKEngine/Editor"
rm -rf "$editor_logs"
open -n -a "$work/installed/YK Engine.app" "$project_copy/Cinder Vale/project.ykproj"
wait_for "grep -q \"Opened project 'Cinder Vale'\" \"$editor_logs/editor.log\" 2>/dev/null" 40 \
    || { cat "$editor_logs/editor.log" 2>/dev/null || true; fail "the project document did not open in the editor"; }
pass "opening project.ykproj through Launch Services started the editor with that project"
# The way the system asks an application to quit (Cmd+Q, Dock > Quit and Apple's own shutdown all
# arrive as this event). Where the automation permission for osascript is missing, SIGTERM is the
# same request: SDL turns both into its quit event.
osascript -e 'tell application id "com.yk.engine" to quit' > /dev/null 2>&1 || echo "osascript could not send the quit event; using SIGTERM"
if ! wait_for "grep -q 'Clean shutdown' \"$editor_logs/editor.log\"" 15; then
    pkill -TERM -f 'YK Engine.app/Contents/MacOS/yk_editor' || true
fi
wait_for "grep -q 'Clean shutdown' \"$editor_logs/editor.log\"" 20 || fail "the editor did not shut down after the quit request"
wait_for "! pgrep -f 'YK Engine.app/Contents/MacOS/yk_editor' > /dev/null" 15 || fail "the editor is still running"
cp "$editor_logs/editor.log" "$work/logs/editor-launchservices.log"
pass "the editor quit on request and shut down cleanly; no process is left"

echo
echo "All checks passed. Screenshots and logs: $work"
