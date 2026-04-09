@echo off
setlocal

set "ROOT_DIR=%~dp0"
set "APP_ROOT=%ROOT_DIR%BMSAssistantQt"
set "APP_EXE=%APP_ROOT%\BMSAssistantQt.exe"
set "PYSIDE_DIR=%APP_ROOT%\_internal\PySide6"
set "QT_PLUGIN_PATH=%PYSIDE_DIR%\plugins"
set "QT_QPA_PLATFORM_PLUGIN_PATH=%PYSIDE_DIR%\plugins\platforms"
set "QT_QPA_PLATFORM=windows"
set "PATH=%PYSIDE_DIR%;%PATH%"

if not exist "%APP_EXE%" (
  echo BMSAssistantQt.exe was not found. Check the package directory layout.
  pause
  exit /b 1
)

start "" "%APP_EXE%"
