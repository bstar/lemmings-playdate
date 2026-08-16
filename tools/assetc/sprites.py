"""Convert the DOS MAIN.DAT lemming animations to a compact 1-bit atlas."""

from __future__ import annotations

import hashlib
from pathlib import Path
import struct

from .crunch import sections
from .graphics import BAYER_2, BAYER_4, CLUSTER_2, planar


ATLAS_HEADER = struct.Struct("<4sHHHHI")
ATLAS_RECORD = struct.Struct("<IHHBBbbH")
SLOT_COUNT = 40
OBJECT_SLOT_COUNT = 80
MONO_THRESHOLD = 160
EXPLOSION_FRAME_COUNT = 51
EXPLOSION_PARTICLE_COUNT = 80
EXPLOSION_PARTICLE_BYTES = EXPLOSION_FRAME_COUNT * EXPLOSION_PARTICLE_COUNT * 2
EXPLOSION_TABLE_SHA256 = "a466969e1fe00205ef0db0d0dfb0fa337f9d05156b9b4dc88c31aeb5468b838f"
ACTION_COUNT = 12
ACTION_WIDTH = 16
ACTION_HEIGHT = 40
ACTION_ROW_BYTES = ACTION_WIDTH // 8
ACTION_BYTES = ACTION_COUNT * ACTION_ROW_BYTES * ACTION_HEIGHT
ACTION_DIGIT_COUNT = 20
ACTION_DIGIT_BYTES = ACTION_DIGIT_COUNT * 8
ACTION_PANEL_SHA256 = "e5da6a14e9849e7297dd0fb9f0e36812b0b552ba921775a666242fc83bbca5db"
ACTION_DIGITS_SHA256 = "86353a37043194292432ba2165bc1af4e5d043f3caf2ec5747453cfd79bc9f17"


def explosion_particles(directory: Path) -> bytes:
    """Extract the exact DOS particle-position table from the unpacked EXE."""
    raw_path = directory.resolve().parents[1] / "preservation/private/vgalemmi.unpacked.raw"
    if not raw_path.is_file():
        raise ValueError("run `make decompile` before compiling explosion particles")
    raw = raw_path.read_bytes()
    # The program starts with DS=09A9h; the table is at DS:2788h.
    start = 0x9A90 + 0x2788
    result = raw[start:start + EXPLOSION_PARTICLE_BYTES]
    if (len(result) != EXPLOSION_PARTICLE_BYTES or
            hashlib.sha256(result).hexdigest() != EXPLOSION_TABLE_SHA256):
        raise ValueError("unexpected DOS explosion-particle table")
    return result


def clean_ink(color: tuple[int, int, int]) -> bool:
    """Map an opaque VGA color to one stable monochrome value."""
    r, g, b = color
    luminance = (r * 54 + g * 183 + b * 19) // 256
    return luminance < MONO_THRESHOLD


def action_art(main_sections, palette) -> tuple[bytes, bytes]:
    """Extract the twelve canonical DOS panel tiles and count digits."""
    panel = main_sections[6].payload[:0x1900]
    digits = main_sections[2].payload[0x1900:0x19A0]
    if (len(panel) != 0x1900 or hashlib.sha256(panel).hexdigest() != ACTION_PANEL_SHA256 or
            len(digits) != ACTION_DIGIT_BYTES or
            hashlib.sha256(digits).hexdigest() != ACTION_DIGITS_SHA256):
        raise ValueError("unexpected DOS action-panel graphics")
    pixels = planar(panel, 0, 320, ACTION_HEIGHT, 4)
    tiles = bytearray(ACTION_BYTES)
    for action in range(ACTION_COUNT):
        for y in range(ACTION_HEIGHT):
            for x in range(ACTION_WIDTH):
                color = palette[pixels[y * 320 + action * ACTION_WIDTH + x]]
                if clean_ink(color):
                    at = action * ACTION_ROW_BYTES * ACTION_HEIGHT + y * ACTION_ROW_BYTES + x // 8
                    tiles[at] |= 0x80 >> (x & 7)
    return bytes(tiles), digits


