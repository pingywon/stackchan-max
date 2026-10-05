#!/usr/bin/env python3
"""
photo2asset.py

Convert an arbitrary photo into an asset the StackChan firmware can load by name
via `assets::get_image()`.

The firmware (main/assets/assets.cpp) branches on file extension:

  *.bin                 -> pre-converted LVGL image: 12-byte lv_image_header_t
                           followed by raw pixel data. Cheapest at runtime — the
                           data is mmap'd straight out of the assets partition
                           (esp_mmap_assets) and blitted with no decode step.
  *.png/.jpg/.jpeg/.gif -> handed to LVGL as LV_COLOR_FORMAT_RAW_ALPHA and decoded
                           at runtime. Smaller in flash, but costs CPU and PSRAM on
                           every load.

For a full-screen avatar that redraws constantly, prefer .bin/RGB565.

Verified against LVGL v9.4.0 (lvgl/lvgl ~9.4.0 per firmware idf_component.yml):

    typedef struct {            // little-endian bitfields, 12 bytes total
        uint32_t magic  : 8;    // LV_IMAGE_HEADER_MAGIC = 0x19
        uint32_t cf     : 8;    // lv_color_format_t
        uint32_t flags  : 16;
        uint32_t w      : 16;
        uint32_t h      : 16;
        uint32_t stride : 16;   // bytes per row
        uint32_t reserved_2: 16;
    } lv_image_header_t;

Examples
--------
    # Standard full-screen avatar, opaque
    ./photo2asset.py face.jpg -o build/ --name alice

    # Oval-masked portrait with alpha, for compositing over a background
    ./photo2asset.py face.jpg -o build/ --name alice --mask oval --format rgb565a8

    # Keep it as a runtime-decoded JPEG instead (smaller in flash)
    ./photo2asset.py face.jpg -o build/ --name alice --format jpg
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

try:
    import numpy as np
    from PIL import Image, ImageDraw, ImageFilter
except ImportError as exc:  # pragma: no cover
    sys.exit(f"missing dependency: {exc}. Try: pip install pillow numpy")

# --- LVGL 9.4 constants (see module docstring) -------------------------------
LV_IMAGE_HEADER_MAGIC = 0x19
CF_RGB565 = 0x12
CF_RGB565A8 = 0x14
CF_RGB888 = 0x0F
CF_ARGB8888 = 0x10

# StackChan CoreS3 panel
DEFAULT_W, DEFAULT_H = 320, 240


def lv_header(cf: int, w: int, h: int, stride: int, flags: int = 0) -> bytes:
    """Pack a 12-byte little-endian lv_image_header_t."""
    word0 = (LV_IMAGE_HEADER_MAGIC & 0xFF) | ((cf & 0xFF) << 8) | ((flags & 0xFFFF) << 16)
    word1 = (w & 0xFFFF) | ((h & 0xFFFF) << 16)
    word2 = (stride & 0xFFFF)  # reserved_2 stays 0
    return struct.pack("<III", word0, word1, word2)


def to_rgb565(rgb: np.ndarray) -> np.ndarray:
    """(H,W,3) uint8 -> (H,W) uint16, LVGL's native RGB565 byte order."""
    r = (rgb[:, :, 0].astype(np.uint16) >> 3) << 11
    g = (rgb[:, :, 1].astype(np.uint16) >> 2) << 5
    b = rgb[:, :, 2].astype(np.uint16) >> 3
    return r | g | b


