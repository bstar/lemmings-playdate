"""Interpret DOS AdLib music locally and render it through a DBOPL oracle."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

from tools.assetc.crunch import sections
from .sound_image import SoundImagePlayer, music_loop_steps
from .trace import synthesis_trace


SYNTH_RATE = 49716
PLAYDATE_RATE = 22050


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dos-dir", type=Path, required=True)
    parser.add_argument("--reference-bundle", type=Path, required=True,
                        help="path to a locally obtained bundle containing DBOPL")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=90)
    args = parser.parse_args()
    payloads = list(sections((args.dos_dir / "adlib.dat").read_bytes()))
    if len(payloads) != 1 or len(payloads[0].payload) != 22125:
        raise SystemExit("unexpected ADLIB.DAT structure")
    args.out.mkdir(parents=True, exist_ok=True)
    sound_image = payloads[0].payload
    renderer = Path(__file__).with_name("render_opl_reference.js")
    def render(name: str, selector: str, sample_count: int) -> None:
        final = args.out / f"{name}.wav"
        pcm = args.out / f"{name}.pcm.wav"
        register_trace = args.out / f"{name}.lpr"
        register_trace.write_bytes(synthesis_trace(
            sound_image, selector, sample_count, SYNTH_RATE))
        subprocess.run(["node", str(renderer), str(args.reference_bundle),
                        str(register_trace), str(pcm)], check=True)
        subprocess.run([
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(pcm),
            "-af", f"lowpass=f=8500:p=2,aresample={PLAYDATE_RATE}:filter_size=64:phase_shift=10",
            "-acodec", "adpcm_ima_wav", str(final)
        ], check=True)
        pcm.unlink(); register_trace.unlink()
    loop_metadata = []
    for track in range(21):
        start_step, end_step = music_loop_steps(sound_image, track)
        player = SoundImagePlayer(sound_image).music(track)
        samples_per_tick = int(SYNTH_RATE / (player.sample_rate_factor / 210) + 0.5)
        loop_start = start_step * samples_per_tick / SYNTH_RATE
        loop_end = end_step * samples_per_tick / SYNTH_RATE
        sample_count = max(args.seconds * SYNTH_RATE,
                           end_step * samples_per_tick + SYNTH_RATE)
        render(f"music{track:02}", f"music:{track}", sample_count)
        loop_metadata.append({
            "track": track, "start_step": start_step, "end_step": end_step,
            "start_seconds": loop_start, "end_seconds": loop_end,
        })
    (args.out / "music_loops.json").write_text(
        json.dumps(loop_metadata, indent=2) + "\n", encoding="utf-8")
    for effect in range(18):
        render(f"sfx{effect:02}", f"sfx:{effect}", 4 * SYNTH_RATE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
