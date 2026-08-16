# Port architecture and fidelity status

The port is a clean, modern C implementation. The original executable is used
as private behavioral evidence, not translated or linked into the Playdate
program.

## Runtime layers

1. `tools.assetc` verifies and decodes the DOS archives into `LPD1` version 6.
2. Each unique level's Bayer 2x2, clustered 2x2, Bayer 4x4, solid, and steel
   1600 x 160 bitplanes use deterministic PackBits compression. `lp_pack`
   decompresses the selected level through a bounded 1 KB streaming reader
   into the same fixed planes used by gameplay. Objects, original animations,
   terrain-edit masks, and the executable's exact 51-frame explosion-particle
   table remain directly addressable in the pack; identical encoded sprite
   sequences share atlas storage.
3. `lp_game` advances at the DOS-compatible 17 Hz rate. It owns release timing,
   lemming states, all eight skills, blocker interactions, steel/one-way rules,
   triggers, win/loss state, and terrain mutation.
4. `src/main.c` maps the portable state to Playdate input, framebuffer, menus,
   saves, and the callback-driven AdLib synthesizer.

The package contains all 120 one-player levels in Fun, Tricky, Taxing, and
Mayhem order. Two-player content is intentionally out of scope.

## Display policy

The 1600 x 160 DOS terrain is converted once with world-anchored Bayer
patterns, so scrolling cannot cause dither shimmer. Lemmings retain their exact
source dimensions, masks, and pixel positions and render every opaque source
pixel as black, keeping pale moving limbs visible against the white sky.
Animated objects use a stable luminance threshold instead of dithering. Normal
gameplay uses an
exact 2x detail view in which every source pixel becomes a uniform 2 x 2 block
and the cursor-following crop shows 200 x 104 world pixels below a fixed
32-pixel status panel. The panel reports time, released/total count,
rescued/required progress, and the active skill inventory or paused state.
During gameplay the crank adds one second of simulation per revolution, capped
at 4x total speed; in the paused action panel it adjusts release rate. The
panel exposes one two-cell release-rate editor, eight skills, pause, and nuke
in a 6 x 2 grid. A enters release-rate editing, pressing or holding Up/Down
changes it, and A/B accepts/cancels. The eight skill cells use clear,
undithered original DOS sprite
poses and only the selected skill animates. Exact native Atari Lynx crops supply
the nuke art at integer 2x; it and Pause stay static.
Entering the panel explicitly pauses simulation and discards menu wall time,
so closing it cannot trigger a catch-up burst. A selects and B returns from the
panel; on the gameplay screen, the A+B chord toggles persistent pause directly.
Horizontal camera movement is limited to visible terrain/object bounds plus 24
source pixels. Gameplay remains fixed at the exact 2x view. The
title-screen-only `Dither` menu selects Solid, world-anchored Bayer 2x2,
Cluster 2x2, or Bayer 4x4 for terrain and special-level backgrounds before
play. Lemmings and animated objects remain unchanged. The view does not draw a
gameplay legend over the world.
The system menu provides direct `Choose Level` and `Reset Level` commands only
while a level is active. The always-available `Game` control provides Continue
and Nuke.
Menus and results use the native 400 x 240 display rather than emulating the
DOS menu resolution. Startup shows the complete logo followed by a native
difficulty and level carousel. The title credits DMA Design and the DOS port
for Playdate, and B opens detailed original DOS credits. Completing Mayhem's
final level shows the original DMA Design master-player salute. All four ratings are
independently available;
within each one, completion bits unlock the first incomplete level while every
completed level remains replayable. The browser initially focuses the saved
last-played rating and its current level, and also shows a compact terrain
preview plus the original rescue, time, and skill requirements.

Falling evenly distributes eight three-pixel and thirty-two two-pixel steps
across 40 ticks, averaging 2.2 source pixels per tick. This stays below the DOS
three-pixel step that appeared excessive in the default 2x view, and the speed
is tuned through `LP_FALL_STEP`, `LP_FALL_PHASE`, and `LP_FALL_EXTRA` in
`lp_game.h`. Falling animation cadence is paced against it. Fall distance
counts every source pixel travelled, landing step included, so the fatal
distance in `LP_FATAL_FALL_PIXELS` stays a real distance when the speed
changes: a 62-pixel drop is survivable and a 63-pixel drop is fatal.

