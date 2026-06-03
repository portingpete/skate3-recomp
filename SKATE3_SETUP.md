# Skate 3 Recomp Setup Notes

Skate 3 Recomp is a custom build of ReXGlue for local Skate 3 bring-up,
runtime diagnostics, and 1080p/120fps experimentation. It is not an official
upstream ReXGlue release.

This repository does not contain Skate 3 assets, decrypted executables,
generated game code, runtime caches, captures, or any other copyrighted game
data.

Use only game files you own and are legally allowed to dump. Do not ask this
project for game files.

## Prerequisites

- Windows x64 development machine.
- Git with submodule support.
- CMake 3.25 or newer.
- Ninja Multi-Config.
- Visual Studio with the C++ Clang/LLVM toolchain available in the developer shell.
- A legally obtained Skate 3 Xbox 360 dump containing `default.xex` and a
  decrypted `default.xex_uncrypted.xex`.

The local bring-up that produced these notes used a Visual Studio
developer shell, the `win-amd64-relwithdebinfo` ReXGlue preset, and a working
directory outside the source tree.

## Clone And Build ReXGlue

```powershell
git clone --recurse-submodules https://github.com/portingpete/skate3-recomp.git
Set-Location .\skate3-recomp
$repoRoot = (Get-Location).Path

cmake --preset win-amd64
cmake --build --preset win-amd64-relwithdebinfo --target install --parallel 8
```

The installed SDK will be under:

```text
out\install\win-amd64
```

## Prepare Local Skate 3 Assets

Keep clean game inputs separate from generated output. One workable layout is:

```text
K:\Skate3\clean-dump       # read-only source dump you own
K:\Skate3\work\assets      # copied working assets
K:\Skate3\work\skate3-rex  # generated ReXGlue project
```

Copy your dump into the working asset folder:

```powershell
New-Item -ItemType Directory -Force K:\Skate3\work\assets
robocopy K:\Skate3\clean-dump K:\Skate3\work\assets /E
```

Preserve the original dump. Do not edit clean binaries or packages in place.

## Generate The Skate 3 Project

From the ReXGlue source checkout:

```powershell
$repoRoot = (Get-Location).Path
$rexglue = Join-Path $repoRoot "out\install\win-amd64\bin\rexglue.exe"

& $rexglue init `
  --project-name Skate3 `
  --xex-path K:\Skate3\work\assets\default.xex_uncrypted.xex `
  --game-root K:\Skate3\work\assets `
  --project-root K:\Skate3\work\skate3-rex

& $rexglue codegen K:\Skate3\work\skate3-rex\skate3_manifest.toml
```

## Known Local Timing Patch

The current local Skate 3 bring-up uses a generated-code timing patch. After
codegen, patch:

```text
K:\Skate3\work\skate3-rex\generated\default.xex_uncrypted\skate3_recomp.153.cpp
```

In function `sub_82B61EE0`, at guest address `0x82B61F04`, change the wait
timeout literal from:

```text
0xFFFB6C20   # -300000 100ns ticks, about 30ms
```

to:

```text
0xFFFD74F5   # -166667 100ns ticks, about 16.667ms
```

This is a local bring-up patch, not a general ReXGlue feature. Regenerating code
can overwrite it.

## Build The Generated Project

Configure the generated project with the ReXGlue install prefix:

```powershell
Set-Location K:\Skate3\work\skate3-rex
$env:CMAKE_PREFIX_PATH = "$repoRoot\out\install\win-amd64"

cmake --preset win-amd64
cmake --build --preset win-amd64-relwithdebinfo --target skate3 --parallel 8
```

Copy the current runtime DLL next to the generated executable if needed:

```powershell
Copy-Item `
  "$repoRoot\out\install\win-amd64\bin\rexruntimerd.dll" `
  K:\Skate3\work\skate3-rex\out\build\win-amd64-relwithdebinfo\rexruntimerd.dll `
  -Force
```

## Run Settings Used Locally

The local 1080p/120Hz output path used:

```powershell
.\out\build\win-amd64-relwithdebinfo\skate3.exe `
  --game-data-root K:\Skate3\work\assets `
  --user-data-root K:\Skate3\work\user-data `
  --cache-root K:\Skate3\work\cache `
  --resolution 1080p `
  --video-mode-width 1920 `
  --video-mode-height 1080 `
  --video-mode-refresh-rate 120 `
  --window-width 1920 `
  --window-height 1080 `
  --mnk-mode `
  --keybind-start P
```

For automation, posted keyboard window messages worked better than controller
button injection in this local desktop session. Useful bindings were:

- `P`: Start, when launched with `--keybind-start P`.
- `Space`: A.
- `E`: Y.
- `W`: left stick up.

## Experimental Ultrawide Notes

Current development builds include experimental ultrawide Hor+ work for Skate 3.
The intended behavior is:

- Gameplay keeps the Hor+ ultrawide path active.
- Startup, menus, and videos use a separate non-stretched presentation path.
- Windowed-mode startup/launch crash behavior has been fixed in current builds.

Treat ultrawide Hor+ as experimental while gameplay and visual coverage continue
to expand.

## Verification Notes

Known local evidence recorded outside this repository:

- Actual gameplay/rendering proof was the `1080p60-dispatchdiag` run, which
  reached the Skate School tutorial scene with a 1920x1080 host client.
- Intro/video captures are not proof of gameplay rendering.
- Some short smoke runs only prove rendered menus, not gameplay.

When testing changes, prefer:

1. A clean source diff check.
2. A ReXGlue runtime rebuild.
3. A generated-app launch with keyboard-message input.
4. A visual capture of an actual rendered menu or gameplay scene.
5. Perf CSV review over the relevant frame window.
