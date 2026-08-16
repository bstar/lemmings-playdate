"""Advance Playdate display and sideload build versions together."""

from __future__ import annotations

from pathlib import Path
import re
import sys


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) == 2 else None
    if path is None or not path.is_file():
        raise SystemExit("usage: bump_playdate_version.py path/to/pdxinfo")
    text = path.read_text(encoding="utf-8")
    version_match = re.search(r"(?m)^version=(\d+)\.(\d+)\.(\d+)$", text)
    build_match = re.search(r"(?m)^buildNumber=(\d+)$", text)
    if not version_match or not build_match:
        raise SystemExit("pdxinfo requires semantic version and integer buildNumber")
    major, minor, patch = map(int, version_match.groups())
    build = int(build_match.group(1))
    version = f"{major}.{minor}.{patch + 1}"
    text = text[:version_match.start()] + f"version={version}" + text[version_match.end():]
    build_match = re.search(r"(?m)^buildNumber=(\d+)$", text)
    assert build_match is not None
    text = text[:build_match.start()] + f"buildNumber={build + 1}" + text[build_match.end():]
    path.write_text(text, encoding="utf-8")
    print(f"Playdate release {version} (build {build + 1})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
