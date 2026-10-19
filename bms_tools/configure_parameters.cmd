@echo off
setlocal
pushd "%~dp0.."
if errorlevel 1 exit /b 1
python -X utf8 -B "%~dp0configure_parameters.py" %*
set "BMS_CONFIG_EXIT_CODE=%ERRORLEVEL%"
if not "%BMS_CONFIG_EXIT_CODE%"=="0" pause
popd
exit /b %BMS_CONFIG_EXIT_CODE%
