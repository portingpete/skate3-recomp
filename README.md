# Skate 3 Recomp

Skate 3 Recomp is an early Windows build that brings Skate 3 toward a native PC
runtime with a simple setup flow, PC settings, keyboard/mouse support, diagnostics,
and 1080p/60fps+ experimentation.  It is in-game playable.

This is still work in progress. Expect bugs, missing polish, and occasional
breakage while the project is being brought up.

This repository does not include Skate 3 game files, decrypted executables,
generated game output, caches, saves, or captures. You must provide your own
legally obtained Xbox 360 game dump.

If you want to support the work, donations are welcome:
[ko-fi.com/portingpete](https://ko-fi.com/portingpete)

## Easy Setup

Use this flow if you downloaded the Windows zip from
[GitHub Releases](https://github.com/portingpete/skate3-recomp/releases/latest).

1. Download `Skate3Recomp-Windows-v0.1.1-alpha.zip` from the latest Release.
2. Extract the zip somewhere easy to find.
3. Open the folder named `Skate 3 Files`.
4. Put your Skate 3 dump in that folder.
5. Double-click `Setup Skate 3 Recomp.cmd`.
6. Press `Set Up Game`.

Leave `Start game when ready` checked if you want the setup tool to launch the
game automatically when it finishes.

## What Goes In `Skate 3 Files`

The setup tool looks for these files and folders:

- `default.xex`
- `default.xex_uncrypted.xex`
- `data`
- `nxeart`

`default.xex_uncrypted.xex` is the decrypted game executable. If it is missing,
setup will stop and ask you to add it.

## What The Setup Button Does

The setup tool keeps your original dump untouched. It copies the files into a
working folder, prepares the runtime folder used by the launcher, creates the
needed save/cache folders, and then starts the normal game launcher if the
auto-start box is checked.

After setup, use:

```text
Launch Skate 3 Recomp.cmd
```

That launcher opens the PC settings screen first, so you can choose resolution,
fullscreen, FPS counter, and other options before the game starts.

## If Setup Fails

Check the `Skate 3 Files` folder first. Most setup problems are caused by one of
these:

- The files were placed inside an extra nested folder.
- `data` is missing.
- `default.xex_uncrypted.xex` is missing.
- The dump is incomplete.

For developer/source-build setup notes, see [SKATE3_SETUP.md](SKATE3_SETUP.md).
For attribution covering Skate 3 Recomp and its dependencies, see
[CREDITS.md](CREDITS.md).

## Current Status

- The normal launcher shows PC settings before the game starts.
- The game has an optional readable FPS counter.
- The setup GUI prepares the working/runtime folders automatically.
- Gameplay bring-up is still under active investigation, including known physics
  and collision issues.

## Disclaimer

Skate 3 Recomp is not affiliated with nor endorsed by Electronic Arts, Microsoft,
Xbox, or the upstream ReXGlue project. All trademarks and copyrights belong to
their respective owners.

This project is not intended to promote piracy or unauthorized use of copyrighted
material. Do not ask this project for game files.

## About ReXGlue

<p align="center">
  <a href="https://github.com/rexglue/rexglue-sdk">
    <img src="https://github.com/rexglue/rexglue-media/blob/main/ReX_Banner.png" alt="ReXGlue banner">
  </a>
</p>

Skate 3 Recomp is built from a custom ReXGlue branch. It is not an official
upstream ReXGlue release.

ReXGlue converts Xbox 360 PowerPC code into portable C++ that runs natively on
modern platforms. It is heavily rooted in the foundations of
[Xenia](https://github.com/xenia-project), and its ahead-of-time C++ generation
approach is inspired by [XenonRecomp](https://github.com/hedge-dev/XenonRecomp)
and [rexdex's recompiler](https://github.com/rexdex/recompiler).

Upstream ReXGlue links:

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)
- [ReXGlue releases](https://github.com/rexglue/rexglue-sdk/releases)
- [ReXGlue Discord](https://discord.gg/CNTxwSNZfT)

## Credits

### ReXGlue

- [Tom (crack)](https://github.com/tomcl7) - Project Founder
- [Loreaxe](https://github.com/Loreaxe) - Linux Contributor
- [mystixor](https://github.com/Mystixor) - Windows Contributor
- [Graine25](https://github.com/Graine25) - Project Support
- [Carlos Estrague (mrcmunir)](https://github.com/mrcmunir) - Linux / ARM64 Contributor
- [sanjay900](https://github.com/sanjay900) - Linux / SDL Contributor
- [Toby](https://github.com/TbyDtch) - Project Support
- [Roxxsen](https://github.com/Roxxsen) - CI/CD Contributor

The list above is not exhaustive. Thanks to everyone in the ReXGlue community who
contributes code, reports issues, tests builds, and keeps the project moving.

### Special Thanks

- [Project Xenia](https://github.com/xenia-project/xenia/tree/master/src/xenia) -
  their Xbox 360 emulation work laid the groundwork for ReXGlue.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) - for pioneering the
  modern static recompilation approach for Xbox 360.
- [rexdex's recompiler](https://github.com/rexdex/recompiler) - the original
  static recompiler for Xbox 360.
- The Xbox 360 homebrew and modding communities whose research makes projects
  like this possible.
