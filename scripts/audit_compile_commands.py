#!/usr/bin/env python3
"""Fail unless every project production C++ TU is in the build database."""

from __future__ import annotations

import argparse
import collections
import json
import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "scripts" / "project-translation-units.txt"
SOURCE_ROOTS = (
    ROOT / "src",
    ROOT / "libraries" / "EspressoScaleBLE" / "src",
    ROOT / "idf" / "main",
    ROOT / "idf" / "components",
)


def relative_project_path(path: pathlib.Path) -> str | None:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return None


def manifest_paths(path: pathlib.Path) -> set[str]:
    entries: set[str] = set()
    for raw in path.read_text(encoding="utf-8").splitlines():
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        if entry in entries:
            raise ValueError(f"duplicate manifest entry: {entry}")
        entries.add(entry)
    return entries


def disk_paths() -> set[str]:
    entries: set[str] = set()
    for source_root in SOURCE_ROOTS:
        for path in source_root.rglob("*.cpp"):
            relative = relative_project_path(path)
            if relative is None or "/tests/" in f"/{relative}/":
                continue
            entries.add(relative)
    return entries


def database_paths(path: pathlib.Path) -> collections.Counter[str]:
    database = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(database, list):
        raise ValueError("compile database root must be an array")
    entries: collections.Counter[str] = collections.Counter()
    for item in database:
        if not isinstance(item, dict) or not isinstance(item.get("file"), str):
            raise ValueError("compile database entry lacks a string file field")
        source = pathlib.Path(item["file"])
        if not source.is_absolute():
            directory = pathlib.Path(item.get("directory", ROOT))
            source = directory / source
        relative = relative_project_path(source)
        if relative is not None:
            entries[relative] += 1
    return entries


def print_set(label: str, entries: set[str]) -> None:
    if entries:
        print(f"{label}:", file=sys.stderr)
        for entry in sorted(entries):
            print(f"  {entry}", file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", type=pathlib.Path, required=True)
    parser.add_argument("--manifest", type=pathlib.Path, default=DEFAULT_MANIFEST)
    args = parser.parse_args()

    try:
        expected = manifest_paths(args.manifest)
        on_disk = disk_paths()
        database = database_paths(args.database)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"translation-unit audit failed: {error}", file=sys.stderr)
        return 2

    database_set = set(database)
    disk_missing = expected - on_disk
    disk_untracked = on_disk - expected
    database_missing = expected - database_set
    database_untracked = (database_set & on_disk) - expected
    duplicates = {entry for entry, count in database.items() if count != 1 and entry in expected}

    print_set("manifest entries missing on disk", disk_missing)
    print_set("production TUs absent from manifest", disk_untracked)
    print_set("manifest entries missing from compile database", database_missing)
    print_set("unexpected project TUs in compile database", database_untracked)
    print_set("project TUs occurring other than once", duplicates)
    if any((disk_missing, disk_untracked, database_missing,
            database_untracked, duplicates)):
        return 1

    print(
        f"translation-unit coverage: {len(expected)}/{len(expected)} "
        f"production C++ files, each compiled exactly once"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
