Skate 3 Recomp Windows Setup

This zip does not include Skate 3 game files.

Quick start:

1. Open the folder named "Skate 3 Files".
2. Put your Skate 3 dump in that folder.
3. Double-click "Setup Skate 3 Recomp.cmd".
4. Press "Set Up Game".

The setup button looks for:

- default.xex
- default.xex_uncrypted.xex
- data
- nxeart

default.xex_uncrypted.xex is required unless default.xex is already decrypted.

Decrypting default.xex with XexTool:

Use this only with your own legally obtained Skate 3 dump. Keep the original
default.xex in place and create a separate decrypted copy:

  xextool -e d -c b -o default.xex_uncrypted.xex default.xex

After running the command, "Skate 3 Files" should contain both default.xex and
default.xex_uncrypted.xex, plus the data folder.

After setup, use "Launch Skate 3 Recomp.cmd" to play. The launcher shows the PC
settings screen before the game starts.

Current build notes:

- Experimental ultrawide Hor+ support is available for testing. It is still
  experimental.
- The windowed-mode startup crash/launch stall is fixed in current builds.

If setup fails, check that the files were not placed inside an extra nested
folder and that default.xex_uncrypted.xex is present.
