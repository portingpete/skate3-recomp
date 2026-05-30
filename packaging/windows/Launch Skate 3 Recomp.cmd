@echo off
setlocal

set "ROOT=%~dp0"
set "APP_DIR=%ROOT%app"
set "APP_EXE=%ROOT%app\skate3.exe"
set "GAME_ROOT=%ROOT%work\runtime-assets"
set "USER_ROOT=%ROOT%work\user-data"
set "CACHE_ROOT=%ROOT%work\cache"

if /I "%~1"=="--dry-run" (
  echo Skate 3 Recomp launcher
  echo App: "%APP_EXE%"
  echo Game data: "%GAME_ROOT%"
  echo User data: "%USER_ROOT%"
  echo Cache: "%CACHE_ROOT%"
  echo Display default: 1920x1080 120Hz fullscreen
  echo PC settings include: 1440p, 4K, and ultrawide modes
  echo Physics timing: target_16_7ms
  echo PC settings: shown before game start
  echo Controls: MnK enabled, Start=P
  exit /b 0
)

if not exist "%APP_EXE%" (
  echo Missing Skate 3 Recomp executable:
  echo "%APP_EXE%"
  pause
  exit /b 1
)

if not exist "%GAME_ROOT%\default.xex" (
  echo Missing Skate 3 runtime files.
  echo Run "Setup Skate 3 Recomp.cmd" first.
  pause
  exit /b 1
)

if not exist "%USER_ROOT%" mkdir "%USER_ROOT%"
if not exist "%CACHE_ROOT%" mkdir "%CACHE_ROOT%"

start "Skate 3 Recomp" /D "%APP_DIR%" "%APP_EXE%" --game-data-root "%GAME_ROOT%" --user-data-root "%USER_ROOT%" --cache-root "%CACHE_ROOT%" --resolution 1080p --video-mode-width 1920 --video-mode-height 1080 --video-mode-refresh-rate 120 --window-width 1920 --window-height 1080 --fullscreen --skate3-physics-timing 1 --mnk-mode --keybind-start P
