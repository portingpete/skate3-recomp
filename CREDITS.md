# Credits And Attribution

Skate 3 Recomp is a custom build of ReXGlue for local Skate 3 bring-up work. It
builds on ReXGlue and a large body of Xbox 360 emulation, recompilation,
graphics, audio, and reverse-engineering work.

## ReXGlue

Core ReXGlue credit belongs to the upstream ReXGlue project and contributors.
The upstream README currently credits:

- Tom Clay / Tom (crack): project founder.
- Loreaxe: Linux contributor.
- Mystixor: Windows contributor.
- Graine25: project support.
- Carlos Estrague / mrcmunir: Linux / ARM64 contributor.
- sanjay900: Linux / SDL contributor.
- Toby: project support.
- Roxxsen: CI/CD contributor.
- Everyone in the ReXGlue community who contributes code, issues, testing, and
  project support.

## Foundation And Research Projects

- [Xenia](https://github.com/xenia-project/xenia): Xbox 360 emulator research
  and implementation that ReXGlue derives portions of code and design from.
  The repository license also credits Ben Vanik and Xenia project contributors.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp): modern Xbox 360
  static recompilation influence.
- [rexdex/recompiler](https://github.com/rexdex/recompiler): earlier Xbox 360
  static recompilation influence.
- Xbox 360 homebrew, modding, and reverse-engineering communities whose public
  research made this kind of work possible.

## Skate 3

Skate 3 is owned by its respective rights holders. Skate 3, EA, Electronic Arts,
Black Box, Xbox, and Microsoft names and marks belong to their owners. This
repository is not affiliated with, sponsored by, or endorsed by those owners.

No Skate 3 game assets are included in this repository.

## Third-Party Source And Library Dependencies

This repository uses or vendors third-party projects through submodules or local
third-party source. See each dependency's own license files for exact terms.

- libmspack
- glslang
- FFmpeg
- tomlplusplus
- SIMDe
- xxHash
- spdlog
- fmt
- Catch2
- Snappy
- utfcpp
- volk
- Vulkan-Headers
- Vulkan Memory Allocator
- Dear ImGui
- SPIRV-Tools
- SPIRV-Headers
- CLI11
- o1heap
- SDL
- inja
- Tracy
- DirectX Shader Compiler / DXC
- RenderDoc-related integration files
- tiny-aes-c
- picosha2
- aes_128
- disasm
- disruptorplus
- dxbc
- PE parsing utilities
- ReXGlue's local `crypto` and `ffmpeg-overlay` third-party support code

## Tools Used Locally

- Git and GitHub CLI for repository management.
- CMake, Ninja, Visual Studio, and LLVM/Clang for builds.
- Ghidra and IDA Pro are available in the local reverse-engineering workflow.
- Local automation posted keyboard window messages to drive the generated app.

## Skate 3 Recomp Work

Skate 3 Recomp's local Skate 3 bring-up notes include runtime/input/diagnostic work,
keyboard-message automation notes, and setup guidance for users who provide
their own legally obtained game dump.
