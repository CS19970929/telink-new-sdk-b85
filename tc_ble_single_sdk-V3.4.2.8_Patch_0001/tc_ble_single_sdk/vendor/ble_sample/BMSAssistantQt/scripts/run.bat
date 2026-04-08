@echo off
setlocal enabledelayedexpansion

set ROOT_DIR=%~dp0..
set VENV_DIR=%ROOT_DIR%\.venv
set PYTHON_BIN=python

if not exist "%VENV_DIR%" (
  %PYTHON_BIN% -m venv "%VENV_DIR%"
)

call "%VENV_DIR%\Scripts\activate.bat"
python -m pip install --upgrade pip
python -m pip install -r "%ROOT_DIR%\requirements.txt"
python "%ROOT_DIR%\main.py"
