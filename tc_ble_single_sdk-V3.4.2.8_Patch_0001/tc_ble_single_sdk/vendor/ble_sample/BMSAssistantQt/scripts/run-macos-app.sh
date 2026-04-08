#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_PATH="$ROOT_DIR/.dist/BMSAssistantQt.app"

if [ ! -d "$APP_PATH" ]; then
  "$ROOT_DIR/scripts/package-macos.sh"
fi

open "$APP_PATH"
