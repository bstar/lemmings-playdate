from __future__ import annotations

import hashlib
from pathlib import Path
import unittest

from tools.assetc.crunch import sections
from tools.audio.sound_image import music_loop_steps
from tools.audio.trace import synthesis_trace, trace


ROOT = Path(__file__).resolve().parents[3]


class SoundImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        packed = (ROOT / "reference/lemming1.pc/adlib.dat").read_bytes()
        parts = list(sections(packed))
        if len(parts) != 1:
            raise AssertionError(f"expected one ADLIB.DAT section, got {len(parts)}")
        cls.sound_image = parts[0].payload

    def test_known_music_register_trace(self) -> None:
        result = trace(self.sound_image, "music:0", 1000)
        self.assertEqual(
            hashlib.sha256(result).hexdigest(),
            "727d8dbbf0133e3a5c78ba31f4d3d8fcf64efae9fe1a7c21400c75ce5b36a5b0",
        )

    def test_all_streams_advance(self) -> None:
        for kind, count in (("music", 21), ("sfx", 18)):
            for index in range(count):
                with self.subTest(kind=kind, index=index):
                    result = trace(self.sound_image, f"{kind}:{index}", 1000)
                    self.assertGreater(len(result), 8)

    def test_synthesis_trace_carries_timing(self) -> None:
        result = synthesis_trace(self.sound_image, "sfx:0", 49716)
        self.assertEqual(result[:4], b"LPR3")
        self.assertEqual(int.from_bytes(result[16:20], "little"), 49716)
        self.assertGreater(len(result), 20)

    def test_known_music_loop_states(self) -> None:
        self.assertEqual(music_loop_steps(self.sound_image, 5), (1, 4609))
        self.assertEqual(music_loop_steps(self.sound_image, 6), (1, 120961))


if __name__ == "__main__":
    unittest.main()
