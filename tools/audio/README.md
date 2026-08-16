# DOS AdLib preservation pipeline

`sound_image.py` is a clean implementation of the DOS Sound Images command
format. `render.py` decompresses the original `ADLIB.DAT`, emits its 21 OPL
register stream locally, and passes only those register writes to an external
DBOPL synthesizer. The generated WAV files are private derived assets and are
not committed.

DBOPL runs at 49,716 Hz and its raw 32-bit output bypasses the reference
wrapper's clipping 2x mixer, providing 6 dB of fixed headroom. Before IMA ADPCM
encoding, a two-pole 8.5 kHz low-pass models the softening of the hardware
output path and the result is high-quality resampled to 22,050 Hz for Playdate.
This avoids both synthesis-rate aliasing and the hard clipping found in the
initial captures while retaining the relative dynamics between songs.

Music capture length is derived from the first exactly repeated state of the
DOS command interpreter rather than an arbitrary duration. The converter emits
`music_loops.json`; the matching Playdate table preserves non-looping intros and
uses the exact command-cycle boundaries with `FilePlayer.setLoopRange`.

The current FM renderer expects a locally obtained LemmingsJS bundle containing
DBOPL. That project has no declared repository license, so its code is neither
copied nor shipped. It is an offline build input, not a runtime dependency. The
OPL core can later be replaced without changing the command interpreter or
Playdate playback.

`trace_reference.js` is a validation-only oracle. During development, all 21
music and 18 sound-effect register streams were compared byte-for-byte for
10,000 timer steps against the independently developed player. The checked-in
unit test pins a known register-trace digest without requiring that bundle.

```sh
python3 -m tools.audio.render \
  --dos-dir reference/lemming1.pc \
  --reference-bundle /path/to/lemmings.js \
  --out generated/audio
```
