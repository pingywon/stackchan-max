# workshop

Host-side tooling for this fork. Nothing here runs on the device.

| path | what it is |
|---|---|
| `sim/` | Desktop skin simulator. Compiles the real skin sources against desktop LVGL and writes PNGs. See `sim/README.md`. |
| `tools/photo2asset.py` | Convert any photo/PNG into an assets-partition image (LVGL 9.4 `.bin`, or runtime-decoded `.jpg`/`.png`). |
| `tools/verify_asset.py` | Decode a `.bin` exactly the way `main/assets/assets.cpp` does and validate the header. Verified against stock M5Stack assets. |
| `tools/check_version.py` | Reads the firmware version from `firmware/CMakeLists.txt` and fails if the README, the status page or a kit text quotes a different one. `--write` brings them into line. |
| `tools/check_printf_formats.py` | Fails if firmware source uses a 64-bit `printf` conversion (`%llu` and friends), which the chip's small `printf` cannot handle. Run before a build. |
| `portal/check_page.py` | Parses the portal page out of its C++ string and checks its script, element ids and navigation. Run before a build. |
| `portal/mock_server.py` | Serves the real portal page with every endpoint faked, so the page can be used in a browser with no robot. |
| `portal/keyframe-editor.html` | Browser editor that emits the firmware's `KeyframeSequence` JSON. Pixel-faithful to the stock skin. Open the file directly. |
| `design/render_chars.py` | Candidate character previews (VOLT / PIP / GLYPH). Only VOLT was built. |
| `design/render_max.py` | MAX (Max Headroom) preview renderer, including the glitch pass. |
| `design/make_backplate.py` | Bakes MAX's static art to `max_backplate.png`. |
| `notes/FINDINGS.md` | Source-level notes on the upstream firmware: JSON control plane, skin geometry, servo limits, asset format, and the quirks that bite. |
| `kit/` | Windows recovery + flashing kit scripts. See `kit/README.md` for how to assemble a releasable kit. |

The three `design/*.py` scripts need Pillow and write their PNGs beside themselves.

## Regenerating MAX's backplate

```bash
python3 workshop/design/make_backplate.py
python3 workshop/tools/photo2asset.py workshop/design/max_backplate.png \
    -o firmware/main/assets/assets_bin --name max_backplate --format rgb565 --fit contain
python3 workshop/tools/verify_asset.py firmware/main/assets/assets_bin/max_backplate.bin
```

Anything dropped in `firmware/main/assets/assets_bin/` is packed into the 5.69 MB assets
partition automatically (`DEFAULT_ASSETS_EXTRA_FILES` in `firmware/main/CMakeLists.txt`).
The wake-word models share that partition and fill most of it, and the build fails if the
packed image no longer fits.

## Checks worth running before every build

```bash
python3 workshop/portal/check_page.py            # the portal page's script is sound
python3 workshop/tools/check_printf_formats.py   # no 64-bit printf conversions
python3 workshop/tools/check_version.py          # every quoted version matches the firmware's
( cd server/bridge && python selftest.py )       # the bridge, against a fake robot
```

## Build

```bash
source <esp-idf>/export.sh          # ESP-IDF v5.5.4, target esp32s3
cd firmware
python3 fetch_repos.py              # must print "Applied patch" 9 times, never "skipped"
idf.py set-target esp32s3
idf.py build
```
