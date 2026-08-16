"""Command-line interface for the private asset compiler."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

from .graphics import GroundSet, compose
from .levels import canonical_levels, load_oddtable, load_unique, RATINGS
from .pack import validate, write_pack
from .crunch import sections


REQUIRED = tuple(
    [f"level{index:03}.dat" for index in range(10)]
    + [f"ground{index}o.dat" for index in range(5)]
    + [f"vgagr{index}.dat" for index in range(5)]
    + [f"vgaspec{index}.dat" for index in range(4)]
    + ["oddtable.dat", "main.dat", "adlib.dat"]
)


def source_digest(directory: Path) -> bytes:
    digest = hashlib.sha256()
    for name in REQUIRED:
        path = directory / name
        if not path.is_file():
            raise FileNotFoundError(path)
        digest.update(name.encode("ascii"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
    return digest.digest()


def build(directory: Path, output: Path) -> dict[str, object]:
    unique = load_unique(directory)
    levels = canonical_levels(unique, load_oddtable(directory / "oddtable.dat"))
    if len(unique) != 80 or len(levels) != 120:
        raise ValueError(f"expected 80 unique and 120 ordered levels, got {len(unique)} and {len(levels)}")
    ground_sets = [GroundSet.load(directory, index) for index in range(5)]
    planes = [compose(level, directory, ground_sets) for level in levels]
    digest = source_digest(directory)
    write_pack(output, levels, planes, digest, ground_sets, directory)
    adlib_sections = list(sections((directory / "adlib.dat").read_bytes()))
    if len(adlib_sections) != 1 or len(adlib_sections[0].payload) != 22125:
        raise ValueError("unexpected DOS v1 ADLIB.DAT structure")
    adlib_output = output.with_name("adlib.bin")
    adlib_output.write_bytes(adlib_sections[0].payload)
    report = validate(output)
    report.update({
        "ratings": {name: [level.name for level in levels if level.rating == index]
                    for index, name in enumerate(RATINGS)},
        "unique_layouts": len(unique),
        "adlib_bytes": len(adlib_sections[0].payload),
    })
    report_path = output.with_suffix(output.suffix + ".json")
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(prog="assetc")
    commands = result.add_subparsers(dest="command", required=True)
    command = commands.add_parser("build", help="compile original DOS data")
    command.add_argument("--dos-dir", type=Path, required=True)
    command.add_argument("--out", type=Path, required=True)
    check = commands.add_parser("validate", help="validate an LPD1 pack")
    check.add_argument("pack", type=Path)
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        report = build(args.dos_dir, args.out) if args.command == "build" else validate(args.pack)
    except (OSError, ValueError) as error:
        print(f"assetc: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2))
    return 0
