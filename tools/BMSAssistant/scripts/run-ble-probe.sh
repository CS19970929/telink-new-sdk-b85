#!/bin/zsh

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR="$ROOT_DIR/.dist/BLEProbe.app"
OUT_PATH="${1:-$ROOT_DIR/.dist/ble-probe-report.json}"
DURATION="${2:-12}"

rm -rf "$APP_DIR"
mkdir -p "$APP_DIR/Contents/MacOS"

xcrun swiftc \
  -framework CoreBluetooth \
  "$ROOT_DIR/tools/ble-probe/BLEProbe.swift" \
  -o "$APP_DIR/Contents/MacOS/BLEProbe"

cp "$ROOT_DIR/tools/ble-probe/Info.plist" "$APP_DIR/Contents/Info.plist"
rm -f "$OUT_PATH"

open "$APP_DIR" --args --output "$OUT_PATH" --duration "$DURATION"

for _ in {1..25}; do
  if [[ -f "$OUT_PATH" ]]; then
    cat "$OUT_PATH"
    exit 0
  fi
  sleep 1
done

echo "BLE probe timeout, output file not generated: $OUT_PATH" >&2
exit 1
