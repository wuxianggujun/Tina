"""Check first-party C++ scalar types without rejecting standard-library containers.

This is a lexical policy check, not a C++ parser. All preprocessor branches are
checked; comments and string/character literals (including raw strings) are not.
The only standard scalar exceptions are Core's definitions, explicit type-identity
assertions, and third-party API fakes that must remain independent of Tina.
"""

from __future__ import annotations

import argparse
from bisect import bisect_right
from collections import Counter
from dataclasses import asdict, dataclass
import json
import os
from pathlib import Path
import re


SCALAR_ALIASES = {
    "int8_t": "i8", "uint8_t": "u8",
    "int16_t": "i16", "uint16_t": "u16",
    "int32_t": "i32", "uint32_t": "u32",
    "int64_t": "i64", "uint64_t": "u64",
    "ptrdiff_t": "isize", "size_t": "usize", "uintptr_t": "uintptr",
}
SOURCE_ROOTS = ("include", "src", "editor", "samples", "tests", "tools", "shared", "cmake")
SOURCE_SUFFIXES = (".h", ".hpp", ".hxx", ".c", ".cc", ".cpp", ".cxx", ".m", ".mm", ".cpp.in", ".hpp.in")
EXCLUDED_DIRECTORIES = frozenset({
    ".git", "node_modules", "build", "out", "dist", ".gradle", ".gradle-user-home-ai",
    ".tmp", ".cache", ".kotlin", ".cxx", ".externalNativeBuild", ".vite", ".turbo",
    "coverage", "captures", "logs", "log", "tmp", "temp", "artifacts", "thirdparty",
    "dependencies", "__pycache__",
})
# These headers model the external ABI. Including Tina types here would hide an
# accidental Tina dependency in the backend's third-party boundary tests.
EXCLUDED_PREFIXES = ("tests/render_bgfx/fakes/",)
TYPES_HEADER = "include/tina/core/base/Types.hpp"
IDENTITY_TEST = "tests/core/TypesTests.cpp"

TYPE_NAMES = "|".join(SCALAR_ALIASES)
STANDARD_TYPE = re.compile(r"\bstd\s*::\s*(?P<type>" + TYPE_NAMES + r")\b")
TYPE_DEFINITION = re.compile(
    r"\busing\s+(?P<alias>\w+)\s*=\s*(?P<standard>std\s*::\s*(?P<type>" + TYPE_NAMES + r"))\s*;"
)
TYPE_IDENTITY = re.compile(
    r"\bstatic_assert\s*\(\s*std\s*::\s*is_same_v\s*<\s*"
    r"(?:(?:Tina::)?Core::|Tina::)?(?P<alias>\w+)\s*,\s*"
    r"(?P<standard>std\s*::\s*(?P<type>" + TYPE_NAMES + r"))\s*>\s*\)\s*;"
)
LINE_SPLICE = re.compile(r"\\\r?\n")
NON_CODE = re.compile(
    r'//[^\n]*|/\*[\s\S]*?(?:\*/|$)'
    r'|(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=delimiter)"'
    r'|(?:u8|u|U|L)?"(?:\\[^\r\n]|[^"\\\r\n])*"'
    # A quote inside a numeric token is a digit separator, not a character literal.
    r"|(?<![\w'])(?:u8|u|U|L)?'(?:\\[^\r\n]|[^'\\\r\n])*'"
)


@dataclass(frozen=True)
class Violation:
    path: str
    line: int
    column: int
    standard_type: str
    core_type: str


def code_view(source: str) -> tuple[str, list[int], list[int]]:
    """Splice physical lines, then mask literals/comments; retain source locations."""
    pieces: list[str] = []
    splice_offsets: list[int] = []
    removed_bytes: list[int] = []
    previous = removed = 0
    for match in LINE_SPLICE.finditer(source):
        pieces.append(source[previous:match.start()])
        splice_offsets.append(match.start() - removed)
        removed += match.end() - match.start()
        removed_bytes.append(removed)
        previous = match.end()
    pieces.append(source[previous:])
    logical = "".join(pieces)
    masked = NON_CODE.sub(lambda match: re.sub(r"[^\r\n]", " ", match.group()), logical)
    return masked, splice_offsets, removed_bytes


def check_source(relative: str, source: str) -> list[Violation]:
    code, splice_offsets, removed_bytes = code_view(source)
    exceptions: set[tuple[int, int]] = set()
    allowed = TYPE_DEFINITION if relative == TYPES_HEADER else TYPE_IDENTITY if relative == IDENTITY_TEST else None
    if allowed is not None:
        for match in allowed.finditer(code):
            if SCALAR_ALIASES[match["type"]] == match["alias"]:
                exceptions.add(match.span("standard"))
    line_starts = [0, *(match.end() for match in re.finditer("\n", source))]
    violations = []
    for match in STANDARD_TYPE.finditer(code):
        if match.span() in exceptions:
            continue
        splice = bisect_right(splice_offsets, match.start())
        offset = match.start() + (removed_bytes[splice - 1] if splice else 0)
        line = bisect_right(line_starts, offset)
        violations.append(Violation(
            relative, line, offset - line_starts[line - 1] + 1,
            f"std::{match['type']}", f"Tina::Core::{SCALAR_ALIASES[match['type']]}",
        ))
    return violations


def source_files(root: Path):
    for scope in SOURCE_ROOTS:
        base = root / scope
        if base.is_symlink():
            continue
        for directory, directories, files in os.walk(base):
            directories[:] = sorted(
                name for name in directories
                if name not in EXCLUDED_DIRECTORIES and not (Path(directory) / name).is_symlink()
            )
            for name in sorted(files):
                path = Path(directory) / name
                relative = path.relative_to(root).as_posix()
                if (name.endswith(SOURCE_SUFFIXES) and not path.is_symlink()
                        and not relative.startswith(EXCLUDED_PREFIXES)):
                    yield path, relative


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--json", type=Path, help="Optional UTF-8 audit report")
    arguments = parser.parse_args()
    root = arguments.root.resolve(strict=True)
    if not (root / TYPES_HEADER).is_file():
        parser.error("--root must name the Tina source tree")
    violations: list[Violation] = []
    scanned = 0
    try:
        for path, relative in source_files(root):
            violations.extend(check_source(relative, path.read_text(encoding="utf-8-sig")))
            scanned += 1
    except (OSError, UnicodeError) as error:
        parser.exit(2, f"Core type audit could not read a source file: {error}\n")
    for entry in violations:
        print(f"{entry.path}:{entry.line}:{entry.column}: use {entry.core_type}, not {entry.standard_type}")
    summary = {
        "schemaVersion": 1,
        "scannedFiles": scanned,
        "violationCount": len(violations),
        "filesWithViolations": len({entry.path for entry in violations}),
        "byType": dict(sorted(Counter(entry.standard_type for entry in violations).items())),
        "violations": [asdict(entry) for entry in violations],
    }
    if arguments.json:
        arguments.json.parent.mkdir(parents=True, exist_ok=True)
        arguments.json.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Core types: {scanned} source files checked, {len(violations)} violations.")
    return int(bool(violations))


if __name__ == "__main__":
    raise SystemExit(main())
