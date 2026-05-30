param(
  [switch]$SelfTest,
  [switch]$SetupOnly,
  [string]$SourceRoot
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 3.0

function Get-ProjectRoot {
  $scriptRoot = $PSScriptRoot
  if ([string]::IsNullOrWhiteSpace($scriptRoot)) {
    $scriptRoot = Split-Path -Parent $PSCommandPath
  }
  if ((Split-Path -Leaf $scriptRoot) -ieq "launchers") {
    return (Split-Path -Parent $scriptRoot)
  }
  return $scriptRoot
}

function Join-SetupPath([string]$Root, [string]$Child) {
  return [System.IO.Path]::GetFullPath((Join-Path $Root $Child))
}

function Get-Skate3SetupPaths {
  $projectRoot = [System.IO.Path]::GetFullPath((Get-ProjectRoot))
  [pscustomobject]@{
    ProjectRoot        = $projectRoot
    DropRoot           = Join-SetupPath $projectRoot "Skate 3 Files"
    ExistingSourceRoot = Join-SetupPath $projectRoot "skate3"
    AssetsRoot         = Join-SetupPath $projectRoot "work\assets"
    RuntimeRoot        = Join-SetupPath $projectRoot "work\runtime-assets"
    UserRoot           = Join-SetupPath $projectRoot "work\user-data"
    CacheRoot          = Join-SetupPath $projectRoot "work\cache"
    LauncherPath       = Join-SetupPath $projectRoot "Launch Skate 3 Recomp.cmd"
  }
}

function Assert-UnderPath([string]$Path, [string]$ParentPath, [string]$Description) {
  $fullPath = [System.IO.Path]::GetFullPath($Path)
  $fullParent = [System.IO.Path]::GetFullPath($ParentPath).TrimEnd('\') + '\'
  if (-not $fullPath.StartsWith($fullParent, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "$Description must stay under $fullParent, got $fullPath"
  }
}

function Get-OptionalFileHash([string]$Path) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    return ""
  }
  return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
}

function Test-Skate3Source([string]$CandidateRoot) {
  $candidateRoot = [System.IO.Path]::GetFullPath($CandidateRoot)
  $defaultXex = Join-Path $candidateRoot "default.xex"
  $uncryptedXex = Join-Path $candidateRoot "default.xex_uncrypted.xex"
  $dataRoot = Join-Path $candidateRoot "data"
  $nxeart = Join-Path $candidateRoot "nxeart"

  if (-not (Test-Path -LiteralPath $candidateRoot -PathType Container)) {
    return [pscustomobject]@{ IsValid = $false; SourceRoot = $candidateRoot; RuntimeXex = ""; Message = "Folder not found." }
  }
  if (-not (Test-Path -LiteralPath $dataRoot -PathType Container)) {
    return [pscustomobject]@{ IsValid = $false; SourceRoot = $candidateRoot; RuntimeXex = ""; Message = "Missing data folder." }
  }
  if (-not (Test-Path -LiteralPath $defaultXex -PathType Leaf)) {
    return [pscustomobject]@{ IsValid = $false; SourceRoot = $candidateRoot; RuntimeXex = ""; Message = "Missing default.xex." }
  }

  $runtimeXex = ""
  if (Test-Path -LiteralPath $uncryptedXex -PathType Leaf) {
    $runtimeXex = $uncryptedXex
  } else {
    $knownDecryptedHash = "FC4D26404382CB2C2FC2C0EE3ADE9E7B5BF3635CFB6ECD43CA432EBAF3EFD080"
    if ((Get-OptionalFileHash $defaultXex) -eq $knownDecryptedHash) {
      $runtimeXex = $defaultXex
    }
  }

  if ([string]::IsNullOrWhiteSpace($runtimeXex)) {
    return [pscustomobject]@{
      IsValid = $false
      SourceRoot = $candidateRoot
      RuntimeXex = ""
      Message = "Missing default.xex_uncrypted.xex. Add the decrypted XEX beside default.xex."
    }
  }

  [pscustomobject]@{
    IsValid = $true
    SourceRoot = $candidateRoot
    RuntimeXex = $runtimeXex
    HasNxeArt = (Test-Path -LiteralPath $nxeart -PathType Leaf)
    Message = "Ready."
  }
}

function Find-Skate3SourceRoot([object]$Paths, [string]$RequestedSourceRoot = "") {
  if (-not [string]::IsNullOrWhiteSpace($RequestedSourceRoot)) {
    $explicit = Test-Skate3Source $RequestedSourceRoot
    if ($explicit.IsValid) {
      return $explicit
    }
    throw "The selected folder is not ready: $($explicit.Message)"
  }

  New-Item -ItemType Directory -Force -Path $Paths.DropRoot | Out-Null
  foreach ($candidate in @($Paths.DropRoot, $Paths.ExistingSourceRoot)) {
    $result = Test-Skate3Source $candidate
    if ($result.IsValid) {
      return $result
    }
  }

  throw "Put the Skate 3 files in $($Paths.DropRoot), then press Set Up Game."
}

function Invoke-Robocopy([string]$Source, [string]$Destination) {
  New-Item -ItemType Directory -Force -Path $Destination | Out-Null
  & robocopy.exe $Source $Destination /E /COPY:DAT /DCOPY:DAT /R:2 /W:1 /NFL /NDL /NP /NJH /NJS | Out-Null
  $code = $LASTEXITCODE
  if ($code -ge 8) {
    throw "Copy failed from $Source to $Destination. Robocopy exit code: $code"
  }
}

function Ensure-RuntimeDataRoute([string]$RuntimeRoot, [string]$AssetsDataRoot) {
  $runtimeDataRoot = Join-Path $RuntimeRoot "data"
  if (Test-Path -LiteralPath $runtimeDataRoot) {
    $item = Get-Item -LiteralPath $runtimeDataRoot -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
      $target = @($item.Target | Select-Object -First 1)[0]
      if ($target -and ([System.IO.Path]::GetFullPath($target) -ieq [System.IO.Path]::GetFullPath($AssetsDataRoot))) {
        return "linked"
      }
      Assert-UnderPath $runtimeDataRoot $RuntimeRoot "Runtime data link"
      Remove-Item -LiteralPath $runtimeDataRoot -Force
    } elseif ($item.PSIsContainer) {
      Invoke-Robocopy $AssetsDataRoot $runtimeDataRoot
      return "copied"
    } else {
      throw "Runtime data path exists but is not a folder: $runtimeDataRoot"
    }
  }

  try {
    New-Item -ItemType Junction -Path $runtimeDataRoot -Target $AssetsDataRoot | Out-Null
    return "linked"
  } catch {
    Invoke-Robocopy $AssetsDataRoot $runtimeDataRoot
    return "copied"
  }
}

