# Contributing

Thanks for your interest. This document covers the one rule that is unusual
here, then the ordinary things: building, testing, and submitting changes.

## Never commit game material

This project reimplements an engine. It does not redistribute the game.

Do not commit original game data, converted assets, sound recordings, level
files, or artwork derived from the original, and do not commit anything
generated from them. That includes the contents of `reference/`, imported
effects under `playdate/effects/`, launcher artwork under
`playdate/Source/SystemAssets/`, and everything under `generated/`.

`.gitignore` covers these paths, and `.gitattributes` keeps `reference/` out of
`git archive` exports. Treat both as a backstop rather than a guarantee: check
`git status` before you commit, and never use `git add -f` on those paths.

A pull request containing such material cannot be merged, because removing it
afterwards means rewriting history.

## Getting a build

You need your own copy of the original DOS data. See the README for what to
supply and where it goes.

With Nix:

```sh
nix develop
make playdate-sdk
```

Without Nix you need Clang or GCC, Python 3 with Pillow, ffmpeg, zip, the
Playdate SDK, and an Arm GNU Toolchain with newlib for device builds.

## Testing

Run these before opening a pull request:

```sh
make test    # unit tests, engine core tests, audio oracles
make host    # loads and simulates all 120 levels
```

`make test` builds with `-Wall -Wextra -Werror`. Warnings are failures, so a
change that introduces one will not build.

If you change engine behaviour, add or update a test in
`playdate/tests/test_game.c`. Prefer assertions written against the named
constants in `playdate/core/lp_game.h` rather than hard-coded numbers, so that
tuning a constant cannot silently invalidate the bookkeeping around it.

Changes to the Playdate adapter in `playdate/src/main.c` are not covered by the
test suite. Say in your pull request how you exercised them, and whether that
was in the Simulator or on hardware.

## Code style

Match the surrounding code rather than importing a different house style. In
practice that means C11 for the engine, four-space indentation, no tabs, and
comments that explain why something is the way it is, especially where
behaviour is dictated by the original game.

The engine under `playdate/core/` stays free of Playdate SDK calls. Platform
code belongs in `playdate/src/`. That separation is what lets the host build
and tests run the same simulation.

## Submitting changes

Keep a pull request to one concern. Explain what changed, why, and how you
verified it. If a change affects gameplay, say what it means for someone
playing, since not every reviewer will recognise the significance of a constant
moving by three pixels.

Reverse-engineering claims should cite evidence: the binary hash, the address,
and how you observed the behaviour. `preservation/README.md` describes the
standard expected. Decompiled or disassembled material stays private and out of
the repository.

## Licensing

Contributions are accepted under the MIT License, matching this project's own
code. By submitting a pull request you confirm you have the right to contribute
the work under that licence.

Note that a built package is still governed by the GPL, because it links DBOPL.
[LICENSING.md](LICENSING.md) explains the split.

Sign off your commits with `git commit -s`, which records agreement with the
Developer Certificate of Origin at https://developercertificate.org/.
