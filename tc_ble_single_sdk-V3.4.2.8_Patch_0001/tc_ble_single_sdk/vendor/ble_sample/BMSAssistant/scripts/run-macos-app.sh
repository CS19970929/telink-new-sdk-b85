#!/bin/zsh

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR="$("$ROOT_DIR/scripts/build-macos-app.sh" | tail -n 1)"

open "$APP_DIR"
echo "$APP_DIR"