function Copy-Skate3RuntimeAssets {
  param(
    [Parameter(Mandatory)][object]$SourceInfo,
    [Parameter(Mandatory)][string]$AssetsRoot,
    [Parameter(Mandatory)][string]$RuntimeRoot,
    [Parameter(Mandatory)][string]$ProjectRoot,
    [scriptblock]$StatusCallback
  )

  Assert-UnderPath $AssetsRoot (Join-Path $ProjectRoot "work") "Working assets folder"
  Assert-UnderPath $RuntimeRoot (Join-Path $ProjectRoot "work") "Runtime assets folder"

  if ($StatusCallback) { & $StatusCallback "Copying game files into the working folder..." }
  Invoke-Robocopy $SourceInfo.SourceRoot $AssetsRoot

  if ($StatusCallback) { & $StatusCallback "Preparing runtime files..." }
  New-Item -ItemType Directory -Force -Path $RuntimeRoot | Out-Null
  Copy-Item -LiteralPath $SourceInfo.RuntimeXex -Destination (Join-Path $RuntimeRoot "default.xex") -Force
  Copy-Item -LiteralPath $SourceInfo.RuntimeXex -Destination (Join-Path $RuntimeRoot "default.xex_uncrypted.xex") -Force

  $nxeart = Join-Path $SourceInfo.SourceRoot "nxeart"
  if (Test-Path -LiteralPath $nxeart -PathType Leaf) {
    Copy-Item -LiteralPath $nxeart -Destination (Join-Path $RuntimeRoot "nxeart") -Force
  }

  $assetsDataRoot = Join-Path $AssetsRoot "data"
  if (-not (Test-Path -LiteralPath $assetsDataRoot -PathType Container)) {
    throw "Working data folder was not created: $assetsDataRoot"
  }
  $dataMode = Ensure-RuntimeDataRoute $RuntimeRoot $assetsDataRoot

  $sourceRuntimeHash = Get-OptionalFileHash $SourceInfo.RuntimeXex
  $runtimeHash = Get-OptionalFileHash (Join-Path $RuntimeRoot "default.xex")
  if ($sourceRuntimeHash -ne $runtimeHash) {
    throw "Runtime default.xex does not match the decrypted XEX."
  }

  [pscustomobject]@{
    RuntimeRoot = $RuntimeRoot
    AssetsRoot = $AssetsRoot
    RuntimeXexHash = $runtimeHash
    DataMode = $dataMode
  }
}

