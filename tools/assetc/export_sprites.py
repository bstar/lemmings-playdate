"""Export the generated LPS3 sprite atlas as transparent PNG files."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct

from PIL import Image

from .crunch import sections
from .graphics import GroundSet, planar
from .pack import HEADER
from .sprites import (ATLAS_HEADER, ATLAS_RECORD, OBJECT_SLOT_COUNT, SLOT_COUNT,
                      SPECS)


LEMMING_NAMES = (
    "unused", "walk", "fall", "jump", "climb", "hoist", "float",
    "block", "build", "shrug", "bash", "mine", "dig", "ohno",
    "explode", "splat", "drown", "burn", "exit", "removed",
)
STYLE_NAMES = ("dirt", "fire", "marble", "pillar", "crystal")


def decode_frame(data: bytes, record: tuple[int, ...], frame: int) -> Image.Image:
    offset, width, height, frame_count, row_bytes, _ox, _oy, frame_bytes = record
    if not 0 <= frame < frame_count:
        raise ValueError("sprite frame outside record")
    plane_bytes = frame_bytes // 2
    mask = offset + frame * frame_bytes
    ink = mask + plane_bytes
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            bit = 0x80 >> (x & 7)
            at = y * row_bytes + x // 8
            if data[mask + at] & bit:
                value = 0 if data[ink + at] & bit else 255
                pixels[x, y] = (value, value, value, 255)
    return image


def save_group(data: bytes, records: list[tuple[int, ...]], names: list[str],
               output: Path) -> list[dict[str, object]]:
    metadata = []
    for record, name in zip(records, names):
        offset, width, height, frame_count, _row_bytes, ox, oy, _frame_bytes = record
        if not frame_count:
            continue
        directory = output / name
        directory.mkdir(parents=True, exist_ok=True)
        frames = []
        for frame_index in range(frame_count):
            frame = decode_frame(data, record, frame_index)
            frame.save(directory / f"frame_{frame_index:02}.png")
            frames.append(frame)
        scale = 4
        sheet = Image.new("RGBA", (sum(frame.width for frame in frames) * scale,
                                   max(frame.height for frame in frames) * scale),
                          (0, 0, 0, 0))
        x = 0
        for frame in frames:
            enlarged = frame.resize((frame.width * scale, frame.height * scale),
                                    Image.Resampling.NEAREST)
            sheet.alpha_composite(enlarged, (x, 0)); x += enlarged.width
        sheet.save(output / f"{name}_sheet_4x.png")
        metadata.append({"name": name, "offset": offset, "width": width,
                         "height": height, "frames": frame_count,
                         "origin": [ox, oy]})
    return metadata


def indexed_frame(pixels: bytes, mask: bytes, width: int, height: int,
                  palette: tuple[tuple[int, int, int], ...]) -> Image.Image:
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    output = image.load()
    for y in range(height):
        for x in range(width):
            at = y * width + x
            if mask[at]:
                output[x, y] = (*palette[pixels[at]], 255)
    return image


def save_color_group(frames: list[Image.Image], name: str, output: Path,
                     origin: tuple[int, int] = (0, 0)) -> dict[str, object]:
    directory = output / name
    directory.mkdir(parents=True, exist_ok=True)
    for index, frame in enumerate(frames):
        frame.save(directory / f"frame_{index:02}.png")
    scale = 4
    sheet = Image.new("RGBA", (sum(frame.width for frame in frames) * scale,
                               max(frame.height for frame in frames) * scale),
                      (0, 0, 0, 0))
    x = 0
    for frame in frames:
        enlarged = frame.resize((frame.width * scale, frame.height * scale),
                                Image.Resampling.NEAREST)
        sheet.alpha_composite(enlarged, (x, 0)); x += enlarged.width
    sheet.save(output / f"{name}_sheet_4x.png")
    return {"name": name, "width": frames[0].width, "height": frames[0].height,
            "frames": len(frames), "origin": list(origin)}


def export_color(dos_dir: Path, output: Path) -> None:
    """Export indexed DOS sprites before the Playdate monochrome conversion."""
    ground_sets = [GroundSet.load(dos_dir, index) for index in range(5)]
    main = list(sections((dos_dir / "main.dat").read_bytes()))[0].payload
    metadata: dict[str, object] = {"source": str(dos_dir), "palettes": {},
                                  "lemmings": {}, "objects": []}
    output.mkdir(parents=True, exist_ok=True)

    # The first eight lemming colors are shared; 4-bit animations (notably
    # burning) use the theme-dependent upper eight colors. Export each theme so
    # every frame is shown with the palette the DOS game uses on that level.
    for style, ground in enumerate(ground_sets):
        style_name = STYLE_NAMES[style]
        metadata["palettes"][style_name] = [list(color) for color in ground.object_palette]
        style_output = output / "lemmings" / style_name
        style_records = []
        for slot, width, height, bpp, source, frame_count, ox, oy in SPECS:
            frame_size = ((width * height + 7) // 8) * bpp
            frames = []
            for frame in range(frame_count):
                pixels = planar(main, source + frame * frame_size, width, height, bpp)
                mask = bytes(bool(value) for value in pixels)
                frames.append(indexed_frame(pixels, mask, width, height,
                                            ground.object_palette))
            name = f"{LEMMING_NAMES[slot // 2]}_{'right' if slot % 2 == 0 else 'left'}"
            style_records.append(save_color_group(frames, name, style_output, (ox, oy)))
        metadata["lemmings"][style_name] = style_records

    for style, ground in enumerate(ground_sets):
        style_name = STYLE_NAMES[style]
        for object_id, info in enumerate(ground.objects):
            if not info.width or not info.height or not info.frame_count:
                continue
            frames = []
            for frame in range(info.frame_count):
                source = info.image_offset + frame * info.frame_size
                pixels = planar(ground.object_data, source, info.width, info.height, 4)
                count = info.width * info.height
                mask = bytes(bool(ground.object_data[source + info.mask_offset + p // 8] &
                                  (0x80 >> (p & 7))) for p in range(count))
                frames.append(indexed_frame(pixels, mask, info.width, info.height,
                                            ground.object_palette))
            name = f"{style_name}_object_{object_id:02}"
            metadata["objects"].append(save_color_group(
                frames, name, output / "objects" / style_name))
    (output / "sprites.json").write_text(json.dumps(metadata, indent=2) + "\n")


def export(pack_path: Path, output: Path) -> None:
    pack = pack_path.read_bytes()
    header = HEADER.unpack_from(pack)
    sprite_offset, sprite_size = header[7], header[8]
    atlas = pack[sprite_offset:sprite_offset + sprite_size]
    magic, lemming_count, object_count, record_size, _reserved, total = ATLAS_HEADER.unpack_from(atlas)
    if (magic, lemming_count, object_count, record_size, total) != (
            b"LPS3", SLOT_COUNT, OBJECT_SLOT_COUNT, ATLAS_RECORD.size, len(atlas)):
        raise ValueError("unsupported sprite atlas")
    records = [ATLAS_RECORD.unpack_from(atlas, ATLAS_HEADER.size + i * record_size)
               for i in range(lemming_count + object_count)]
    lemming_names = [f"{LEMMING_NAMES[i // 2]}_{'right' if i % 2 == 0 else 'left'}"
                     for i in range(lemming_count)]
    object_names = [f"{STYLE_NAMES[i // 16]}_object_{i % 16:02}"
                    for i in range(object_count)]
    output.mkdir(parents=True, exist_ok=True)
    metadata = {
        "source": str(pack_path),
        "lemmings": save_group(atlas, records[:lemming_count], lemming_names,
                                output / "lemmings"),
        "objects": save_group(atlas, records[lemming_count:], object_names,
                               output / "objects"),
    }
    (output / "sprites.json").write_text(json.dumps(metadata, indent=2) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("pack", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--dos-dir", type=Path,
                        help="export original indexed color sprites from this DOS directory")
    args = parser.parse_args()
    if args.dos_dir:
        export_color(args.dos_dir, args.output)
    else:
        export(args.pack, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