def dither_ink(color: tuple[int, int, int], x: int, y: int,
               matrix: tuple[tuple[int, ...], ...]) -> bool:
    """Map a VGA color through the same ordered threshold used by terrain."""
    r, g, b = color
    luminance = (r * 54 + g * 183 + b * 19) // 256
    size = len(matrix)
    threshold_count = size * size
    ink = threshold_count - 1 - luminance * threshold_count // 256
    return matrix[y % size][x % size] <= ink


# (slot, width, height, bpp, file offset, frame count, x offset, y offset)
SPECS = (
    (2, 16, 10, 2, 0x0000, 8, -8, -10), (3, 16, 10, 2, 0x0168, 8, -8, -10),
    (4, 16, 10, 2, 0x37F0, 4, -8, -10), (5, 16, 10, 2, 0x3890, 4, -8, -10),
    (6, 16, 10, 2, 0x0140, 1, -8, -10), (7, 16, 10, 2, 0x02A8, 1, -8, -10),
    (8, 16, 12, 2, 0x0810, 8, -8, -12), (9, 16, 12, 2, 0x0990, 8, -8, -12),
    (10, 16, 12, 2, 0x0D90, 8, -8, -12), (11, 16, 12, 2, 0x0F10, 8, -8, -12),
    (12, 16, 16, 3, 0x3930, 8, -8, -16), (13, 16, 16, 3, 0x3C30, 8, -8, -16),
    (14, 16, 10, 2, 0x4970, 16, -8, -10), (15, 16, 10, 2, 0x4970, 16, -8, -10),
    (16, 16, 13, 3, 0x1090, 16, -8, -13), (17, 16, 13, 3, 0x1570, 16, -8, -13),
    (18, 16, 10, 2, 0x4BF0, 8, -8, -10), (19, 16, 10, 2, 0x4D30, 8, -8, -10),
    (20, 16, 10, 3, 0x1A50, 32, -8, -10), (21, 16, 10, 3, 0x21D0, 32, -8, -10),
    (22, 16, 13, 3, 0x2950, 24, -8, -12), (23, 16, 13, 3, 0x30A0, 24, -8, -12),
    (24, 16, 14, 3, 0x02D0, 16, -8, -12), (25, 16, 14, 3, 0x02D0, 16, -8, -12),
    (26, 16, 10, 2, 0x4E70, 16, -8, -10), (27, 16, 10, 2, 0x4E70, 16, -8, -10),
    (28, 32, 32, 3, 0x5070, 1, -16, -16), (29, 32, 32, 3, 0x5070, 1, -16, -16),
    (30, 16, 10, 2, 0x3F30, 16, -8, -10), (31, 16, 10, 2, 0x3F30, 16, -8, -10),
    (32, 16, 10, 2, 0x0B10, 16, -8, -10), (33, 16, 10, 2, 0x0B10, 16, -8, -10),
    (34, 16, 14, 4, 0x4350, 14, -8, -10), (35, 16, 14, 4, 0x4350, 14, -8, -10),
    (36, 16, 13, 2, 0x41B0, 8, -8, -13), (37, 16, 13, 2, 0x41B0, 8, -8, -13),
)


def _append_frames(payload: bytearray, records: list[bytes], slot: int, pixels_for_frame,
                   width: int, height: int, frame_count: int, palette,
                   header_bytes: int, offset_x: int = 0, offset_y: int = 0,
                   dither_matrices=None, silhouette: bool = False) -> None:
    row_bytes = (width + 7) // 8
    plane_bytes = row_bytes * height
    ink_planes = 1 if dither_matrices is None else 1 + len(dither_matrices)
    frame_bytes = plane_bytes * (1 + ink_planes)
    encoded = bytearray()
    for frame in range(frame_count):
        pixels, source_mask = pixels_for_frame(frame)
        mask = bytearray(row_bytes * height)
        inks = [bytearray(plane_bytes) for _ in range(ink_planes)]
        for y in range(height):
            for x in range(width):
                value = pixels[y * width + x]
                if not source_mask[y * width + x]:
                    continue
                at = y * row_bytes + x // 8
                bit = 0x80 >> (x & 7)
                mask[at] |= bit
                if dither_matrices is None:
                    if silhouette or clean_ink(palette[value]):
                        inks[0][at] |= bit
                else:
                    inks[0][at] |= bit
                    for index, matrix in enumerate(dither_matrices, 1):
                        if dither_ink(palette[value], x, y, matrix):
                            inks[index][at] |= bit
        encoded.extend(mask)
        for ink in inks:
            encoded.extend(ink)
    existing = payload.find(encoded)
    if existing < 0:
        existing = len(payload)
        payload.extend(encoded)
    data_offset = (header_bytes + (SLOT_COUNT + OBJECT_SLOT_COUNT) * ATLAS_RECORD.size +
                   existing)
    records[slot] = ATLAS_RECORD.pack(data_offset, width, height, frame_count,
                                      row_bytes, offset_x, offset_y, frame_bytes)


