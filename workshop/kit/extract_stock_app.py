#!/usr/bin/env python3
"""Slice a bare app image out of M5Stack's merged stock firmware.

M5Burner distributes the factory image as one blob written starting at flash offset 0x0:
bootloader, partition table, app, assets, all concatenated at their real offsets. That is
NOT a valid input for this portal's existing OTA endpoints (`/api/ota`, `/api/ota_url`) —
they write into the `ota_0`/`ota_1` app slot only, and the merged blob's first bytes are the
*bootloader's* own valid ESP image (same 0xE9 magic an app image has), so it would sail past
the magic check and get written into the app slot as garbage.

This reads the merged image's own embedded partition table (the standard ESP-IDF format, at
a fixed 0x8000 offset — M5Stack's layout is "structured exactly like ours", per
workshop/kit/README.md) to find where ITS app partition actually sits, and slices out just
those bytes. The result is a normal, valid app image that can be fed straight to this
portal's existing `/api/ota_url`, exactly like updating to a new build of this fork.

Usage:
    python3 workshop/kit/extract_stock_app.py [--out stock-app-only.bin]

Fetches the current firmware index from m5burner-api.m5stack.com, finds the
"StackChan-UserDemo" entry, downloads its merged image, and writes the extracted slice.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
import urllib.request

INDEX_URL = "https://m5burner-api.m5stack.com/api/firmware"
FIRMWARE_NAME_HINT = "stackchan"

PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_SIZE = 0xC00          # one flash sector, standard ESP-IDF layout
ENTRY_SIZE = 32
ENTRY_MAGIC = 0x50AA                  # esp_partition_info_t.magic, little-endian (bytes AA 50)
APP_TYPE = 0x00
SUBTYPE_FACTORY = 0x00
SUBTYPE_OTA_0 = 0x10

# Our own ota_0/ota_1 slot size, from firmware/partitions.csv — the extracted slice must
# fit, or the assumption that M5Stack's layout matches ours closely enough has broken.
OUR_OTA_SLOT_SIZE = 0x4F0000


def fetch(url: str) -> bytes:
    print(f"  GET {url}", file=sys.stderr)
    with urllib.request.urlopen(url, timeout=30) as resp:
        return resp.read()


def find_stock_url() -> str:
    index = json.loads(fetch(INDEX_URL))
    entries = index if isinstance(index, list) else index.get("data", index.get("list", []))
    for entry in entries:
        name = json.dumps(entry).lower()
        if FIRMWARE_NAME_HINT in name and "userdemo" in name:
            for key in ("versions", "version", "files"):
                versions = entry.get(key)
                if isinstance(versions, list) and versions:
                    v = versions[-1]
                    file_name = v.get("name") or v.get("file") or v.get("bin")
                    if file_name:
                        return f"https://m5burner.m5stack.com/firmware/{file_name}"
    raise SystemExit(
        "could not find a StackChan-UserDemo entry in the M5Burner index — "
        "its JSON shape may have changed; inspect it manually and hardcode the URL instead")


def parse_partition_table(blob: bytes) -> list[dict]:
    table = blob[PARTITION_TABLE_OFFSET:PARTITION_TABLE_OFFSET + PARTITION_TABLE_SIZE]
    entries = []
    for i in range(0, len(table), ENTRY_SIZE):
        chunk = table[i:i + ENTRY_SIZE]
        if len(chunk) < ENTRY_SIZE:
            break
        magic, ptype, subtype, offset, size = struct.unpack_from("<HBBII", chunk, 0)
        if magic != ENTRY_MAGIC:
            break  # 0xFFFF padding or the MD5-checksum entry — table ends here
        label = chunk[12:28].split(b"\x00", 1)[0].decode("ascii", "replace")
        entries.append({"type": ptype, "subtype": subtype, "offset": offset,
                        "size": size, "label": label})
    return entries


def find_app_entry(entries: list[dict]) -> dict:
    by_subtype = {e["subtype"]: e for e in entries if e["type"] == APP_TYPE}
    for subtype in (SUBTYPE_FACTORY, SUBTYPE_OTA_0):
        if subtype in by_subtype:
            return by_subtype[subtype]
    app_entries = [e for e in entries if e["type"] == APP_TYPE]
    if app_entries:
        return app_entries[0]
    raise SystemExit("no APP-type partition found in the stock image's partition table — "
                      "dumping what was parsed:\n" + json.dumps(entries, indent=2))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--url", help="merged stock image URL (skips the index lookup)")
    ap.add_argument("--out", default="stock-app-only.bin")
    args = ap.parse_args()

    url = args.url or find_stock_url()
    blob = fetch(url)
    print(f"  downloaded {len(blob)} bytes", file=sys.stderr)

    entries = parse_partition_table(blob)
    if not entries:
        raise SystemExit(f"no valid partition table found at offset {PARTITION_TABLE_OFFSET:#x} "
                          "— this file may not be the merged image expected")
    print("  partition table:", file=sys.stderr)
    for e in entries:
        print(f"    {e['label']:<16} type={e['type']:#04x} subtype={e['subtype']:#04x} "
              f"offset={e['offset']:#x} size={e['size']:#x}", file=sys.stderr)

    app = find_app_entry(entries)
    print(f"  using app partition '{app['label']}' at {app['offset']:#x}, "
          f"size {app['size']:#x}", file=sys.stderr)

    if app["offset"] + app["size"] > len(blob):
        raise SystemExit("app partition extends past the end of the downloaded file — "
                          "the download is truncated or the partition table is wrong")

    if app["size"] > OUR_OTA_SLOT_SIZE:
        raise SystemExit(
            f"stock app ({app['size']:#x} bytes) does not fit our ota_0/ota_1 slot "
            f"({OUR_OTA_SLOT_SIZE:#x} bytes) — the layouts have diverged, this needs a "
            "human to look at before shipping it as an OTA target")

    sliced = blob[app["offset"]:app["offset"] + app["size"]]
    if sliced[0] != 0xE9:
        raise SystemExit(f"sliced app does not start with the ESP image magic (0xE9); "
                          f"got {sliced[0]:#04x} — offset/size is wrong, do not use this")

    with open(args.out, "wb") as fh:
        fh.write(sliced)
    print(f"  wrote {args.out} ({len(sliced)} bytes) — verified 0xE9 magic, fits OTA slot",
          file=sys.stderr)


if __name__ == "__main__":
    main()
