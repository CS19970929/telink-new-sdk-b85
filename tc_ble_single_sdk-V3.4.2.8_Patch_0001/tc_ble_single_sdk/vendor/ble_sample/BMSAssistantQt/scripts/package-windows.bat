@echo off
setlocal enabledelayedexpansion

set "ROOT_DIR=%~dp0.."
set "STATE_DIR=%LOCALAPPDATA%\BMSAssistantQt"
if not defined LOCALAPPDATA set "STATE_DIR=%ROOT_DIR%\.state"
set "VENV_DIR=%STATE_DIR%\venv"
set "BUILD_WORK_DIR=%STATE_DIR%\build"
set "PYINSTALLER_DIST_DIR=%STATE_DIR%\pyinstaller-dist"
set "DIST_DIR=%ROOT_DIR%\.dist"
set "DOCS_DIR=%DIST_DIR%\docs"
set "PYTHON_BIN="

where python > nul 2> nul
if not errorlevel 1 set "PYTHON_BIN=python"
if not defined PYTHON_BIN (
  where py > nul 2> nul
  if not errorlevel 1 set "PYTHON_BIN=py -3"
)

if not defined PYTHON_BIN goto :no_python

if not exist "%STATE_DIR%" mkdir "%STATE_DIR%"
if errorlevel 1 goto :state_dir_failed

if exist "%VENV_DIR%\Scripts\python.exe" goto :venv_ready
%PYTHON_BIN% -m venv "%VENV_DIR%"
if errorlevel 1 goto :venv_failed

:venv_ready
call "%VENV_DIR%\Scripts\activate.bat"
python -m pip install --upgrade pip
if errorlevel 1 goto :pip_upgrade_failed

python -m pip install -r "%ROOT_DIR%\requirements.txt"
if errorlevel 1 goto :pip_install_failed

python "%ROOT_DIR%\main.py" --smoke-test
if errorlevel 1 goto :smoke_test_failed

if exist "%BUILD_WORK_DIR%" rmdir /s /q "%BUILD_WORK_DIR%"
if exist "%PYINSTALLER_DIST_DIR%" rmdir /s /q "%PYINSTALLER_DIST_DIR%"
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
if not exist "%DOCS_DIR%" mkdir "%DOCS_DIR%"

pyinstaller ^
  --noconfirm ^
  --windowed ^
  --name BMSAssistantQt ^
  --workpath "%BUILD_WORK_DIR%" ^
  --distpath "%PYINSTALLER_DIST_DIR%" ^
  --collect-all PySide6 ^
  --hidden-import PySide6.QtBluetooth ^
  "%ROOT_DIR%\main.py"
if errorlevel 1 goto :pyinstaller_failed

robocopy "%PYINSTALLER_DIST_DIR%\BMSAssistantQt" "%DIST_DIR%\BMSAssistantQt" /MIR > nul
if errorlevel 8 goto :copy_failed

copy /y "%ROOT_DIR%\README.md" "%DOCS_DIR%\README.md" > nul
copy /y "%ROOT_DIR%\WINDOWS-DELIVERY.md" "%DOCS_DIR%\WINDOWS-DELIVERY.md" > nul
for %%D in ("%ROOT_DIR%\..\..\..\docs\BMSWinAndroid*.md") do (
  if exist "%%~fD" copy /y "%%~fD" "%DOCS_DIR%\" > nul
)
copy /y "%ROOT_DIR%\scripts\launch-windows-package.bat" "%DIST_DIR%\Launch-BMSAssistantQt.bat" > nul
echo Windows package generated: %DIST_DIR%\BMSAssistantQt
exit /b 0

:no_python
echo Python was not found. Install Python 3.9+ or make sure the Windows py launcher is available.
exit /b 1

:state_dir_failed
echo Failed to create the BMSAssistantQt state directory.
exit /b 1

:venv_failed
echo Failed to create the Python virtual environment.
exit /b 1

:pip_upgrade_failed
echo Failed to upgrade pip.
exit /b 1

:pip_install_failed
echo Failed to install dependencies. Check network access and the Python environment.
exit /b 1

:smoke_test_failed
echo Qt smoke test failed. Check PySide6, QtBluetooth, and local runtime dependencies.
exit /b 1

:pyinstaller_failed
echo PyInstaller packaging failed.
exit /b 1

:copy_failed
echo Failed to copy packaged output.
exit /b 1
