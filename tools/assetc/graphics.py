"""Decode and compose VGA terrain into deterministic Playdate bitplanes."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .crunch import FormatError, sections
from .levels import LEVEL_HEIGHT, LEVEL_WIDTH, Level


ROW_BYTES = LEVEL_WIDTH // 8
PLANE_BYTES = ROW_BYTES * LEVEL_HEIGHT
BAYER_4 = (
    (0, 8, 2, 10),
    (12, 4, 14, 6),
    (3, 11, 1, 9),
    (15, 7, 13, 5),
)
BAYER_2 = (
    (0, 2),
    (3, 1),
)
CLUSTER_2 = (
    (0, 1),
    (3, 2),
)


def le16(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little")


@dataclass(frozen=True)
class TerrainImage:
    width: int
    height: int
    pixels: bytes
    mask: bytes


@dataclass(frozen=True)
class ObjectInfo:
    anim_flags: int
    width: int
    height: int
    first_frame: int
    frame_count: int
    frame_size: int
    image_offset: int
    mask_offset: int
    trigger_x: int
    trigger_y: int
    trigger_width: int
    trigger_height: int
    trigger_effect: int
    sound_effect: int


def planar(data: bytes, offset: int, width: int, height: int, planes: int) -> bytes:
    pixel_count = width * height
    plane_size = (pixel_count + 7) // 8
    end = offset + plane_size * planes
    if offset < 0 or end > len(data):
        raise FormatError("planar image lies outside graphics section")
    output = bytearray(pixel_count)
    for plane in range(planes):
        base = offset + plane * plane_size
        for pixel in range(pixel_count):
            if data[base + pixel // 8] & (0x80 >> (pixel & 7)):
                output[pixel] |= 1 << plane
    return bytes(output)


def mono_mask(data: bytes, offset: int, pixel_count: int) -> bytes:
    size = (pixel_count + 7) // 8
    if offset < 0 or offset + size > len(data):
        raise FormatError("image mask lies outside graphics section")
    return bytes(
        1 if data[offset + pixel // 8] & (0x80 >> (pixel & 7)) else 0
        for pixel in range(pixel_count)
    )


class GroundSet:
    def __init__(self, definition: bytes, terrain_data: bytes, object_data: bytes):
        if len(definition) != 1056:
            raise FormatError("GROUNDxO.DAT must be 1056 bytes")
        self.definition = definition
        self.terrain_data = terrain_data
        self.object_data = object_data
        self.palette = self._palette(0x3D8)
        self.object_palette = self._palette(0x3F0) + self._palette(0x408)
        self.objects = self._objects()
        self.terrain = self._terrain()

    @classmethod
    def load(cls, directory: Path, index: int) -> "GroundSet":
        definition = (directory / f"ground{index}o.dat").read_bytes()
        parts = list(sections((directory / f"vgagr{index}.dat").read_bytes()))
        if len(parts) != 2:
            raise FormatError(f"VGAGR{index}.DAT must have two sections")
        return cls(definition, parts[0].payload, parts[1].payload)

    def _palette(self, offset: int) -> tuple[tuple[int, int, int], ...]:
        return tuple(
            tuple(min(255, self.definition[offset + index * 3 + channel] << 2)
                  for channel in range(3))
            for index in range(8)
        )

    def _objects(self) -> tuple[ObjectInfo, ...]:
        result = []
        for index in range(16):
            offset = index * 28
            result.append(ObjectInfo(
                anim_flags=le16(self.definition, offset),
                width=self.definition[offset + 4], height=self.definition[offset + 5],
                first_frame=self.definition[offset + 2],
                frame_count=self.definition[offset + 3],
                frame_size=le16(self.definition, offset + 6),
                image_offset=le16(self.definition, offset + 21),
                mask_offset=le16(self.definition, offset + 8),
                trigger_x=le16(self.definition, offset + 14) * 4,
                trigger_y=le16(self.definition, offset + 16) * 4 - 4,
                trigger_width=self.definition[offset + 18] * 4,
                trigger_height=self.definition[offset + 19] * 4,
                trigger_effect=self.definition[offset + 20],
                sound_effect=self.definition[offset + 27],
            ))
        return tuple(result)

    def _terrain(self) -> tuple[TerrainImage | None, ...]:
        result = []
        for index in range(64):
            offset = 448 + index * 8
            width, height = self.definition[offset : offset + 2]
            if width == 0 or height == 0:
                result.append(None)
                continue
            image_offset = le16(self.definition, offset + 2)
            mask_offset = le16(self.definition, offset + 4)
            count = width * height
            result.append(TerrainImage(
                width, height,
                planar(self.terrain_data, image_offset, width, height, 3),
                mono_mask(self.terrain_data, mask_offset, count),
            ))
        return tuple(result)


def special_map(path: Path) -> tuple[bytearray, bytearray, tuple[tuple[int, int, int], ...]]:
    parts = list(sections(path.read_bytes()))
    if len(parts) != 1:
        raise FormatError("VGASPEC file must contain one section")
    data = parts[0].payload
    palette = tuple(tuple(min(255, data[i * 3 + c] << 2) for c in range(3)) for i in range(8))
    source = 40
    rows = bytearray()
    chunk = bytearray()
    while source < len(data) and len(rows) < 153600:
        command = data[source]
        source += 1
        if command == 0x80:
            if len(chunk) != 14400:
                raise FormatError("VGASPEC RLE chunk has unexpected size")
            rows.extend(planar(bytes(chunk), 0, 960, 40, 3))
            chunk.clear()
        elif command < 0x80:
            count = command + 1
            chunk.extend(data[source : source + count])
            source += count
        else:
            count = 257 - command
            if source >= len(data):
                raise FormatError("truncated VGASPEC repeat")
            chunk.extend(bytes((data[source],)) * count)
            source += 1
    if len(rows) != 960 * 160:
        raise FormatError("VGASPEC did not produce 960 x 160 pixels")
    colors = bytearray(LEVEL_WIDTH * LEVEL_HEIGHT)
    solid = bytearray(LEVEL_WIDTH * LEVEL_HEIGHT)
    for y in range(LEVEL_HEIGHT):
        for x in range(960):
            value = rows[y * 960 + x]
            if value:
                dest = y * LEVEL_WIDTH + x + 304
                colors[dest] = value
                solid[dest] = 1
    return colors, solid, palette


def _dither_plane(visible: bytearray, colors: bytearray, luminance,
                  matrix: tuple[tuple[int, ...], ...]) -> bytes:
    size = len(matrix)
    threshold_count = size * size
    output = bytearray(PLANE_BYTES)
    for y in range(LEVEL_HEIGHT):
        for x in range(LEVEL_WIDTH):
            pixel = y * LEVEL_WIDTH + x
            if not visible[pixel]:
                continue
            color = colors[pixel] & 7
            ink = threshold_count - 1 - luminance[color] * threshold_count // 256
            if matrix[y % size][x % size] <= ink:
                output[y * ROW_BYTES + x // 8] |= 0x80 >> (x & 7)
    return bytes(output)


def compose(level: Level, directory: Path,
            ground_sets: list[GroundSet]) -> tuple[bytes, bytes, bytes, bytes, bytes]:
    if level.special:
        colors, solid, palette = special_map(directory / f"vgaspec{level.special - 1}.dat")
    else:
        colors = bytearray(LEVEL_WIDTH * LEVEL_HEIGHT)
        solid = bytearray(LEVEL_WIDTH * LEVEL_HEIGHT)
        ground = ground_sets[level.style]
        palette = ground.palette
        for placement in level.terrain:
            image = ground.terrain[placement.terrain_id]
            if image is None:
                continue
            for sy in range(image.height):
                source_y = image.height - sy - 1 if placement.upside_down else sy
                for sx in range(image.width):
                    source = source_y * image.width + sx
                    if not image.mask[source]:
                        continue
                    x, y = placement.x + sx, placement.y + sy
                    if not (0 <= x < LEVEL_WIDTH and 0 <= y < LEVEL_HEIGHT):
                        continue
                    dest = y * LEVEL_WIDTH + x
                    if placement.erase:
                        colors[dest] = 0
                        solid[dest] = 0
                    elif not (placement.no_overwrite and solid[dest]):
                        colors[dest] = image.pixels[source]
                        solid[dest] = 1

    visible = bytearray(solid)
    ground = ground_sets[level.style]

    steel = bytearray(LEVEL_WIDTH * LEVEL_HEIGHT)
    for area in level.steel:
        for y in range(max(0, area.y), min(LEVEL_HEIGHT, area.y + area.height)):
            start = y * LEVEL_WIDTH + max(0, area.x)
            end = y * LEVEL_WIDTH + min(LEVEL_WIDTH, area.x + area.width)
            steel[start:end] = b"\x01" * (end - start)

    solid_bits = bytearray(PLANE_BYTES)
    steel_bits = bytearray(PLANE_BYTES)
    luminance = tuple((r * 54 + g * 183 + b * 19) // 256 for r, g, b in palette)
    for y in range(LEVEL_HEIGHT):
        for x in range(LEVEL_WIDTH):
            pixel = y * LEVEL_WIDTH + x
            byte = y * ROW_BYTES + x // 8
            bit = 0x80 >> (x & 7)
            if solid[pixel]:
                solid_bits[byte] |= bit
            if steel[pixel]:
                steel_bits[byte] |= bit
    # Dark source colors receive more ink. Every pattern phase is anchored to
    # world coordinates, so camera movement cannot shimmer.
    bayer4 = _dither_plane(visible, colors, luminance, BAYER_4)
    bayer2 = _dither_plane(visible, colors, luminance, BAYER_2)
    cluster2 = _dither_plane(visible, colors, luminance, CLUSTER_2)
    return bayer4, bayer2, cluster2, bytes(solid_bits), bytes(steel_bits)
