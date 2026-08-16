# Preservation workspace

`reference.sha256` is the immutable baseline for the supplied DOS installation
and Lynx cartridge image. `verify_reference.py` fails on missing, added, or
changed files.

Reverse engineering must be evidence-driven. Notes should identify the exact
binary hash, code/data address, observation method, and confidence. Decompiled
or disassembled material remains private and is not copied into the clean game
implementation.

Run `make decompile` to clone the pinned MIT-licensed `depklite` tool into a
temporary directory, decrypt/unpack the PKLITE image, create a 16-bit Ghidra
project, and export pseudocode under the ignored `preservation/private/`
directory. The original reference file is never modified.

## Current inventory findings

- DOS VGA executable: PKLITE-compressed 16-bit MZ executable.
- Gameplay: 1600 x 160 level, 320 x 160 viewport, 320 x 40 HUD.
- Levels: 80 unique layouts plus 80 odd-table records arranged as 120 levels.
- Graphics: five standard terrain/object sets and four 960 x 160 special maps.
- Audio: one 22,125-byte decompressed AdLib sound image with 21 music tracks;
  the executable's rotation and special override tables are documented in
  `music.md`.
- Lynx image: 256 KiB headerless cartridge image; used as a control reference.