def build_atlas(directory: Path, ground_sets) -> bytes:
    main_sections = list(sections((directory / "main.dat").read_bytes()))
    main = main_sections[0].payload
    records = [b"\0" * ATLAS_RECORD.size for _ in range(SLOT_COUNT + OBJECT_SLOT_COUNT)]
    payload = bytearray()
    # VGA DAC values scaled to 8-bit; color zero is transparent.
    palette = ((0, 0, 0), (64, 64, 224), (0, 176, 0), (240, 208, 208),
               (240, 240, 0), (240, 32, 32), (128, 128, 128), (224, 128, 32),
               (255, 255, 255), (255, 255, 255), (255, 255, 255), (255, 255, 255),
               (255, 255, 255), (255, 255, 255), (255, 255, 255), (255, 255, 255))
    for slot, width, height, bpp, source, frame_count, ox, oy in SPECS:
        source_frame_bytes = ((width * height + 7) // 8) * bpp
        def lemming_frame(frame, source=source, width=width, height=height,
                          bpp=bpp, source_frame_bytes=source_frame_bytes):
            pixels = planar(main, source + frame * source_frame_bytes, width, height, bpp)
            return pixels, bytes(1 if value else 0 for value in pixels)
        _append_frames(payload, records, slot, lemming_frame, width, height, frame_count,
                       palette, ATLAS_HEADER.size, ox, oy, silhouette=True)
    for style, ground in enumerate(ground_sets):
        for object_id, info in enumerate(ground.objects):
            if not info.width or not info.height or not info.frame_count:
                continue
            def object_frame(frame, ground=ground, info=info):
                source = info.image_offset + frame * info.frame_size
                pixels = planar(ground.object_data, source, info.width, info.height, 4)
                count = info.width * info.height
                mask = tuple(1 if ground.object_data[source + info.mask_offset + p // 8] &
                             (0x80 >> (p & 7)) else 0 for p in range(count))
                return pixels, mask
            _append_frames(payload, records, SLOT_COUNT + style * 16 + object_id,
                           object_frame, info.width, info.height, info.frame_count,
                           ground.object_palette, ATLAS_HEADER.size,
                           dither_matrices=(BAYER_2, CLUSTER_2, BAYER_4)
                           if object_id == 1 or info.trigger_effect == 1 else None)
    # Keep the original twelve action tiles and their position-specific count
    # digits intact. They are rendered at exact 2x scale by the Playdate UI.
    actions, action_digits = action_art(main_sections, palette)
    payload.extend(actions)
    payload.extend(action_digits)
    # DOS renders explosions from 51 frames of 80 signed particle coordinates.
    # Keep this immediately before the terrain masks so the runtime can locate
    # both fixed-size canonical tables without expanding the atlas directory.
    payload.extend(explosion_particles(directory))
    # The 388-byte second MAIN section is the exact DOS terrain-edit mask set.
    payload.extend(main_sections[1].payload)
    total = ATLAS_HEADER.size + sum(map(len, records)) + len(payload)
    return ATLAS_HEADER.pack(b"LPS3", SLOT_COUNT, OBJECT_SLOT_COUNT,
                             ATLAS_RECORD.size, EXPLOSION_FRAME_COUNT, total) + b"".join(records) + payload
