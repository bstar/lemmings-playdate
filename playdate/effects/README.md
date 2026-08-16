# Runtime effects

This directory contains the 15 sound-complete runtime assets staged into every
Playdate build. They are 22,050 Hz mono IMA ADPCM WAV files: DOS AdLib captures
for action effects plus the original Amiga voice recordings for `letsgo`,
`ohno`, and `yippee`.

The files can be reproduced with:

```sh
make effects EFFECTS_ARCHIVE=/path/to/LemmingsVersionsNLSounds.zip
```

The importer accepts only the research archive with SHA-256
`b8d5753f402b9e24d84282479f4a73f2758ac5fff66c0d9e1db48c098181c7b6`.
These preservation assets are not relicensed by this repository.