function Invoke-Skate3Setup {
  param(
    [string]$SourceRoot = "",
    [string]$AssetsRoot = "",
    [string]$RuntimeRoot = "",
    [string]$UserRoot = "",
    [string]$CacheRoot = "",
    [string]$LauncherPath = "",
    [string]$ProjectRoot = "",
    [switch]$StartGame,
    [scriptblock]$StatusCallback
  )

  $paths = Get-Skate3SetupPaths
  if ([string]::IsNullOrWhiteSpace($ProjectRoot)) { $ProjectRoot = $paths.ProjectRoot }
  if ([string]::IsNullOrWhiteSpace($AssetsRoot)) { $AssetsRoot = $paths.AssetsRoot }
  if ([string]::IsNullOrWhiteSpace($RuntimeRoot)) { $RuntimeRoot = $paths.RuntimeRoot }
  if ([string]::IsNullOrWhiteSpace($UserRoot)) { $UserRoot = $paths.UserRoot }
  if ([string]::IsNullOrWhiteSpace($CacheRoot)) { $CacheRoot = $paths.CacheRoot }
  if ([string]::IsNullOrWhiteSpace($LauncherPath)) { $LauncherPath = $paths.LauncherPath }

  $sourceInfo = Find-Skate3SourceRoot $paths $SourceRoot
  if ($StatusCallback) { & $StatusCallback "Using game files from $($sourceInfo.SourceRoot)" }

  $copyResult = Copy-Skate3RuntimeAssets -SourceInfo $sourceInfo -AssetsRoot $AssetsRoot `
      -RuntimeRoot $RuntimeRoot -ProjectRoot $ProjectRoot -StatusCallback $StatusCallback

  New-Item -ItemType Directory -Force -Path $UserRoot, $CacheRoot | Out-Null

  if ($StatusCallback) { & $StatusCallback "Setup complete." }
  if ($StartGame) {
    if (-not (Test-Path -LiteralPath $LauncherPath -PathType Leaf)) {
      throw "Game launcher not found: $LauncherPath"
    }
    if ($StatusCallback) { & $StatusCallback "Starting Skate 3 Recomp..." }
    Start-Process -FilePath $LauncherPath | Out-Null
  }

  [pscustomobject]@{
    SourceRoot = $sourceInfo.SourceRoot
    AssetsRoot = $copyResult.AssetsRoot
    RuntimeRoot = $copyResult.RuntimeRoot
    RuntimeXexHash = $copyResult.RuntimeXexHash
    DataMode = $copyResult.DataMode
  }
}

function Invoke-SetupSelfTest {
  $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("skate3-setup-selftest-" + [guid]::NewGuid().ToString("N"))
  try {
    $source = Join-Path $tempRoot "Skate 3 Files"
    $assets = Join-Path $tempRoot "work\assets"
    $runtime = Join-Path $tempRoot "work\runtime-assets"
    $user = Join-Path $tempRoot "work\user-data"
    $cache = Join-Path $tempRoot "work\cache"
    New-Item -ItemType Directory -Force -Path (Join-Path $source "data\example") | Out-Null
    Set-Content -LiteralPath (Join-Path $source "default.xex") -Value "clean-xex" -NoNewline
    Set-Content -LiteralPath (Join-Path $source "default.xex_uncrypted.xex") -Value "runtime-xex" -NoNewline
    Set-Content -LiteralPath (Join-Path $source "nxeart") -Value "art" -NoNewline
    Set-Content -LiteralPath (Join-Path $source "data\example\file.bin") -Value "data" -NoNewline

    $result = Invoke-Skate3Setup -SourceRoot $source -AssetsRoot $assets -RuntimeRoot $runtime `
      -UserRoot $user -CacheRoot $cache -ProjectRoot $tempRoot

    if ((Get-Content -LiteralPath (Join-Path $runtime "default.xex") -Raw) -ne "runtime-xex") {
      throw "Runtime default.xex was not prepared from the decrypted XEX."
    }
    if ((Get-Content -LiteralPath (Join-Path $assets "default.xex") -Raw) -ne "clean-xex") {
      throw "Working copy did not preserve the source default.xex."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $runtime "data\example\file.bin") -PathType Leaf)) {
      throw "Runtime data route did not expose copied data."
    }
    if ([string]::IsNullOrWhiteSpace($result.RuntimeXexHash)) {
      throw "Setup did not report a runtime XEX hash."
    }
    "Setup self-test passed"
  } finally {
    if (Test-Path -LiteralPath $tempRoot) {
      Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
  }
}

