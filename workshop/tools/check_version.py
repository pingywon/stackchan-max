#!/usr/bin/env python3
"""One version number, defined in one place.

The firmware version is set once, in `firmware/CMakeLists.txt` (`PROJECT_VER`). ESP-IDF
stamps it into the program, the portal asks the running firmware for it, and that is
the string the robot's own page and boot log show. Nothing on the robot can drift.

The README, the status page and the kit texts also quote the version, and plain text
cannot read a CMake file. This script does it for them:

    python3 workshop/tools/check_version.py           # fail if any quoted version differs
    python3 workshop/tools/check_version.py --write   # rewrite the quoted versions to match

Run the check before a build, and `--write` right after changing PROJECT_VER.
"""

from __future__ import annotations

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SOURCE = os.path.join("firmware", "CMakeLists.txt")

# Never scanned: downloaded or generated trees, and upstream's separate projects.
SKIP_DIRS = {".git", "build", "managed_components", "components", "xiaozhi-esp32", "lvgl",
             "out", "release", "__pycache__", ".venv", "app", "remote"}
TEXT_EXT = {".md", ".txt", ".bat", ".ps1", ".py", ".html", ".json", ".yml", ".sh", ".h",
            ".cpp", ".cc", ".c", ".csv", ".defaults"}
# These must quote the version; the check fails if one of them has none.
MUST_QUOTE = ["README.md", os.path.join("docs", "STATUS.md")]


def read_version(root: str) -> str:
    text = open(os.path.join(root, SOURCE), encoding="utf-8").read()
    match = re.search(r'set\(\s*PROJECT_VER\s+"([^"]+)"\s*\)', text)
    if not match:
        sys.exit(f"no PROJECT_VER in {SOURCE}")
    return match.group(1)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write", action="store_true", help="rewrite quoted versions to match")
    ap.add_argument("--root", default=DEFAULT_ROOT, help=argparse.SUPPRESS)
    args = ap.parse_args()

    version = read_version(args.root)
    number, dash, suffix = version.partition("-")
    if not dash:
        sys.exit(f"PROJECT_VER {version!r} has no '-suffix'; cannot tell it apart from other numbers")
    quoted = re.compile(r"\d+\.\d+\.\d+-" + re.escape(suffix) + r"\b")

    wrong, seen = [], {}
    for folder, dirs, files in os.walk(args.root):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
        for name in sorted(files):
            path = os.path.join(folder, name)
            rel = os.path.relpath(path, args.root)
            if rel == SOURCE or os.path.splitext(name)[1].lower() not in TEXT_EXT:
                continue
            try:
                text = open(path, encoding="utf-8", newline="").read()
            except (UnicodeDecodeError, OSError):
                continue
            found = quoted.findall(text)
            if not found:
                continue
            seen[rel] = len(found)
            bad = sorted({f for f in found if f != version})
            if bad and args.write:
                open(path, "w", encoding="utf-8", newline="").write(quoted.sub(version, text))
                print(f"  fixed  {rel}: {', '.join(bad)} -> {version}")
            elif bad:
                wrong.append((rel, bad))

    print(f"version {version}  (from {SOURCE})")
    for rel, count in sorted(seen.items()):
        print(f"  quoted {count}x in {rel}")
    missing = [m for m in MUST_QUOTE if m not in seen]
    for rel in missing:
        print(f"  MISSING: {rel} does not state the version")
    for rel, bad in wrong:
        print(f"  WRONG: {rel} says {', '.join(bad)}")
    if wrong or missing:
        print("version check FAILED" + ("" if missing else " — run with --write to fix"))
        return 1
    print("version check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
