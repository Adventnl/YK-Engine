#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

echo 'Configuring the current engine source...'
cmake --preset release

echo 'Building the editor app...'
cmake --build --preset release --target yk_engine_app

echo 'Launching YK Engine...'
open "$root/build/release/YK Engine.app"
