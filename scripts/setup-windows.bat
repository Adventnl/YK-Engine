@echo off
rem Double-click to install the build tools, build YK Engine and open the editor.
rem Extra arguments are passed on, e.g. setup-windows.bat -NoLaunch
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-windows.ps1" %*
if errorlevel 1 echo. & echo Setup stopped with an error. See the messages above.
pause
