# Validation record

Validated on 2026-08-09 with Playdate SDK 3.1.1, Arm GNU Toolchain 15.2.Rel1,
and Clang 21.1.8 for the host and Simulator targets, built from the pinned Nix
development environment.

## Automated gates

- `make verify-reference`: all 71 supplied preservation inputs match the
  recorded SHA-256 manifest.
- `make test`: archive, level ordering, v6 pack, clean AdLib interpreters,
  DBOPL synthesis, and portable game-core tests pass under warnings-as-errors.
- PackBits tests cover literal/repeated-run boundaries, malformed streams, and
  byte-exact round trips. The native streaming loader decompresses and simulates
  all 120 levels through the same bounded path used by Playdate.
- Sprite conversion tests prove opacity-mask preservation, black lemming
  silhouettes, identical-frame atlas deduplication, and palette-aware
  ordered-dither planes for entrances/exits.
- Terrain conversion tests verify complete dispersed and clustered 2x2 plus
  Bayer 4x4 threshold rankings; core tests verify terrain edits propagate to
  every plane.
- Core selection tests verify the inclusive original-size targeting footprint
  and assignment eligibility used by the gameplay hover box.
- Falling tests pin the fatal distance to real travelled pixels, asserting that
  a 62-pixel drop through an action-to-fall transition survives and a 63-pixel
  drop splats, and that such a transition starts a fall at zero rather than
  crediting pixels never fallen. The falling cadence assertions are stated
  against the speed constants, so retuning the speed cannot silently move the
  distance bookkeeping.
- `make host`: all 120 levels load and run a 300-tick smoke simulation while
  checking release/alive invariants.
- The clean Sound Images interpreter matches the independent register oracle
  for all 21 music and 18 effect streams over 10,000 timer steps. The command
  stream was also proven PCM-identical through the original DBOPL
  wrapper before the output stage was deliberately refined to bypass that
  wrapper's clipping mixer.
- Music-loop tests pin repeated command states, including the 59.04-second
  first-level cycle and `Lemming 2`'s long combined voice cycle.
- The current 49,716 Hz raw DBOPL render reports zero clipped synthesis samples
  across all 39 streams. Decoding every final music ADPCM file likewise finds
  zero samples at or above 32,000 magnitude.
- The callback synth test renders the first level's original music program for
  three seconds with nonzero output and no clipped samples. A separate golden
  test matches 49,716 native samples byte-for-byte with the approved JavaScript
  DBOPL oracle (`FNV-1a 79358478ee30ec6d`), and all 21 tracks pass a native
  one-second render smoke test.
- Render-ahead tests verify producer high-water behavior, consumer accounting,
  explicit zero-filled underruns, and safe buffer flushing. In the Simulator,
  moving DBOPL out of the audio callback reduced active-game host CPU from
  roughly 16% to roughly 11%.
- The runtime C sequencer's Fun 1 register stream matches the independent
  Python reference byte-for-byte for its complete 4,609-step command cycle.
- The native-audio PDX builds for Simulator and ARM and launches successfully
  in Playdate Simulator 3.1.1. Simulator gameplay with active music uses about
  16% host CPU. The device-rate regression renders all commands without
  clipping and measures Fun 1 at 59.041224 seconds versus 59.041270 seconds for
  the reference-rate path. Physical-device underrun validation remains required.

## Current private artifact

The current device validation build is release 0.0.21 (build 21), is named
`Lemmings`, and is published by GreenTree Industries. The ARM-only PDX
contains 120 levels, the 22,125-byte AdLib image, the runtime OPL2 engine, and
15 IMA ADPCM effects. Its SDK-compliant launcher artwork includes an uncropped
350 x 155 card, 32 x 32 icon, and 400 x 240 launch image. PackBits-compressed
level planes and deduplicated sprite bytes reduce it to 2,841,320 bytes as
unpacked PDX content, 90.3% of the 3 MiB packaging limit; the sideload ZIP is
1,144,126 bytes. Simulator binaries are excluded from device builds. The asset
pack and AdLib image are unchanged from 0.0.20; only engine code differs.

A `TEST_UNLOCK=1` build exposes every level for device testing. It never
writes `save.dat` and never records a completion, so it cannot alter saved
progression, and it stages a marker that the release packager refuses. The
release target never sets the flag, and a change to it discards the SDK object
tree so a release build cannot reuse test-compiled objects.

Key SHA-256 values:

```text
4130427aff14ce0f53838fe27158179d920a5ea28d901924f2f5d19c0925249f  lemmings.lpd
ad4f33eb383c60b5a67945987f27580fc0540e38558d4c158efe1a672db5f490  adlib.bin
20a7bce3315d73591ef45a5d712660b156c25eb2da2c72631a2605628f5b81d5  pdex.bin
2d7bc4438188255a81f011998d0525c70686ee39b6291a10209c68a2cf49bbd4  Lemmings.pdx.zip
```

The artifact and converted assets are intentionally ignored because they are
derived from the owner's reference copy. Physical-device installation and
performance profiling require a connected Playdate and are the only validation
gate that cannot be performed in this workspace.
