"""Write a deterministic OPL-register trace from the clean interpreter."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct

from tools.assetc.crunch import sections
from .sound_image import SoundImagePlayer


def trace(data: bytes, selector: str, steps: int) -> bytes:
    kind, index_text = selector.split(":", 1)
    player = SoundImagePlayer(data)
    player.music(int(index_text)) if kind == "music" else player.sound(int(index_text))
    output = bytearray(b"LPR1" + struct.pack("<I", steps))
    for step in range(steps):
        for register, value in player.step():
            output.extend(struct.pack("<IBB", step, register, value))
    return bytes(output)


def synthesis_trace(data: bytes, selector: str, sample_count: int,
                    sample_rate: int = 49716) -> bytes:
    """Return a timed register stream for an OPL synthesizer.

    LPR3 adds the sound driver's timer factor, desired output length, and OPL
    synthesis rate to the validation-oriented LPR1 stream. Records remain six
    bytes. 49,716 Hz keeps synthesis close to the OPL3 clock-derived rate before
    the final analog-style filtering and Playdate downsampling stage.
    """
    kind, index_text = selector.split(":", 1)
    player = SoundImagePlayer(data)
    player.music(int(index_text)) if kind == "music" else player.sound(int(index_text))
    samples_per_tick = int(sample_rate / (player.sample_rate_factor / 210) + 0.5)
    steps = (sample_count + samples_per_tick - 1) // samples_per_tick
    output = bytearray(b"LPR3" + struct.pack(
        "<IIII", steps, player.sample_rate_factor, sample_count, sample_rate))
    for step in range(steps):
        for register, value in player.step():
            output.extend(struct.pack("<IBB", step, register, value))
    return bytes(output)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adlib", type=Path, required=True)
    parser.add_argument("--track", default="music:0")
    parser.add_argument("--steps", type=int, default=10000)
    parser.add_argument("--samples", type=int,
                        help="write a timed LPR3 stream with this many samples")
    parser.add_argument("--rate", type=int, default=49716,
                        help="synthesis rate for --samples (default: 49716)")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    parts = list(sections(args.adlib.read_bytes()))
    if len(parts) != 1:
        raise SystemExit("unexpected ADLIB.DAT")
    result = (synthesis_trace(parts[0].payload, args.track, args.samples, args.rate)
              if args.samples is not None else
              trace(parts[0].payload, args.track, args.steps))
    if args.out:
        args.out.write_bytes(result)
    print(hashlib.sha256(result).hexdigest(), len(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
