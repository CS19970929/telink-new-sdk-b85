#!/bin/zsh

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR="$ROOT_DIR/.dist/BMSAssistant.app"
BIN_PATH="$ROOT_DIR/.build/arm64-apple-macosx/debug/BMSAssistant"
INFO_PLIST="$ROOT_DIR/Resources/Info.plist"

cd "$ROOT_DIR"
swift build >&2

rm -rf "$APP_DIR"
mkdir -p "$APP_DIR/Contents/MacOS"
cp "$BIN_PATH" "$APP_DIR/Contents/MacOS/BMSAssistant"
cp "$INFO_PLIST" "$APP_DIR/Contents/Info.plist"

echo "$APP_DIR"