def fit_image(img: Image.Image, w: int, h: int, mode: str, focus: str) -> Image.Image:
    """Resize to exactly w*h, either cropping to fill (cover) or padding (contain)."""
    if mode == "contain":
        out = Image.new("RGBA", (w, h), (0, 0, 0, 255))
        scaled = img.copy()
        scaled.thumbnail((w, h), Image.LANCZOS)
        out.paste(scaled, ((w - scaled.width) // 2, (h - scaled.height) // 2))
        return out

    # cover: scale so both axes are >= target, then crop
    scale = max(w / img.width, h / img.height)
    new = img.resize((max(1, round(img.width * scale)), max(1, round(img.height * scale))), Image.LANCZOS)

    dx = (new.width - w) // 2
    # Portraits usually want the crop biased upward so the face isn't chopped.
    if focus == "top":
        dy = 0
    elif focus == "face":
        dy = min(max(0, int(new.height * 0.12)), max(0, new.height - h))
    else:
        dy = (new.height - h) // 2
    return new.crop((dx, dy, dx + w, dy + h))


def apply_mask(img: Image.Image, shape: str, feather: int) -> Image.Image:
    """Punch an alpha mask into the image. Returns RGBA."""
    if shape == "none":
        return img.convert("RGBA")

    w, h = img.size
    mask = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(mask)

    if shape == "circle":
        r = min(w, h) // 2
        cx, cy = w // 2, h // 2
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    elif shape == "oval":
        # Portrait-ish oval: slightly narrower than the frame, full height.
        pad_x = int(w * 0.14)
        d.ellipse((pad_x, -int(h * 0.04), w - pad_x, h + int(h * 0.04)), fill=255)
    elif shape == "rounded":
        d.rounded_rectangle((0, 0, w - 1, h - 1), radius=int(min(w, h) * 0.12), fill=255)
    else:
        raise ValueError(f"unknown mask shape: {shape}")

    if feather > 0:
        mask = mask.filter(ImageFilter.GaussianBlur(feather))

    out = img.convert("RGBA")
    out.putalpha(mask)
    return out


def encode(img: Image.Image, fmt: str) -> tuple[bytes, int, int]:
    """Return (payload, color_format, stride_bytes)."""
    w, h = img.size

    if fmt == "rgb565":
        rgb = np.asarray(img.convert("RGB"), dtype=np.uint8)
        return to_rgb565(rgb).astype("<u2").tobytes(), CF_RGB565, w * 2

    if fmt == "rgb565a8":
        rgba = np.asarray(img.convert("RGBA"), dtype=np.uint8)
        colour = to_rgb565(rgba[:, :, :3]).astype("<u2").tobytes()
        alpha = rgba[:, :, 3].tobytes()  # full alpha plane appended after colour
        return colour + alpha, CF_RGB565A8, w * 2

    if fmt == "argb8888":
        rgba = np.asarray(img.convert("RGBA"), dtype=np.uint8)
        bgra = rgba[:, :, [2, 1, 0, 3]]  # LVGL stores B,G,R,A in memory order
        return bgra.tobytes(), CF_ARGB8888, w * 4

    raise ValueError(f"unknown format: {fmt}")


def main() -> int:
    p = argparse.ArgumentParser(
        description="Convert a photo into a StackChan assets-partition image.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("input", type=Path, help="source photo (anything Pillow can open)")
    p.add_argument("-o", "--outdir", type=Path, default=Path("."), help="output directory")
    p.add_argument("--name", default=None, help="asset base name (default: input stem)")
    p.add_argument("--size", default=f"{DEFAULT_W}x{DEFAULT_H}", help="WxH (default 320x240)")
    p.add_argument("--format", default="rgb565",
                   choices=["rgb565", "rgb565a8", "argb8888", "jpg", "png"],
                   help="rgb565 is the cheapest at runtime (default)")
    p.add_argument("--fit", default="cover", choices=["cover", "contain"])
    p.add_argument("--focus", default="face", choices=["center", "top", "face"],
                   help="crop bias when fitting (default: face = slight upward bias)")
    p.add_argument("--mask", default="none", choices=["none", "circle", "oval", "rounded"])
    p.add_argument("--feather", type=int, default=0, help="mask edge blur radius in px")
    p.add_argument("--quality", type=int, default=88, help="JPEG quality when --format jpg")
    p.add_argument("--preview", action="store_true", help="also write a viewable PNG preview")
    args = p.parse_args()

    if not args.input.exists():
        return f"input not found: {args.input}"

    try:
        w, h = (int(v) for v in args.size.lower().split("x"))
    except ValueError:
        return f"bad --size {args.size!r}, expected WxH e.g. 320x240"

    name = args.name or args.input.stem
    args.outdir.mkdir(parents=True, exist_ok=True)

    img = Image.open(args.input)
    if getattr(img, "n_frames", 1) > 1:
        img.seek(0)  # first frame of animated sources
    img = img.convert("RGBA")

    img = fit_image(img, w, h, args.fit, args.focus)
    img = apply_mask(img, args.mask, args.feather)

    # Runtime-decoded formats: just write the encoded file.
    if args.format in ("jpg", "png"):
        ext = "jpg" if args.format == "jpg" else "png"
        out = args.outdir / f"{name}.{ext}"
        if ext == "jpg":
            if args.mask != "none":
                print("note: JPEG has no alpha channel — mask is flattened onto black", file=sys.stderr)
            img.convert("RGB").save(out, quality=args.quality, optimize=True)
        else:
            img.save(out, optimize=True)
        print(f"wrote {out}  ({out.stat().st_size:,} bytes, decoded at runtime by LVGL)")
        return 0

    if args.format == "rgb565" and args.mask != "none":
        print("note: rgb565 has no alpha — use rgb565a8 if you want the mask to be transparent",
              file=sys.stderr)

    payload, cf, stride = encode(img, args.format)
    blob = lv_header(cf, w, h, stride) + payload

    out = args.outdir / f"{name}.bin"
    out.write_bytes(blob)

    print(f"wrote {out}")
    print(f"  format   {args.format} (cf=0x{cf:02X})   {w}x{h}   stride={stride}B")
    print(f"  size     {len(blob):,} bytes  ({len(blob)/1024:.1f} KiB)  header=12B + data={len(payload):,}B")
    print(f"  load in firmware:  assets::get_image(\"{name}.bin\")")

    if args.preview:
        prev = args.outdir / f"{name}_preview.png"
        img.save(prev)
        print(f"  preview  {prev}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
