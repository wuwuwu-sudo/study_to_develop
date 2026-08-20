@echo off
REM Thin wrapper that delegates to the PowerShell build script.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_tests.ps1"
exit /b %errorlevel%
