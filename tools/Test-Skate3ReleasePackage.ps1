param(
  [string]$PackageZip = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 3.0

$repoRoot = Split-Path -Parent $PSScriptRoot
$packageRoot = Join-Path $repoRoot "packaging\windows"
$setupCmd = Join-Path $packageRoot "Setup Skate 3 Recomp.cmd"
$launchCmd = Join-Path $packageRoot "Launch Skate 3 Recomp.cmd"
$setupPs1 = Join-Path $packageRoot "launchers\Setup-Skate3Recomp.ps1"
$dropNote = Join-Path $packageRoot "Skate 3 Files\Put Skate 3 files here.txt"
$readme = Join-Path $packageRoot "README.txt"
$packager = Join-Path $repoRoot "tools\New-Skate3ReleasePackage.ps1"

function Require-File([string]$Path, [string]$Description) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Missing $Description`: $Path"
  }
}

function Require-Contains([string]$Path, [string]$Needle) {
  $text = Get-Content -LiteralPath $Path -Raw
  if ($text -notlike "*$Needle*") {
    throw "$Path does not contain expected text: $Needle"
  }
}

function Require-NotContains([string]$Path, [string]$Needle) {
  $text = Get-Content -LiteralPath $Path -Raw
  if ($text -like "*$Needle*") {
    throw "$Path contains forbidden text: $Needle"
  }
}

Require-File $setupCmd "portable setup command"
Require-File $launchCmd "portable game launcher"
Require-File $setupPs1 "portable setup GUI script"
Require-File $dropNote "drop-folder note"
Require-File $readme "release README"
Require-File $packager "release packager"

foreach ($path in @($setupCmd, $launchCmd, $setupPs1, $dropNote, $readme, $packager)) {
  Require-NotContains $path "K:\Skate3"
}

Require-Contains $setupCmd "%~dp0"
Require-Contains $setupCmd "launchers\Setup-Skate3Recomp.ps1"
Require-Contains $setupCmd "-STA"
Require-Contains $launchCmd "%~dp0"
Require-Contains $launchCmd "app\skate3.exe"
Require-Contains $launchCmd "work\runtime-assets"
Require-Contains $launchCmd "--skate3-physics-timing 1"
Require-Contains $launchCmd "--keybind-start P"
Require-Contains $launchCmd "1440p, 4K, and ultrawide"
Require-Contains $setupPs1 "Start-Skate3SetupGui"
Require-Contains $setupPs1 "Invoke-Skate3Setup"
Require-Contains $setupPs1 "default.xex_uncrypted.xex"
Require-Contains $setupPs1 "Launch Skate 3 Recomp.cmd"
Require-Contains $setupPs1 ".add_DoWork("
Require-Contains $setupPs1 ".add_ProgressChanged("
Require-Contains $setupPs1 ".add_RunWorkerCompleted("
Require-NotContains $setupPs1 '$worker.DoWork +='
Require-NotContains $setupPs1 '$worker.ProgressChanged +='
Require-NotContains $setupPs1 '$worker.RunWorkerCompleted +='
Require-Contains $readme "Setup Skate 3 Recomp.cmd"
Require-Contains $readme "Skate 3 Files"
Require-Contains $readme "default.xex_uncrypted.xex"
Require-Contains $readme "1440p, 4K, and ultrawide"
Require-Contains $packager "Compress-Archive"
Require-Contains $packager "Assert-NoGameFiles"

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $setupPs1 -SelfTest | Out-Host
if ($LASTEXITCODE -ne 0) {
  throw "Portable setup GUI self-test failed."
}

if (-not [string]::IsNullOrWhiteSpace($PackageZip)) {
  if (-not (Test-Path -LiteralPath $PackageZip -PathType Leaf)) {
    throw "Package zip not found: $PackageZip"
  }
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $zip = [System.IO.Compression.ZipFile]::OpenRead($PackageZip)
  try {
    $entries = @($zip.Entries | ForEach-Object { $_.FullName -replace '\\', '/' })
    foreach ($required in @(
      "Skate3Recomp-Windows/Setup Skate 3 Recomp.cmd",
      "Skate3Recomp-Windows/Launch Skate 3 Recomp.cmd",
      "Skate3Recomp-Windows/launchers/Setup-Skate3Recomp.ps1",
      "Skate3Recomp-Windows/Skate 3 Files/Put Skate 3 files here.txt",
      "Skate3Recomp-Windows/app/skate3.exe",
      "Skate3Recomp-Windows/app/rexruntimerd.dll"
    )) {
      if ($entries -notcontains $required) {
        throw "Package zip is missing $required"
      }
    }

    foreach ($forbidden in @(
      "Skate3Recomp-Windows/Skate 3 Files/default.xex",
      "Skate3Recomp-Windows/Skate 3 Files/default.xex_uncrypted.xex",
      "Skate3Recomp-Windows/Skate 3 Files/nxeart",
      "Skate3Recomp-Windows/Skate 3 Files/data/"
    )) {
      if ($entries -contains $forbidden) {
        throw "Package zip contains game data placeholder/path: $forbidden"
      }
    }
  } finally {
    $zip.Dispose()
  }
}

"Skate 3 release package checks passed"
