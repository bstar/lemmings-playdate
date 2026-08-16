#!/usr/bin/env python3
"""Verify the preservation inputs against the recorded SHA-256 manifest."""

from __future__ import annotations

import hashlib
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "reference"
MANIFEST = Path(__file__).with_name("reference.sha256")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def main() -> int:
    expected = {}
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        checksum, name = line.split("  ", 1)
        expected[name] = checksum

    actual_names = {
        str(path.relative_to(ROOT))
        for path in REFERENCE.rglob("*")
        if path.is_file() and path.name != ".DS_Store"
    }
    errors = []
    for name, checksum in sorted(expected.items()):
        path = ROOT / name
        if not path.is_file():
            errors.append(f"missing: {name}")
        elif digest(path) != checksum:
            errors.append(f"changed: {name}")
    for name in sorted(actual_names - expected.keys()):
        errors.append(f"unrecorded: {name}")

    if errors:
        print("Reference verification failed:", file=sys.stderr)
        print("\n".join(f"  {error}" for error in errors), file=sys.stderr)
        return 1
    print(f"Verified {len(expected)} preservation files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

