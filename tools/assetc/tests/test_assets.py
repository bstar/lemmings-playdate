from pathlib import Path
import tempfile
import unittest

from tools.assetc.cli import build
from tools.assetc.crunch import FormatError, sections
from tools.assetc.graphics import BAYER_2, BAYER_4, CLUSTER_2
from tools.assetc.levels import canonical_levels, load_oddtable, load_unique
from tools.assetc.pack import (HEADER, RECORD, packbits_decode, packbits_encode,
                               validate)
from tools.assetc.sprites import (ATLAS_HEADER, ATLAS_RECORD, OBJECT_SLOT_COUNT,
                                  SLOT_COUNT, ACTION_BYTES, ACTION_COUNT,
                                  ACTION_DIGIT_BYTES, ACTION_HEIGHT, ACTION_WIDTH,
                                  EXPLOSION_FRAME_COUNT,
                                  EXPLOSION_PARTICLE_BYTES, _append_frames,
                                  action_art, clean_ink, explosion_particles)


ROOT = Path(__file__).resolve().parents[3]
DOS = ROOT / "reference" / "lemming1.pc"


class CrunchTests(unittest.TestCase):
    def test_known_section_shapes(self):
        self.assertEqual([s.decompressed_size for s in sections((DOS / "main.dat").read_bytes())],
                         [21104, 388, 8384, 61968, 36080, 758, 8224])
        self.assertEqual([s.decompressed_size for s in sections((DOS / "adlib.dat").read_bytes())],
                         [22125])

    def test_checksum_rejected(self):
        data = bytearray((DOS / "level000.dat").read_bytes())
        data[20] ^= 1
        with self.assertRaises(FormatError):
            list(sections(bytes(data)))


class LevelTests(unittest.TestCase):
    def test_canonical_order(self):
        unique = load_unique(DOS)
        levels = canonical_levels(unique, load_oddtable(DOS / "oddtable.dat"))
        self.assertEqual(len(unique), 80)
        self.assertEqual(len(levels), 120)
        self.assertEqual(levels[0].name, "Just dig!")
        self.assertEqual(levels[29].name, "Lock up your Lemmings")
        self.assertEqual(levels[30].name, "This should be a doddle!")
        self.assertEqual(levels[-1].name, "Rendezvous at the Mountain")

    def test_pack_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "lemmings.lpd"
            report = build(DOS, path)
            self.assertEqual(report["version"], 6)
            self.assertEqual(report["level_count"], 120)
            self.assertEqual(validate(path)["names"][0], "Just dig!")
            self.assertGreater(report["sprite_bytes"], 100_000)
            self.assertGreater(path.stat().st_size, 2_000_000)
            self.assertLess(path.stat().st_size, 2_700_000)
            data = path.read_bytes()
            directory = HEADER.unpack_from(data)[5]
            values = RECORD.unpack_from(data, directory)
            offsets = values[20:25]
            ends = (*offsets[1:], offsets[-1] + values[28])
            planes = [packbits_decode(data[start:end], 32_000)
                      for start, end in zip(offsets, ends)]
            self.assertEqual(len(set(planes[:4])), 4)

    def test_packbits_round_trip_and_boundaries(self):
        sources = (
            b"",
            b"A",
            b"AB",
            b"AAA",
            bytes(range(128)),
            bytes(range(128)) + b"ZZZ" + bytes(range(127)),
            b"Q" * 130 + b"Q" * 130 + b"end",
        )
        for source in sources:
            encoded = packbits_encode(source)
            self.assertEqual(packbits_decode(encoded, len(source)), source)

    def test_packbits_rejects_truncated_and_trailing_data(self):
        with self.assertRaises(ValueError):
            packbits_decode(bytes((2, ord("a"), ord("b"))), 3)
        with self.assertRaises(ValueError):
            packbits_decode(bytes((0x80, ord("a"), 0)), 3)