function Start-Skate3SetupGui {
  Add-Type -AssemblyName System.Windows.Forms
  Add-Type -AssemblyName System.Drawing

  $paths = Get-Skate3SetupPaths
  New-Item -ItemType Directory -Force -Path $paths.DropRoot | Out-Null

  $form = New-Object System.Windows.Forms.Form
  $form.Text = "Skate 3 Recomp Setup"
  $form.StartPosition = "CenterScreen"
  $form.ClientSize = New-Object System.Drawing.Size(624, 392)
  $form.FormBorderStyle = "FixedDialog"
  $form.MaximizeBox = $false

  $title = New-Object System.Windows.Forms.Label
  $title.Text = "Skate 3 Recomp Setup"
  $title.Font = New-Object System.Drawing.Font("Segoe UI", 16, [System.Drawing.FontStyle]::Bold)
  $title.Location = New-Object System.Drawing.Point(28, 24)
  $title.Size = New-Object System.Drawing.Size(560, 34)
  $form.Controls.Add($title)

  $body = New-Object System.Windows.Forms.Label
  $body.Text = "Put your Skate 3 files in this folder, then press Set Up Game."
  $body.Font = New-Object System.Drawing.Font("Segoe UI", 9)
  $body.Location = New-Object System.Drawing.Point(30, 70)
  $body.Size = New-Object System.Drawing.Size(560, 24)
  $form.Controls.Add($body)

  $pathBox = New-Object System.Windows.Forms.TextBox
  $pathBox.Text = $paths.DropRoot
  $pathBox.ReadOnly = $true
  $pathBox.Location = New-Object System.Drawing.Point(32, 104)
  $pathBox.Size = New-Object System.Drawing.Size(452, 24)
  $form.Controls.Add($pathBox)

  $openFolder = New-Object System.Windows.Forms.Button
  $openFolder.Text = "Open Folder"
  $openFolder.Location = New-Object System.Drawing.Point(496, 102)
  $openFolder.Size = New-Object System.Drawing.Size(104, 28)
  $openFolder.Add_Click({ Start-Process -FilePath $paths.DropRoot | Out-Null })
  $form.Controls.Add($openFolder)

  $launchAfter = New-Object System.Windows.Forms.CheckBox
  $launchAfter.Text = "Start game when ready"
  $launchAfter.Checked = $true
  $launchAfter.Location = New-Object System.Drawing.Point(32, 148)
  $launchAfter.Size = New-Object System.Drawing.Size(220, 24)
  $form.Controls.Add($launchAfter)

  $setupButton = New-Object System.Windows.Forms.Button
  $setupButton.Text = "Set Up Game"
  $setupButton.Font = New-Object System.Drawing.Font("Segoe UI", 10, [System.Drawing.FontStyle]::Bold)
  $setupButton.Location = New-Object System.Drawing.Point(32, 190)
  $setupButton.Size = New-Object System.Drawing.Size(568, 46)
  $form.Controls.Add($setupButton)

  $progress = New-Object System.Windows.Forms.ProgressBar
  $progress.Style = "Marquee"
  $progress.MarqueeAnimationSpeed = 0
  $progress.Location = New-Object System.Drawing.Point(32, 256)
  $progress.Size = New-Object System.Drawing.Size(568, 18)
  $form.Controls.Add($progress)

  $statusBox = New-Object System.Windows.Forms.TextBox
  $statusBox.Multiline = $true
  $statusBox.ReadOnly = $true
  $statusBox.ScrollBars = "Vertical"
  $statusBox.Location = New-Object System.Drawing.Point(32, 294)
  $statusBox.Size = New-Object System.Drawing.Size(568, 74)
  $statusBox.Text = "Ready. Folder created if it was missing."
  $form.Controls.Add($statusBox)

  $worker = New-Object System.ComponentModel.BackgroundWorker
  $worker.WorkerReportsProgress = $true
  $worker.DoWork += {
    param($sender, $eventArgs)
    $result = Invoke-Skate3Setup -StartGame:$false -StatusCallback {
      param($message)
      $sender.ReportProgress(0, $message)
    }
    $eventArgs.Result = $result
  }
  $worker.ProgressChanged += {
    param($sender, $eventArgs)
    $statusBox.AppendText([Environment]::NewLine + [string]$eventArgs.UserState)
  }
  $worker.RunWorkerCompleted += {
    param($sender, $eventArgs)
    $progress.MarqueeAnimationSpeed = 0
    $setupButton.Enabled = $true
    $openFolder.Enabled = $true
    $launchAfter.Enabled = $true
    if ($eventArgs.Error) {
      $statusBox.AppendText([Environment]::NewLine + "Setup failed: " + $eventArgs.Error.Message)
      [System.Windows.Forms.MessageBox]::Show($form, $eventArgs.Error.Message, "Setup failed", "OK", "Error") | Out-Null
      return
    }
    $statusBox.AppendText([Environment]::NewLine + "Done.")
    if ($launchAfter.Checked) {
      Start-Process -FilePath $paths.LauncherPath | Out-Null
    }
  }

  $setupButton.Add_Click({
    $setupButton.Enabled = $false
    $openFolder.Enabled = $false
    $launchAfter.Enabled = $false
    $progress.MarqueeAnimationSpeed = 30
    $statusBox.Text = "Starting setup..."
    $worker.RunWorkerAsync()
  })

  [void]$form.ShowDialog()
}

if ($SelfTest) {
  Invoke-SetupSelfTest
  exit 0
}

if ($SetupOnly) {
  Invoke-Skate3Setup -SourceRoot $SourceRoot -StartGame:$false | Format-List
  exit 0
}

Start-Skate3SetupGui
