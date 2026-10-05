#!/usr/bin/env python3
"""Assemble a flashable Windows kit from a finished build.

The kit is what actually gets handed over: the five partition images at their correct
offsets, the scripts that write them, and a bundled esptool so the Windows machine doing
the flashing needs nothing installed. The archive is built with the standard library, so
no `zip` program is needed.

    python3 workshop/kit/make_release_zip.py --version 1.7.1 --esptool-dir /path/to/esptool

Writes release/stackychan-kit-v<version>.zip under the repo (change with --outdir).

`esptool.exe` is not in the repo. Download the Windows build named in README.md and point
--esptool-dir at the folder holding `esptool.exe` and its licence file (default: a `tools/`
folder beside this script). The two PowerShell helpers are taken from this directory.

Add --stock /path/to/StackChan-UserDemo.bin to include M5Stack's factory image and
5-RESTORE-STOCK.bat, which makes it a full recovery kit.

Note the partition table is included and written at 0x8000. That is deliberate — it is
the only way to ship a layout change — but it also means a release whose partition table
differs from the installed one cannot be applied over the air.
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
BUILD = os.path.join(REPO, "firmware", "build")

# name in the kit -> path in the build tree
IMAGES = {
    "bootloader.bin": os.path.join(BUILD, "bootloader", "bootloader.bin"),
    "partition-table.bin": os.path.join(BUILD, "partition_table", "partition-table.bin"),
    "ota_data_initial.bin": os.path.join(BUILD, "ota_data_initial.bin"),
    "stack-chan.bin": os.path.join(BUILD, "stack-chan.bin"),
    "generated_assets.bin": os.path.join(BUILD, "generated_assets.bin"),
}

SCRIPTS = ["1-DIAGNOSE.bat", "2-ERASE.bat", "3-FLASH.bat", "4-BOOTLOG.bat",
           "6-FIND-DEVICE.bat", "START-HERE.txt"]
RESTORE_SCRIPT = "5-RESTORE-STOCK.bat"
STOCK_NAME = "StackChan-UserDemo.bin"        # the name 5-RESTORE-STOCK.bat looks for

# Kept in this directory.
HELPERS = ["finddevice.ps1", "serialmon.ps1"]
# Large, separately licensed, and not in the repo.
ESPTOOL_FILES = ["esptool.exe", "esptool-LICENSE.txt"]


def to_crlf(src: str, dst: str) -> None:
    """Windows will run a LF batch file, but its error messages get strange."""
    with open(src, "rb") as fh:
        body = fh.read().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
    with open(dst, "wb") as fh:
        fh.write(body)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", required=True, help="e.g. 1.7.1")
    ap.add_argument("--outdir", default=os.path.join(REPO, "release"))
    ap.add_argument("--esptool-dir", default=os.path.join(HERE, "tools"),
                    help="folder holding esptool.exe and esptool-LICENSE.txt")
    ap.add_argument("--stock", default="",
                    help="M5Stack's merged factory image; adds 5-RESTORE-STOCK.bat")
    args = ap.parse_args()

    name = f"stackychan-kit-v{args.version}"
    staging = os.path.join(args.outdir, name)
    archive = os.path.join(args.outdir, name + ".zip")

    missing = [p for p in IMAGES.values() if not os.path.exists(p)]
    if missing:
        print("Build these first — missing:")
        for p in missing:
            print("  " + p)
        return 1
    if args.stock and not os.path.exists(args.stock):
        print(f"--stock: no such file: {args.stock}")
        return 1

    shutil.rmtree(staging, ignore_errors=True)
    os.makedirs(os.path.join(staging, "bin"))
    os.makedirs(os.path.join(staging, "tools"))

    for target, source in IMAGES.items():
        shutil.copy2(source, os.path.join(staging, "bin", target))

    scripts = SCRIPTS + ([RESTORE_SCRIPT] if args.stock else [])
    for script in scripts:
        to_crlf(os.path.join(HERE, script), os.path.join(staging, script))

    for helper in HELPERS:
        shutil.copy2(os.path.join(HERE, helper), os.path.join(staging, "tools", helper))

    for tool in ESPTOOL_FILES:
        source = os.path.join(args.esptool_dir, tool)
        if os.path.exists(source):
            shutil.copy2(source, os.path.join(staging, "tools", tool))
        else:
            print(f"warning: {source} not found — the kit will be incomplete")

    if args.stock:
        os.makedirs(os.path.join(staging, "stock"))
        shutil.copy2(args.stock, os.path.join(staging, "stock", STOCK_NAME))

    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for folder, _, files in os.walk(staging):
            for fname in sorted(files):
                full = os.path.join(folder, fname)
                # Keep the top folder inside the archive, so unzipping cannot scatter
                # files across whatever directory the user happened to be in.
                z.write(full, os.path.relpath(full, args.outdir))

    size = os.path.getsize(archive)
    print(f"{archive}  ({size / 1048576:.1f} MB)")
    with zipfile.ZipFile(archive) as z:
        broken = z.testzip()
        print("integrity:", "OK" if broken is None else f"CORRUPT at {broken}")
        for info in z.infolist():
            print(f"  {info.file_size:>9}  {info.filename}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
