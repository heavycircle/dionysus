#!/usr/bin/env python3

"""Merge many compile_commands.json into a single file."""

import json
import sys
from pathlib import Path
from typing import Any


def merge_compdb(input_files: list[Path], output_file: Path) -> int:
    """Merge multiple compile_commands.json files into one.

    Args:
        input_files (list[Path]): List of compile_commands.json to merge.
        output_file (Path): Where to store merged compile_commands.json.

    Returns:
        int: 0 on success, 1 on error.

    """
    merged: list[dict[str, Any]] = []
    seen_files: set[tuple[str, str]] = set()
    processed = 0

    for input_file in input_files:
        if not input_file.exists():
            print(f"  {input_file} does not exist, skipping ...", file=sys.stderr)
            continue

        try:
            with input_file.open(encoding="utf-8") as f:
                commands: list[dict[str, Any]] | dict[str, Any] = json.load(f)

            if not isinstance(commands, list):
                print(
                    f"  {input_file} does not contain a list, skipping ...",
                    file=sys.stderr,
                )
                continue

            added = 0
            for command in commands:
                file_path: str = command.get("file", "")
                directory: str = command.get("directory", "")
                key = (file_path, directory)
                if key not in seen_files:
                    merged.append(command)
                    seen_files.add(key)
                    added += 1

            processed = len(commands)
            print(
                f"  {input_file.name:30s}: {added:4d} unique / {processed:4d} total entries"
            )
        except json.JSONDecodeError as e:
            print(f"  Error parsing {input_file}: {e}", file=sys.stderr)
        except Exception as e:
            print(f"  Error parsing {input_file}: {e}", file=sys.stderr)

    if not merged:
        print("  No compile commands to merge!", file=sys.stderr)
        return 1

    try:
        output_file.parent.mkdir(parents=True, exist_ok=True)
        with output_file.open("w", encoding="utf-8") as f:
            json.dump(merged, f, indent=2)

        print(f"  Merged {processed} unique entries!")
        print(f"  Merged file written to: {output_file}")
        return 0
    except Exception as e:
        print(f"  Error writing output file: {e}", file=sys.stderr)
        return 1


def main() -> int:
    """Program driver."""
    if len(sys.argv) < 3:
        print(
            "Usage: merge_compdb <binary_dir> <output_file> [project1] [project2] ..."
        )
        print()
        print("Arguments:")
        print("  binary_dir  : Root binary directory containing project subdirectories")
        print("  output_file : Path where compile_commands.json will be written")
        print("  projectX    : Optional list of project names")
        print()
        return 1

    binary_dir = Path(sys.argv[1])
    output_file = Path(sys.argv[2])
    project_names = sys.argv[3:] if len(sys.argv) > 3 else ["stdlib", "sample_apps"]

    input_files: list[Path] = []
    for project in project_names:
        compile_commands_path = binary_dir / project / "compile_commands.json"
        input_files.append(compile_commands_path)
    return merge_compdb(input_files, output_file)


if __name__ == "__main__":
    sys.exit(main())

