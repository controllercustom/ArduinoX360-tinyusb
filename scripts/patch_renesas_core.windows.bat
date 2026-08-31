@echo off
rem Apply the ArduinoX360-tinyusb patch to the installed arduino:renesas_uno
rem core. Wrapper so the PowerShell script can be started by double-click.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_renesas_core.ps1"
pause
