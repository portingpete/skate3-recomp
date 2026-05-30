@echo off
setlocal

set "ROOT=%~dp0"
set "SETUP_SCRIPT=%ROOT%launchers\Setup-Skate3Recomp.ps1"

if /I "%~1"=="--dry-run" (
  echo Skate 3 Recomp setup
  echo Setup GUI: "%SETUP_SCRIPT%"
  echo Game files folder: "%ROOT%Skate 3 Files"
  echo Working copy: "%ROOT%work\assets"
  echo Runtime copy: "%ROOT%work\runtime-assets"
  exit /b 0
)

if not exist "%SETUP_SCRIPT%" (
  echo Missing setup GUI:
  echo "%SETUP_SCRIPT%"
  pause
  exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File "%SETUP_SCRIPT%"
