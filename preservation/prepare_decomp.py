"""Reproducibly unpack and analyze the private DOS VGA executable."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
INPUT = ROOT / "reference" / "lemming1.pc" / "vgalemmi.exe"
OUTPUT = ROOT / "preservation" / "private"
DEPKLITE_URL = "https://github.com/hackerb9/depklite.git"
DEPKLITE_COMMIT = "83a11e57961cc95af0fdfebeb3759f82e0c11abe"


def headless_path() -> Path:
    configured = os.environ.get("GHIDRA_HEADLESS")
    candidates = [Path(configured)] if configured else []
    found = shutil.which("analyzeHeadless")
    if found:
        candidates.append(Path(found))
    candidates += list(Path("/opt/homebrew/Cellar/ghidra").glob("*/libexec/support/analyzeHeadless"))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit("Ghidra analyzeHeadless not found; set GHIDRA_HEADLESS")


def main() -> int:
    data = INPUT.read_bytes()
    header_paragraphs = struct.unpack_from("<H", data, 8)[0]
    decoder_paragraphs = data[header_paragraphs * 16 + 0x4E]
    compressed_offset = (header_paragraphs + decoder_paragraphs - 0x10) * 16
    OUTPUT.mkdir(parents=True, exist_ok=True)
    raw = OUTPUT / "vgalemmi.unpacked.raw"
    with tempfile.TemporaryDirectory(prefix="lemmings-depklite-") as temporary:
        source = Path(temporary) / "depklite"
        subprocess.run(["git", "clone", "--quiet", DEPKLITE_URL, str(source)], check=True)
        subprocess.run(["git", "-C", str(source), "checkout", "--quiet", DEPKLITE_COMMIT], check=True)
        tool = source / "depklite-bin"
        # depklite calls asprintf with a non-literal format. Toolchains that
        # enable -Wformat-security hardening by default, such as the Nix
        # compiler wrappers, promote that to an error and refuse to build a
        # third-party source we do not modify.
        subprocess.run([os.environ.get("CC", "cc"), "-O2", "-Wno-error=format-security",
                        str(source / "depklite.c"), "-o", str(tool)], check=True)
        subprocess.run([str(tool), "--decrypt", "--output", str(raw), str(INPUT), str(compressed_offset)], check=True)
    if raw.stat().st_size != 82906:
        raise SystemExit(f"unexpected unpacked size: {raw.stat().st_size}")
    with (OUTPUT / "vgalemmi.ndisasm.txt").open("w", encoding="ascii") as disassembly:
        subprocess.run(["ndisasm", "-b", "16", str(raw)], stdout=disassembly, check=True)
    project = OUTPUT / "ghidra"
    project.mkdir(parents=True, exist_ok=True)
    script_dir = ROOT / "preservation" / "ghidra_scripts"
    java_environment = os.environ.copy()
    java_environment.setdefault("JAVA_HOME", "/opt/homebrew/opt/openjdk@21")
    subprocess.run([str(headless_path()), str(project), "Lemmings",
                    "-import", str(raw), "-loader", "BinaryLoader",
                    "-processor", "x86:LE:16:Real Mode", "-overwrite",
                    "-scriptPath", str(script_dir), "-preScript", "SeedEntry.java",
                    "-postScript", "ExportFunctions.java",
                    str(OUTPUT / "vgalemmi.decompiled.c")], env=java_environment, check=True)
    print(f"Private decompilation workspace written to {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
