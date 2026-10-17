@echo off
set "ROOT=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%desktop\ui-comparison\Preview.ps1" -Variant b %*
