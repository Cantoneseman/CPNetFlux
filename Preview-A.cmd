@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0desktop\ui-comparison\Preview.ps1" -Variant a
if errorlevel 1 pause
