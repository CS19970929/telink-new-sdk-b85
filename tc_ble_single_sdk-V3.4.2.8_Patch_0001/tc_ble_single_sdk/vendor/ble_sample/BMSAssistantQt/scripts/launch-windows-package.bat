@echo off
setlocal

set ROOT_DIR=%~dp0
set APP_EXE=%ROOT_DIR%BMSAssistantQt\BMSAssistantQt.exe

if not exist "%APP_EXE%" (
  echo 未找到 BMSAssistantQt.exe，请确认当前目录结构完整。
  pause
  exit /b 1
)

start "" "%APP_EXE%"
