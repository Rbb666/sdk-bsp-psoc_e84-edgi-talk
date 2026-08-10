#!/usr/bin/env python3
"""Reject foreign-platform identifiers in SDLPal production files."""

from __future__ import annotations

import pathlib
import sys


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE_ROOTS = (
    "sdlpal",
    "platform",
    "audio",
    "applications",
    "board",
    "tests",
)
ROOT_FILES = ("SConstruct", "SConscript", "Kconfig", "README.md")
SOURCE_SUFFIXES = {".c", ".h", ".cpp", ".inc", ".py", ".md", ".ld"}
BUILD_FILENAMES = {"SConstruct", "SConscript", "Kconfig", "Makefile"}
FORBIDDEN = (
    "esp" + "32",
    "esp" + "ressif",
    "esp_" + "platform",
    "esp_" + "attr.h",
    "native" + "_engine_shim",
)


def production_files() -> list[pathlib.Path]:
    files = [PROJECT_ROOT / name for name in ROOT_FILES]
    for root_name in SOURCE_ROOTS:
        root = PROJECT_ROOT / root_name
        files.extend(
            path
            for path in root.rglob("*")
            if path.is_file()
            and (
                path.suffix.lower() in SOURCE_SUFFIXES
                or path.name in BUILD_FILENAMES
            )
        )
    return sorted(set(files))


def main() -> int:
    matches: list[str] = []
    for path in production_files():
        text = path.read_text(encoding="utf-8", errors="replace")
        for line_number, line in enumerate(text.splitlines(), start=1):
            folded = line.casefold()
            if any(token in folded for token in FORBIDDEN):
                relative = path.relative_to(PROJECT_ROOT)
                matches.append(f"{relative}:{line_number}:{line.strip()}")
    if matches:
        print("Foreign-platform references found:", file=sys.stderr)
        print("\n".join(matches), file=sys.stderr)
        return 1
    print("platform_clean: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
