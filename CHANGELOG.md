# Changelog

Versions follow the Playdate package version in `playdate/Source/pdxinfo`,
where the semantic patch number and the sideload build number advance together.

## 0.0.21

Fixed lemmings dying on falls they should survive. A fall was credited with
three pixels it had not travelled, so the fatal distance was reached four
pixels early. Fall distance now counts real pixels travelled, and the boundary
is a 62 pixel drop surviving and a 63 pixel drop not. Fun 13 showed this on
almost every dig.

Fixed the delay before the first lemming, which followed the level's release
rate and so grew to over six seconds on the slowest levels. The first lemming
now arrives at a fixed delay on every level, while the gap between later
lemmings still follows the release rate.

Falling is quicker, tunable through the constants in `playdate/core/lp_game.h`.
The fatal distance is counted in real pixels and does not move when the speed
changes.

Levels now open centered on the entrance hatch. The original start position
assumes a wider viewport than the 2x detail view, and 13 of the 120 levels
previously began with the hatch partly or entirely off screen.

Added a `TEST_UNLOCK=1` build that exposes every level for device testing. It
never writes the save file and never records a completion, and it stages a
marker that the release packager refuses, so it cannot be published.

Build and packaging: a pinned Nix development environment, an automatic
Playdate SDK fetch, device bundle ZIPs produced by the build, `make decompile`
working under compiler hardening that treats format warnings as errors.

## 0.0.20

First public development package. All 120 original levels, native difficulty
and level selection, saved progress, the paused action panel, exact 2x
presentation, selectable terrain dithering, sound effects, and real-time AdLib
music synthesis.
