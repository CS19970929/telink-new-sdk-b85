@echo off
setlocal enabledelayedexpansion

set ROOT_DIR=%~dp0..
set VENV_DIR=%ROOT_DIR%\.venv
set DIST_DIR=%ROOT_DIR%\.dist
set DOCS_DIR=%DIST_DIR%\docs
set PYTHON_BIN=python

if not exist "%VENV_DIR%" (
  %PYTHON_BIN% -m venv "%VENV_DIR%"
)

call "%VENV_DIR%\Scripts\activate.bat"
python -m pip install --upgrade pip
python -m pip install -r "%ROOT_DIR%\requirements.txt"

if exist "%ROOT_DIR%\build" rmdir /s /q "%ROOT_DIR%\build"
if exist "%ROOT_DIR%\dist" rmdir /s /q "%ROOT_DIR%\dist"
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
if not exist "%DOCS_DIR%" mkdir "%DOCS_DIR%"

pyinstaller ^
  --noconfirm ^
  --windowed ^
  --name BMSAssistantQt ^
  --collect-all PySide6 ^
  --hidden-import PySide6.QtBluetooth ^
  "%ROOT_DIR%\main.py"

xcopy /e /i /y "%ROOT_DIR%\dist\BMSAssistantQt" "%DIST_DIR%\BMSAssistantQt" > nul
copy /y "%ROOT_DIR%\README.md" "%DOCS_DIR%\README.md" > nul
copy /y "%ROOT_DIR%\WINDOWS-DELIVERY.md" "%DOCS_DIR%\WINDOWS-DELIVERY.md" > nul
copy /y "%ROOT_DIR%\scripts\launch-windows-package.bat" "%DIST_DIR%\Launch-BMSAssistantQt.bat" > nul
echo Windows 包已生成: %DIST_DIR%\BMSAssistantQt