A level opens centred on its entrance hatch. The DOS start scroll assumes a
320-pixel viewport and leaves the hatch out of frame on 13 of the 120 levels in
the 200-wide 2x detail view, so it does not place the opening view. Levels whose
hatch sits near a world edge keep the hatch on screen but off centre, because
the camera stays inside the level.

The first lemming is released a fixed `LP_FIRST_RELEASE_TICKS` after the level
starts. Only the gap between later lemmings follows the level's release rate,
so a slow level does not wait an entire release interval for its first lemming.

The visible level clock remains at its initial value during the spoken intro
and entrance-hatch opening. It begins on the first lemming release, alongside
the level music, so setup animation does not consume playable time.

## Audio policy

The authoritative `ADLIB.DAT` is decompressed to its 22,125-byte Sound Images
memory image during asset compilation. A clean C port of the command interpreter
drives the pinned DOSBox DBOPL core in nine-channel OPL2 mode. The reference and
Simulator paths synthesize at 49,716 Hz. On physical Playdate, DBOPL synthesizes
at 22,050 Hz above the final 8.5 kHz passband, then uses deterministic 2x linear
interpolation for the 44.1 kHz callback. A fractional scheduler preserves the
approved 49,716 Hz tick durations: Fun 1 differs by only two output frames over
its complete 59.04-second cycle. Two gentle fixed-point low-pass poles run after
rate conversion. Songs repeat through their
original control flow, including independently looping voices, without stored
PCM seams. The main update loop renders ahead toward a 6,144-frame (139 ms)
high-water mark in a lock-free 8,192-frame ring. The SDK audio thread only
copies published samples, so it performs no synthesis, allocation, or file I/O;
atomic producer/consumer indices make track changes and underrun zero-fills
safe. Gameplay emits semantic events into a bounded queue. Fifteen short IMA
ADPCM effects are preloaded and played through a four-voice pool, eliminating
trigger-time file I/O. Action effects retain the DOS AdLib character, while the
three iconic spoken lines use original Amiga samples. Per-event gains
compensate for the captures' different mastering levels, so voices and
transient effects no longer overpower the live OPL mix.

DBOPL is copyright the DOSBox Team and licensed under GPL-2.0-or-later. The
pinned adapted source and complete license are retained in `third_party/dbopl`,
and the license is copied into PDX builds. Public binary distribution must make
the corresponding engine source and build instructions available under
GPL-compatible terms. The copyrighted DOS data is separate, remains
user-supplied, and is not covered by that license.

The Python interpreter and JavaScript DBOPL player remain independent research
oracles. All 39 clean-interpreter register streams were verified byte-for-byte
against the independent player for 10,000 timer steps. A one-second Fun 1 native
render is also pinned PCM-exactly against that oracle.

## Validation and remaining fidelity work

Automated checks cover archive checksums, decompression failures, known section
sizes, canonical level ordering, pack structure, all-level loading and 300-tick
smoke simulations, sprite/mask loading, spawning, movement, all eight skills,
steel preservation, traps, object animation, sound-event transitions, nuke and win conditions, host
compilation, and both Playdate compiler targets.

Bombers display a five-to-one countdown over five real simulation seconds.
Nuking follows DOS slot order and arms one lemming per tick with a complete
fuse. The Oh-no pose, static burst frame, 51 frames of 80 signed particle
positions, terrain mask, sound transition, and delayed removal are preserved.

The port is playable, but “DOS-exact” remains a validation target rather than a
claim. Original object animation frames, masks, and trap activation are now
implemented. Named gameplay sound transitions are implemented; exact
frame-level comparison against a golden DOS replay remains future work. The
private Ghidra workspace is intended to close those gaps without contaminating
the clean C implementation. Physical-device profiling also remains necessary.
