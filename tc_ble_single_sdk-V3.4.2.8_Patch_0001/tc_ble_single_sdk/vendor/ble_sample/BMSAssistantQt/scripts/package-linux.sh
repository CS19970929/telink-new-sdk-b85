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
  "$ROOT_DIR/main.py"

cp -R "$ROOT_DIR/dist/BMSAssistantQt" "$DIST_DIR/BMSAssistantQt"
echo "Linux 包已生成: $DIST_DIR/BMSAssistantQt"