class SpriteTests(unittest.TestCase):
    def test_exact_dos_action_panel_art(self):
        main_sections = list(sections((DOS / "main.dat").read_bytes()))
        palette = ((0, 0, 0), (64, 64, 224), (0, 176, 0), (240, 208, 208),
                   (240, 240, 0), (240, 32, 32), (128, 128, 128), (224, 128, 32),
                   *((255, 255, 255),) * 8)
        actions, digits = action_art(main_sections, palette)
        self.assertEqual((ACTION_COUNT, ACTION_WIDTH, ACTION_HEIGHT), (12, 16, 40))
        self.assertEqual(len(actions), ACTION_BYTES)
        self.assertEqual(len(digits), ACTION_DIGIT_BYTES)
        self.assertTrue(any(actions))
        self.assertTrue(any(digits))

    def test_exact_dos_explosion_particle_table(self):
        particles = explosion_particles(DOS)
        self.assertEqual(len(particles), EXPLOSION_PARTICLE_BYTES)
        self.assertEqual(EXPLOSION_FRAME_COUNT, 51)
        self.assertEqual(tuple(int.from_bytes(particles[index:index + 1], "little", signed=True)
                               for index in range(8)),
                         (-52, -100, -23, -47, 7, -25, -2, -21))

    def test_clean_ink_uses_fixed_luminance_threshold(self):
        self.assertTrue(clean_ink((0, 0, 0)))
        self.assertTrue(clean_ink((159, 159, 159)))
        self.assertFalse(clean_ink((160, 160, 160)))
        self.assertFalse(clean_ink((255, 255, 255)))

    def test_clean_sprite_mask_is_exact_and_not_position_dithered(self):
        payload = bytearray()
        records = [b"\0" * ATLAS_RECORD.size
                   for _ in range(SLOT_COUNT + OBJECT_SLOT_COUNT)]
        pixels = bytes((0, 1, 0, 1, 0, 1, 0, 1) * 4)
        source_mask = bytes((1,) * 24 + (1, 1, 1, 1, 0, 0, 0, 0))

        _append_frames(payload, records, 0,
                       lambda _frame: (pixels, source_mask),
                       8, 4, 1, ((0, 0, 0), (255, 255, 255)),
                       ATLAS_HEADER.size)

        self.assertEqual(payload[:4], bytes((0xFF, 0xFF, 0xFF, 0xF0)))
        self.assertEqual(payload[4:], bytes((0xAA, 0xAA, 0xAA, 0xA0)))

    def test_silhouette_makes_every_opaque_sprite_pixel_black(self):
        payload = bytearray()
        records = [b"\0" * ATLAS_RECORD.size
                   for _ in range(SLOT_COUNT + OBJECT_SLOT_COUNT)]
        pixels = bytes((0, 1, 0, 1, 0, 1, 0, 1))
        source_mask = bytes((1, 1, 1, 1, 1, 1, 0, 0))

        _append_frames(payload, records, 0,
                       lambda _frame: (pixels, source_mask),
                       8, 1, 1, ((0, 0, 0), (255, 255, 255)),
                       ATLAS_HEADER.size, silhouette=True)

        self.assertEqual(payload, bytes((0xFC, 0xFC)))

    def test_identical_sprite_pixels_share_atlas_storage(self):
        payload = bytearray()
        records = [b"\0" * ATLAS_RECORD.size
                   for _ in range(SLOT_COUNT + OBJECT_SLOT_COUNT)]
        frame = lambda _index: (bytes((1,) * 8), bytes((1,) * 8))
        for slot in (0, 1):
            _append_frames(payload, records, slot, frame, 8, 1, 1,
                           ((0, 0, 0), (0, 0, 0)), ATLAS_HEADER.size)

        self.assertEqual(len(payload), 2)
        self.assertEqual(ATLAS_RECORD.unpack(records[0])[0],
                         ATLAS_RECORD.unpack(records[1])[0])

    def test_portal_sprite_has_solid_and_ordered_dither_planes(self):
        payload = bytearray()
        records = [b"\0" * ATLAS_RECORD.size
                   for _ in range(SLOT_COUNT + OBJECT_SLOT_COUNT)]
        pixels = bytes((0, 0, 0, 0))

        _append_frames(payload, records, 0,
                       lambda _frame: (pixels, bytes((1, 1, 1, 1))),
                       2, 2, 1, ((128, 128, 128),), ATLAS_HEADER.size,
                       dither_matrices=(BAYER_2,))

        self.assertEqual(ATLAS_RECORD.unpack(records[0])[-1], 6)
        self.assertEqual(payload, bytes((0xC0, 0xC0, 0xC0, 0xC0, 0x80, 0x40)))


class DitherTests(unittest.TestCase):
    def test_threshold_masks_are_complete_rankings(self):
        for matrix in (BAYER_2, CLUSTER_2, BAYER_4):
            size = len(matrix)
            self.assertTrue(all(len(row) == size for row in matrix))
            self.assertEqual(sorted(value for row in matrix for value in row),
                             list(range(size * size)))

    def test_clustered_and_dispersed_2x2_patterns_differ(self):
        self.assertNotEqual(CLUSTER_2, BAYER_2)


if __name__ == "__main__":
    unittest.main()
