"""Writer and validator for the streamable LPD1 asset pack."""

from __future__ import annotations

import hashlib
from pathlib import Path
import struct

from .graphics import GroundSet, PLANE_BYTES
from .levels import Level
from .sprites import build_atlas


MAGIC = b"LPD1"
VERSION = 6
HEADER = struct.Struct("<4sHHIIIIII32s")
RECORD = struct.Struct("<BBBBHhHHHH8HH32sIIIIIIIHH")
OBJECT = struct.Struct("<hhBBBBhhhhHBB")


def packbits_encode(source: bytes) -> bytes:
    """Encode bytes as bounded literal and repeated runs.

    Control bytes 0..127 introduce 1..128 literal bytes. Controls 128..255
    introduce a repeated byte 3..130 times. Two-byte runs stay literal.
    """
    output = bytearray()
    offset = 0
    while offset < len(source):
        run = 1
        while (offset + run < len(source) and run < 130 and
               source[offset + run] == source[offset]):
            run += 1
        if run >= 3:
            output.extend((0x80 | (run - 3), source[offset]))
            offset += run
            continue
        start = offset
        offset += run
        while offset < len(source) and offset - start < 128:
            run = 1
            while (offset + run < len(source) and run < 130 and
                   source[offset + run] == source[offset]):
                run += 1
            if run >= 3:
                break
            offset += min(run, 128 - (offset - start))
        output.append(offset - start - 1)
        output.extend(source[start:offset])
    return bytes(output)


def packbits_decode(source: bytes, output_size: int) -> bytes:
    """Decode a complete PackBits stream and reject malformed/trailing data."""
    output = bytearray()
    offset = 0
    while offset < len(source) and len(output) < output_size:
        control = source[offset]
        offset += 1
        if control & 0x80:
            count = (control & 0x7f) + 3
            if offset >= len(source) or len(output) + count > output_size:
                raise ValueError("invalid repeated run")
            output.extend((source[offset],) * count)
            offset += 1
        else:
            count = control + 1
            if offset + count > len(source) or len(output) + count > output_size:
                raise ValueError("invalid literal run")
            output.extend(source[offset:offset + count])
            offset += count
    if offset != len(source) or len(output) != output_size:
        raise ValueError("incomplete PackBits stream")
    return bytes(output)


def _name(value: str) -> bytes:
    return value.encode("ascii", "replace")[:32].ljust(32, b" ")


def write_pack(path: Path, levels: list[Level],
               planes: list[tuple[bytes, bytes, bytes, bytes, bytes]],
               source_digest: bytes, ground_sets: list[GroundSet], dos_dir: Path) -> None:
    if len(levels) != len(planes):
        raise ValueError("level/plane count mismatch")
    directory_offset = HEADER.size
    data_offset = directory_offset + len(levels) * RECORD.size
    records = bytearray()
    payload = bytearray()
    shared: dict[tuple[bytes, ...], tuple[int, ...]] = {}
    for level, (visual, bayer2, cluster2, solid, steel) in zip(levels, planes):
        key = (visual, bayer2, cluster2, solid, steel)
        if not all(len(value) == PLANE_BYTES for value in key):
            raise ValueError("invalid level plane size")
        if key in shared:
            (visual_offset, bayer2_offset, cluster2_offset, solid_offset,
             steel_offset, steel_size) = shared[key]
        else:
            compressed = tuple(packbits_encode(value) for value in key)
            offsets = []
            for value in compressed:
                offsets.append(data_offset + len(payload))
                payload.extend(value)
            (visual_offset, bayer2_offset, cluster2_offset,
             solid_offset, steel_offset) = offsets
            steel_size = len(compressed[-1])
            shared[key] = (visual_offset, bayer2_offset, cluster2_offset,
                           solid_offset, steel_offset, steel_size)
        object_offset = data_offset + len(payload)
        objects = bytearray()
        ground = ground_sets[level.style]
        for placed in level.objects:
            info = ground.objects[placed.object_id]
            flags = ((1 if placed.upside_down else 0)
                     | (2 if placed.no_overwrite else 0)
                     | (4 if placed.only_overwrite else 0))
            x1 = placed.x + info.trigger_x
            y1 = placed.y + info.trigger_y
            objects.extend(OBJECT.pack(
                placed.x, placed.y, placed.object_id, flags,
                info.trigger_effect, info.sound_effect,
                x1, y1, x1 + info.trigger_width, y1 + info.trigger_height,
                info.anim_flags, info.first_frame, info.frame_count,
            ))
        records.extend(RECORD.pack(
            level.rating, level.number, level.style, level.special,
            level.source_id, level.odd_record,
            level.release_rate, level.lemming_count, level.rescue_count,
            level.time_minutes, *level.skills, level.camera_x, _name(level.name),
            visual_offset, bayer2_offset, cluster2_offset, solid_offset, steel_offset,
            object_offset, PLANE_BYTES, len(level.objects), steel_size,
        ))
        payload.extend(objects)
    atlas = build_atlas(dos_dir, ground_sets)
    sprite_offset = data_offset + len(payload)
    payload.extend(atlas)
    file_size = data_offset + len(payload)
    header = HEADER.pack(MAGIC, VERSION, HEADER.size, len(levels), RECORD.size,
                         directory_offset, file_size, sprite_offset, len(atlas), source_digest)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + records + payload)


def validate(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise ValueError("pack is too small")
    magic, version, header_size, count, record_size, directory, file_size, sprite_offset, sprite_size, source = HEADER.unpack_from(data)
    if (magic, version, header_size, record_size, file_size) != (MAGIC, VERSION, HEADER.size, RECORD.size, len(data)):
        raise ValueError("invalid LPD1 header")
    names = []
    for index in range(count):
        values = RECORD.unpack_from(data, directory + index * record_size)
        name = values[19].decode("ascii").rstrip()
        visual, bayer2, cluster2, solid, steel, objects, size, object_count, steel_size = values[20:29]
        plane_offsets = (visual, bayer2, cluster2, solid, steel)
        plane_ends = (bayer2, cluster2, solid, steel, steel + steel_size)
        if (size != PLANE_BYTES or not steel_size or
                any(start < directory + count * record_size or start >= end or
                    end > sprite_offset
                    for start, end in zip(plane_offsets, plane_ends))):
            raise ValueError(f"invalid level payload {index}")
        for start, end in zip(plane_offsets, plane_ends):
            packbits_decode(data[start:end], PLANE_BYTES)
        if objects + object_count * OBJECT.size > sprite_offset:
            raise ValueError(f"invalid object payload {index}")
        names.append(name)
    return {
        "version": version, "level_count": count, "names": names,
        "source_sha256": source.hex(), "pack_sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data), "sprite_bytes": sprite_size,
    }
