#!/usr/bin/env bash
# Opens the verified Installer package for the newest completed GitHub release.
# The installer handles authorization and replacing the app in /Applications.
set -euo pipefail
current=${1:?current version required}
bundle=${2:?bundle path required}
[[ -d "$bundle/Contents" ]] || exit 0

metadata=$(mktemp "${TMPDIR:-/tmp}/yk-release.XXXXXX")
trap 'rm -f "$metadata"' EXIT
curl -fsSL --connect-timeout 5 --max-time 15 \
    -H 'Accept: application/vnd.github+json' -H 'User-Agent: YK-Engine-Updater' \
    'https://api.github.com/repos/Adventnl/YK-Engine/releases/latest' -o "$metadata" || exit 0
tag=$(/usr/bin/plutil -extract tag_name raw -o - "$metadata" 2>/dev/null) || exit 0
[[ "$tag" =~ ^v([0-9]+\.[0-9]+\.[0-9]+(\.[0-9]+)?)$ ]] || exit 0
latest=${BASH_REMATCH[1]}

# sort -V is unavailable on macOS; compare each numeric component without octal parsing.
IFS=. read -r -a newest_parts <<< "$latest"
IFS=. read -r -a current_parts <<< "$current"
newer=0
for index in 0 1 2 3; do
    left=$((10#${newest_parts[index]:-0}))
    right=$((10#${current_parts[index]:-0}))
    if (( left > right )); then newer=1; break; fi
    if (( left < right )); then break; fi
done
(( newer )) || exit 0

arch=$(uname -m)
[[ "$arch" == arm64 || "$arch" == x86_64 ]] || exit 0
name="YKEngine-$latest-macos-$arch.pkg"
base="https://github.com/Adventnl/YK-Engine/releases/download/$tag"
folder=$(mktemp -d "${TMPDIR:-/tmp}/yk-engine-update.XXXXXX")
trap 'rm -f "$metadata"; rm -rf "$folder"' EXIT
curl -fsSL --connect-timeout 5 --max-time 60 "$base/SHA256SUMS.txt" -o "$folder/SHA256SUMS.txt" || exit 0
curl -fsSL --connect-timeout 5 --max-time 300 "$base/$name" -o "$folder/$name" || exit 0
line=$(grep -E "^[[:xdigit:]]{64}[[:space:]]+\\*?$name$" "$folder/SHA256SUMS.txt") || exit 0
[[ $(printf '%s\n' "$line" | wc -l | tr -d ' ') == 1 ]] || exit 0
expected=${line:0:64}
actual=$(shasum -a 256 "$folder/$name" | cut -d ' ' -f 1)
[[ "$(printf '%s' "$actual" | tr '[:upper:]' '[:lower:]')" == \
   "$(printf '%s' "$expected" | tr '[:upper:]' '[:lower:]')" ]] || exit 0
open -a Installer "$folder/$name" || exit 0
trap 'rm -f "$metadata"' EXIT
exit 10
