"""Reproduce the tracked Playdate effects from the research sound archive.

The original archive is not committed. This importer verifies the known
research capture, uses DOS AdLib recordings for effects, and substitutes the
original Amiga voice clips for the three iconic spoken lines selected for the
hybrid port.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile
import zipfile


ARCHIVE_SHA256 = "b8d5753f402b9e24d84282479f4a73f2758ac5fff66c0d9e1db48c098181c7b6"
EFFECTS = (
    "door", "letsgo", "changeop", "mousepre", "ting", "yippee",
    "glug", "splat", "fire", "electric", "chain", "tenton", "ohno",
    "explode", "thud",
)
VOICES = {"letsgo", "ohno", "yippee"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    payload = args.archive.read_bytes()
    if hashlib.sha256(payload).hexdigest() != ARCHIVE_SHA256:
        raise SystemExit("unrecognized Lemmings sound-pack archive")
    args.out.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.archive) as source, tempfile.TemporaryDirectory() as temporary:
        temporary_path = Path(temporary)
        for name in EFFECTS:
            member = (f"AmigaLemmings/{name.upper()}.wav" if name in VOICES else
                      f"DOSLemmingsAdLib/{name}.wav")
            raw = temporary_path / f"{name}.wav"
            raw.write_bytes(source.read(member))
            subprocess.run([
                "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                "-i", str(raw), "-af", "lowpass=f=8500:p=2",
                "-ar", "22050", "-ac", "1", "-c:a", "adpcm_ima_wav",
                str(args.out / f"{name}.wav"),
            ], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
