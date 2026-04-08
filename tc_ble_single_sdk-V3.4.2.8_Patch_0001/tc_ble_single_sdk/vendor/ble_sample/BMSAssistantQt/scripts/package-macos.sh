#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
VENV_DIR="$ROOT_DIR/.venv"
DIST_DIR="$ROOT_DIR/.dist"

if [ ! -d "$VENV_DIR" ]; then
  "$PYTHON_BIN" -m venv "$VENV_DIR"
fi

source "$VENV_DIR/bin/activate"
python -m pip install --upgrade pip
python -m pip install -r "$ROOT_DIR/requirements.txt"

rm -rf "$ROOT_DIR/build" "$ROOT_DIR/dist" "$DIST_DIR"
mkdir -p "$DIST_DIR"

pyinstaller \
  --noconfirm \
  --windowed \
  --name BMSAssistantQt \
  --collect-all PySide6 \
  --hidden-import PySide6.QtBluetooth \
  --osx-bundle-identifier com.cs.bmsassistantqt \
  "$ROOT_DIR/main.py"

APP_PATH="$ROOT_DIR/dist/BMSAssistantQt.app"
INFO_PLIST="$APP_PATH/Contents/Info.plist"

if [ -f "$INFO_PLIST" ]; then
  /usr/libexec/PlistBuddy -c "Add :NSBluetoothAlwaysUsageDescription string 用于扫描并连接 BMS BLE 设备" "$INFO_PLIST" || true
  /usr/libexec/PlistBuddy -c "Add :NSBluetoothPeripheralUsageDescription string 用于扫描并连接 BMS BLE 设备" "$INFO_PLIST" || true
fi

cp -R "$APP_PATH" "$DIST_DIR/BMSAssistantQt.app"
echo "macOS app 已生成: $DIST_DIR/BMSAssistantQt.app"
