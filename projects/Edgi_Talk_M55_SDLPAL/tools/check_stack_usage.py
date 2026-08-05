#!/usr/bin/env python3
"""Reject oversized or unbounded SDLPal stack frames from GCC .su files."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import pathlib
import re
import sys
from typing import List, Sequence, Tuple


@dataclass(frozen=True)
class StackRecord:
    source: str
    line: int
    column: int
    function: str
    bytes: int
    qualifier: str


RECORD_PATTERN = re.compile(
    r"^(?P<source>.*):(?P<line>\d+):(?P<column>\d+):"
    r"(?P<function>[^\t]+)\t(?P<bytes>\d+)\t(?P<qualifier>[^\t]+)$"
)


def parse_stack_usage(text: str) -> List[StackRecord]:
    records: List[StackRecord] = []
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.strip()
        if not line:
            continue
        match = RECORD_PATTERN.match(line)
        if match is None:
            raise ValueError(f"invalid GCC stack-usage record at line {line_number}")
        records.append(
            StackRecord(
                source=match.group("source"),
                line=int(match.group("line")),
                column=int(match.group("column")),
                function=match.group("function"),
                bytes=int(match.group("bytes")),
                qualifier=match.group("qualifier"),
            )
        )
    return records


def collect_stack_usage(
    root: pathlib.Path,
) -> Tuple[List[pathlib.Path], List[StackRecord]]:
    files = sorted(root.rglob("*.su")) if root.is_dir() else []
    if not files:
        raise ValueError(f"no GCC stack-usage reports under {root}")

    records: List[StackRecord] = []
    for path in files:
        records.extend(parse_stack_usage(path.read_text(encoding="utf-8")))
    if not records:
        raise ValueError(f"GCC stack-usage reports under {root} are empty")
    return files, records


def validate_records(records: Sequence[StackRecord], limit: int) -> List[str]:
    errors: List[str] = []
    for record in records:
        qualifiers = {part.strip() for part in record.qualifier.split(",")}
        if "dynamic" in qualifiers and "bounded" not in qualifiers:
            errors.append(
                f"{record.function} has unbounded dynamic stack usage"
            )
        if record.bytes > limit:
            errors.append(
                f"{record.function} uses {record.bytes} bytes (limit {limit})"
            )
    return errors


def _parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=pathlib.Path)
    parser.add_argument("--limit", required=True, type=int)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parse_arguments(sys.argv[1:] if argv is None else argv)
    if arguments.limit <= 0:
        print("FAIL: --limit must be greater than zero")
        return 1

    try:
        files, records = collect_stack_usage(arguments.root)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"FAIL: {error}")
        return 1

    largest = sorted(records, key=lambda record: record.bytes, reverse=True)
    for record in largest[:10]:
        print(
            f"{record.bytes:8d}  {record.function}  "
            f"{record.source}:{record.line}"
        )

    errors = validate_records(records, arguments.limit)
    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1

    maximum = largest[0]
    print(
        f"PASS files={len(files)} functions={len(records)} "
        f"max={maximum.bytes} function={maximum.function} "
        f"limit={arguments.limit}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
