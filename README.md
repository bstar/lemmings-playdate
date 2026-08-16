# Lemmings preservation and Playdate port

A clean-room engine and asset converter that run the original DOS levels on the
Playdate. This repository contains only the new implementation:

- `preservation/` records hashes, provenance, and reverse-engineering notes.
- `tools/assetc/` converts the DOS data into a deterministic `LPD1` pack.
- `playdate/` contains the portable C game core and Playdate adapter, plus the
  small C++ wrapper required by the DBOPL music backend.
- `playdate/core/lp_adlib.c` synthesizes the DOS AdLib score in real time;
  `tools/audio/` retains an independent offline reference renderer.

## What is not included

No original game material ships here. You must supply your own copy of the
following before a playable package can be built:

- **Game data.** Place your DOS Lemmings files in `reference/lemming1.pc/`.
  `preservation/reference.sha256` records the SHA-256 values the tools were
  developed against, and `make verify-reference` checks a set against it.
- **Sound effects.** `make effects EFFECTS_ARCHIVE=/path/to/archive` produces
  the 15 runtime effects into `playdate/effects/`. Without them the Playdate
  targets will not stage.
- **Launcher artwork.** `playdate/Source/SystemAssets/` needs `card.png`,
  `icon.png`, `launchImage.png`, and `logo.png` at the sizes the Playdate SDK
  requires. Supply your own; the originals are not redistributable.
- **Package identity.** Copy `playdate/Source/pdxinfo.example` to
  `playdate/Source/pdxinfo` and set your own author and bundle ID. `make
  release` advances the version and build number in that file.

Generated output stays under the ignored `generated/` tree.

## Status

In development, and playable. All 120 original levels run with saved progress,
the full skill set, and real-time AdLib music synthesis. Device audio and
edge-case gameplay behavior are still being refined.

- [docs/PORT.md](docs/PORT.md) covers the runtime design and explicit fidelity status
- [docs/VALIDATION.md](docs/VALIDATION.md) records what the current build was validated against
- [CHANGELOG.md](CHANGELOG.md) lists what changed in each release
- [CONTRIBUTING.md](CONTRIBUTING.md) covers building, testing, and submitting changes
- [LICENSING.md](LICENSING.md) explains the terms and how the two licenses combine

## Quick start

On NixOS or another host with Nix, enter the pinned development environment
first. It supplies Clang, Python with Pillow, ffmpeg, Node, zip, and the ARM
bare-metal compiler used by the Playdate device build, then fetches the pinned
Playdate SDK into the ignored `build/` tree:

```sh
nix develop
make playdate-sdk
```

`nix develop .#decompile` adds Ghidra, a JDK, and `ndisasm` for `make decompile`.
The Playdate SDK ships prebuilt binaries; running them on NixOS relies on
`programs.nix-ld` being enabled, which the shell's library path complements.

Inside that shell `PLAYDATE_SDK_PATH` already points at the managed SDK, so the
`PLAYDATE_SDK_PATH=` arguments below can be omitted. Setting it to another
location uses that SDK as-is instead of downloading one.

```sh
make verify-reference
make assets
make test
make host
make decompile
make audio REFERENCE_PLAYER=/path/to/local/lemmings.js
make effects EFFECTS_ARCHIVE=/path/to/archive
make playdate PLAYDATE_SDK_PATH=/path/to/PlaydateSDK
make simulator PLAYDATE_SDK_PATH=/path/to/PlaydateSDK
make playdate-run
make release PLAYDATE_SDK_PATH=/path/to/PlaydateSDK
```

`make playdate-run` builds the Simulator target and launches it on the
resulting `generated/Lemmings.pdx`.

The Playdate target uses the C SDK. The host build exists for deterministic
simulation tests and faster debugging. Device builds require a complete Arm
GNU Toolchain with newlib and C++ support; when it is not first on `PATH`, pass
`ARM_TOOLCHAIN_PATH=/path/to/arm-none-eabi/bin` to `make playdate` or
`make release`.

