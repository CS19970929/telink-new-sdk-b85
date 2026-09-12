@echo off
setlocal enabledelayedexpansion

set "ROOT_DIR=%~dp0.."
set "STATE_DIR=%LOCALAPPDATA%\BMSAssistantQt"
if not defined LOCALAPPDATA set "STATE_DIR=%ROOT_DIR%\.state"
set "VENV_DIR=%STATE_DIR%\venv"
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

python "%ROOT_DIR%\main.py"
exit /b %errorlevel%

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
