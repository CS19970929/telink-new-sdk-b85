@echo off
setlocal
cd /d "%~dp0.."
where py >nul 2>nul
if %errorlevel%==0 (
  py -3 ota_tool.py
) else (
  python ota_tool.py
)
endlocal
