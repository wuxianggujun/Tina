#!/usr/bin/env python3
"""Bake only glyphs shaped from a UTF-8 UI string manifest using Tina's msdfgen host tool."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--msdfgen", required=True, type=Path, help="Path to the host-built tina_msdfgen executable")
    parser.add_argument("--font", required=True, type=Path)
    parser.add_argument("--strings", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path, help="Output stem; produces .png, .json and .tmsdf")
    args = parser.parse_args()
    strings = args.strings.read_text(encoding="utf-8-sig")
    if "\x00" in strings or len(strings.encode("utf-8")) > 65536:
        parser.error("UI string manifest must be UTF-8 without NUL and at most 65536 bytes")
    # Deduplicate complete lines, not scalars: contextual Arabic/Indic and Emoji
    # sequences must survive. No Unicode range/charmap enumeration is allowed.
    strings = "\n".join(dict.fromkeys(strings.splitlines()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Generation/validation finishes before publication. The authoritative
    # runtime .tmsdf is published last; PNG/JSON are inspection companions.
    with tempfile.TemporaryDirectory(prefix="tina-font-", dir=args.output.parent) as directory:
        temporary = Path(directory)
        manifest = temporary / "strings.txt"
        manifest.write_text(strings, encoding="utf-8", newline="\n")
        stem = temporary / "font"
        subprocess.run([str(args.msdfgen.resolve()), "--font", str(args.font.resolve()),
                        "--strings", str(manifest.resolve()), "--output", str(stem.resolve())], check=True,
                       encoding="utf-8", errors="strict", shell=False)
        metadata = json.loads(stem.with_suffix(".json").read_text(encoding="utf-8"))
        if metadata["schemaVersion"] != 1 or metadata["type"] != "msdf" or metadata["glyphCount"] > 4096:
            raise ValueError("msdfgen returned incompatible or over-budget metadata")
        if not stem.with_suffix(".png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
            raise ValueError("msdfgen did not produce a PNG atlas")
        for suffix in (".png", ".json", ".tmsdf"):
            os.replace(stem.with_suffix(suffix), args.output.with_suffix(suffix))


if __name__ == "__main__":
    main()