`make release` increments both the semantic patch version and the required
sideload `buildNumber`. It writes `generated/Lemmings.pdx` and an upload-ready
`generated/Lemmings.pdx.zip`; the first release is version 0.0.1. The unzipped
package can also be uploaded directly with the Simulator's Device menu.

Launcher and store artwork is read from `playdate/Source/SystemAssets` and is
not included here. Supply a card, icon, and launch image at the dimensions the
SDK requires.

## Playdate controls

- D-pad moves the mouse-style world cursor; pushing against an edge scrolls.
- A selects the lemming under the cursor and assigns the active skill. A
  two-pixel targeting box appears when that assignment is valid.
- B opens the paused 6 x 2 action panel. It contains one two-cell-wide release
  rate control, all eight skills, persistent pause/resume, and `NUKE ALL`.
  Press A on release rate, press or hold Up/Down to adjust it, then press A to
  accept or B to cancel. Elsewhere A confirms, B goes back, and Nuke requires
  a second A press. During gameplay, pressing A+B toggles pause directly.
- In the action panel, the crank changes release rate. The eight skill cells
  use clear, undithered original DOS sprite poses and only the selected skill
  animates. The supplied Lynx nuke art remains static at exact 2x, as does
  Pause.
- Gameplay defaults to an exact 2x detail view showing a cursor-following
  200 x 104 crop below the fixed status panel. Turning the crank fast-forwards
  gameplay; one revolution adds one second of simulation, capped at 4x total
  speed. Horizontal scrolling stops 24 source pixels beyond each level's
  visible terrain and objects.
- A level begins with “Let's Go!”, followed by the entrance hatch opening;
  music starts when the first lemming is released and begins falling.
- The system menu is contextual. `Dither` appears only on the title screen and
  selects Solid, Bayer 2, Cluster 2, or Bayer 4 for terrain, entrances, and
  exits. Direct `Choose Level` and `Reset Level` commands appear only during an
  active level. `Game` provides Continue and Nuke. Gameplay stays at exact 2x;
  lemmings, traps, and other objects remain crisp.

The native startup flow shows the complete Lemmings logo, credits DMA Design
and the DOS port for Playdate, and provides a B-accessible detailed DOS credits
screen. It then opens a four-rating level browser. Up/down selects Fun, Tricky,
Taxing, or Mayhem; left/right or the crank selects a level; A starts and B
returns to the logo. Each card includes a terrain preview, rescue requirements,
time, and completion status. All four ratings are available immediately. Within each rating,
completed levels and the first incomplete level can be played. Completion state
and the last-played rating are persisted in the existing `LPS1` `save.dat`.

Completing the final Mayhem level displays the original DMA Design salute for
master Lemmings players.

The 15 runtime effects are not included. `make effects
EFFECTS_ARCHIVE=/path/to/archive` imports them into `playdate/effects`, where
the Playdate targets stage them from. The importer verifies the archive it
accepts by SHA-256.

## Asset and code boundaries

`reference/`, `generated/`, `preservation/private/`, imported effects, launcher
artwork, and SDK staging directories under `playdate/Source/` are all ignored.
They hold owner-supplied game data, material derived from it, generated output,
and private decompiler output, none of which is redistributable.

The portable core and converter are kept separate from that material so the
provenance and licensing boundaries stay explicit, and so this repository can
be published without any of it.

DBOPL is derived from DOSBox and licensed under GPL-2.0-or-later. A distributed
binary using this backend must be accompanied by the corresponding source and
build instructions under GPL-compatible terms. See [LICENSING.md](LICENSING.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The one unusual rule is that game data
and anything derived from it must never be committed, because removing it later
means rewriting history. [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) applies to
project spaces.

## License

This project's code is MIT. See [LICENSE](LICENSE). It links DBOPL, which is
GPL-2.0-or-later, so a built package must be distributed under the GPL.
[LICENSING.md](LICENSING.md) explains how the two combine and what that means
for you.

Lemmings is a trademark of its respective owner. This project is not
affiliated with, endorsed by, or sponsored by that owner, and redistributes no
part of the original game.
