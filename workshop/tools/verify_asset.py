#!/usr/bin/env python3
"""
verify_asset.py — companion to photo2asset.py

Parse a .bin produced by photo2asset.py exactly the way the firmware does, and
reconstruct a viewable PNG. This is the only way to prove the LVGL header is
byte-correct without flashing the device.

Mirrors main/assets/assets.cpp:

    memcpy(&dsc.header, data_ptr, sizeof(lv_image_header_t));   // 12 bytes
    dsc.data_size = data_size - sizeof(lv_image_header_t);
    dsc.data      = data_ptr + sizeof(lv_image_header_t);
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

MAGIC = 0x19
CF_NAMES = {0x0F: "RGB888", 0x10: "ARGB8888", 0x12: "RGB565", 0x14: "RGB565A8"}


def parse_header(blob: bytes) -> dict:
    if len(blob) < 12:
        raise ValueError(f"file too short ({len(blob)}B) to contain a 12-byte header")
    w0, w1, w2 = struct.unpack_from("<III", blob, 0)
    return {
        "magic": w0 & 0xFF,
        "cf": (w0 >> 8) & 0xFF,
        "flags": (w0 >> 16) & 0xFFFF,
        "w": w1 & 0xFFFF,
        "h": (w1 >> 16) & 0xFFFF,
        "stride": w2 & 0xFFFF,
        "reserved_2": (w2 >> 16) & 0xFFFF,
    }


def rgb565_to_rgb(buf: bytes, w: int, h: int) -> np.ndarray:
    px = np.frombuffer(buf, dtype="<u2", count=w * h).reshape(h, w)
    r = ((px >> 11) & 0x1F).astype(np.uint8)
    g = ((px >> 5) & 0x3F).astype(np.uint8)
    b = (px & 0x1F).astype(np.uint8)
    # expand to 8-bit preserving full range
    r = (r << 3) | (r >> 2)
    g = (g << 2) | (g >> 4)
    b = (b << 3) | (b >> 2)
    return np.dstack([r, g, b])


def main() -> int:
    ap = argparse.ArgumentParser(description="Decode and validate a StackChan LVGL .bin asset")
    ap.add_argument("path", type=Path)
    ap.add_argument("-o", "--out", type=Path, default=None, help="write decoded PNG here")
    args = ap.parse_args()

    blob = args.path.read_bytes()
    hdr = parse_header(blob)
    body = blob[12:]

    cf_name = CF_NAMES.get(hdr["cf"], f"UNKNOWN(0x{hdr['cf']:02X})")
    print(f"file        {args.path}  ({len(blob):,} bytes)")
    print(f"magic       0x{hdr['magic']:02X}  {'OK' if hdr['magic'] == MAGIC else 'BAD — expected 0x19'}")
    print(f"color fmt   0x{hdr['cf']:02X}  {cf_name}")
    print(f"dimensions  {hdr['w']} x {hdr['h']}")
    print(f"stride      {hdr['stride']} bytes/row")
    print(f"flags       0x{hdr['flags']:04X}   reserved_2 0x{hdr['reserved_2']:04X}")
    print(f"payload     {len(body):,} bytes")

    ok = hdr["magic"] == MAGIC
    w, h = hdr["w"], hdr["h"]

    expected = {0x12: w * h * 2, 0x14: w * h * 3, 0x10: w * h * 4, 0x0F: w * h * 3}.get(hdr["cf"])
    if expected is not None:
        match = len(body) == expected
        print(f"payload chk {'OK' if match else f'MISMATCH — expected {expected:,}'}")
        ok = ok and match

    exp_stride = {0x12: w * 2, 0x14: w * 2, 0x10: w * 4, 0x0F: w * 3}.get(hdr["cf"])
    if exp_stride is not None:
        match = hdr["stride"] == exp_stride
        print(f"stride chk  {'OK' if match else f'MISMATCH — expected {exp_stride}'}")
        ok = ok and match

    if args.out:
        if hdr["cf"] == 0x12:
            im = Image.fromarray(rgb565_to_rgb(body, w, h), "RGB")
        elif hdr["cf"] == 0x14:
            rgb = rgb565_to_rgb(body[: w * h * 2], w, h)
            alpha = np.frombuffer(body[w * h * 2:], dtype=np.uint8, count=w * h).reshape(h, w)
            im = Image.fromarray(np.dstack([rgb, alpha]), "RGBA")
        elif hdr["cf"] == 0x10:
            bgra = np.frombuffer(body, dtype=np.uint8, count=w * h * 4).reshape(h, w, 4)
            im = Image.fromarray(bgra[:, :, [2, 1, 0, 3]], "RGBA")
        elif hdr["cf"] == 0x0F:
            im = Image.fromarray(np.frombuffer(body, dtype=np.uint8, count=w * h * 3).reshape(h, w, 3), "RGB")
        else:
            print("cannot decode this colour format", file=sys.stderr)
            return 2
        args.out.parent.mkdir(parents=True, exist_ok=True)
        im.save(args.out)
        print(f"decoded ->  {args.out}")

    print(f"\nRESULT      {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
