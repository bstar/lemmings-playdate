# Licensing

This project combines code under two licenses. The short version: the code
written for this project is MIT, but a binary built from it is GPL, because of
the audio backend it links.

## This project's code

The portable engine, Playdate adapter, conversion tools, and documentation are
copyright 2026 GreenTree Industries and licensed under the MIT License. See
[LICENSE](LICENSE).

## DBOPL

`third_party/dbopl/` is derived from DOSBox, copyright the DOSBox Team, and is
licensed under the GNU General Public License, version 2 or any later version.
The full text is at [`third_party/dbopl/COPYING`](third_party/dbopl/COPYING).

DBOPL is compiled into every Playdate build to synthesize the AdLib score. MIT
code may be combined into a GPL work, so the combination is permitted, but the
result is governed by the GPL.

**Anyone distributing a built package must do so under GPL-2.0-or-later, and
must make the corresponding source and build instructions available.** That
obligation attaches to the binary, not to this repository's own sources, which
you may reuse under the MIT terms above.

If you want a build free of that obligation, replace the DBOPL backend with a
permissively licensed OPL2 emulator. The engine reaches it through the small
wrapper in `playdate/core/lp_dbopl.cpp`.

## HUD font

The 8 x 8 bitmap glyphs embedded in `playdate/src/hud_font.h` come from the
Battle Garegga Type 2 font in Idleberg's Playdate Arcade Fonts collection,
which dedicates its fonts to the public domain under CC0 1.0. No attribution is
required; it is recorded here because the glyph data ships in every build.

## Original game material

Original game data, artwork, levels, music, sound recordings, names, and other
third-party materials are not relicensed here, and none of them are included in
this repository. It contains no game data, no converted assets, no sound
recordings, and no artwork derived from the original game. Building a playable
package requires supplying your own copy of the original data, as described in
the README.

Lemmings is a trademark of its respective owner. This project is not affiliated
with, endorsed by, or sponsored by that owner.
