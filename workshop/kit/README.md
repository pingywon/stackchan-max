# Recovery kit

Everything needed on a Windows PC to diagnose, erase, flash, capture a boot log, find the
robot on the network, or fall back to stock M5Stack firmware. **Nothing to install** —
`esptool.exe` is bundled in the built kit, and no script ever asks for a flash offset.

The scripts live here. The binaries do not — they are large and rebuildable, so the kit is
assembled rather than committed.

## Contents

| script | what it does |
|---|---|
| `START-HERE.txt` | The entry point for whoever is doing the flashing. Read first. |
| `1-DIAGNOSE.bat` | Read-only. Identifies the chip and flash, and checks whether a valid bootloader exists at 0x0. Changes nothing. |
| `2-ERASE.bat` | Wipes all 16MB. Clears a half-written image. Requires typing `ERASE`. |
| `3-FLASH.bat` | Installs this firmware — five partitions at fixed offsets, with a chip-responds pre-check before a single byte is written. |
| `4-BOOTLOG.bat` | Captures the serial boot log via `serialmon.ps1`. The single most useful diagnostic. |
| `5-RESTORE-STOCK.bat` | Writes M5Stack's own factory image from `stock\StackChan-UserDemo.bin`. The guaranteed way back. |
| `6-FIND-DEVICE.bat` | Finds the robot's IP on the network via `finddevice.ps1`, then opens the portal in a browser. No cable. It looks for Espressif network chips and asks each one whether it is the StackChan portal. Give it the robot's WiFi MAC to single out one robot for certain: `6-FIND-DEVICE.bat aa-bb-cc-dd-ee-ff`. |
| `extract_stock_app.py` | Not part of the kit itself — a host-side tool (see below) that slices a bare app image out of M5Stack's merged factory image, for the portal's own "Restore stock firmware" button. |
| `make_release_zip.py` | Builds the kit zip from a finished firmware build. |

## Assembling a kit

```bash
python3 workshop/kit/make_release_zip.py --version 1.7.1 --esptool-dir /path/to/esptool
```

That writes `release/stackychan-kit-v1.7.1.zip` under the repo. It needs a finished
`firmware/build/`, and a folder holding `esptool.exe` and `esptool-LICENSE.txt` (by
default `workshop/kit/tools/`, which is git-ignored). The two PowerShell helpers are taken
from this directory. Add `--stock /path/to/StackChan-UserDemo.bin` to include M5Stack's
factory image and `5-RESTORE-STOCK.bat`, which makes it a full recovery kit.

The result looks like this:

```
stackychan-kit-v1.7.1/
  START-HERE.txt
  1-DIAGNOSE.bat  … 6-FIND-DEVICE.bat
  tools/
    esptool.exe               espressif/esptool v4.12.0, windows-amd64
    esptool-LICENSE.txt
    serialmon.ps1
    finddevice.ps1
  bin/
    bootloader.bin            firmware/build/bootloader/bootloader.bin
    partition-table.bin       firmware/build/partition_table/partition-table.bin
    ota_data_initial.bin      firmware/build/ota_data_initial.bin
    stack-chan.bin            firmware/build/stack-chan.bin
    generated_assets.bin      firmware/build/generated_assets.bin
  stock/                      only with --stock
    StackChan-UserDemo.bin
```

**esptool** — pin to the version ESP-IDF built with (v4.12.0 for IDF 5.5.4) so the CLI
matches what the build printed. esptool v5 renamed commands:

```
https://github.com/espressif/esptool/releases/download/v4.12.0/esptool-v4.12.0-windows-amd64.zip
```

**Stock firmware** — M5Stack publish a machine-readable index, so the current factory image
can be fetched without M5Burner:

```
https://m5burner-api.m5stack.com/api/firmware        # JSON index; find "StackChan-UserDemo"
https://m5burner.m5stack.com/firmware/<file>.bin     # the image named in its versions[]
```

That image is a full merged flash written at `0x0`, structured exactly like ours. Save it
as `StackChan-UserDemo.bin`, whatever its version. (V1.5.1 was the newest when this was
written.) Whether you may pass M5Stack's binary on to other people is between you and
M5Stack; this repository does not include it.

## The portal's own "Restore stock firmware" button

The portal's Update page (`firmware/main/portal/portal_page.h`) has a button that reverts
the *running code* to real M5Stack firmware over WiFi, using the existing `/api/ota_url`
pull-OTA path (in `firmware/main/portal/portal.cpp`) — the same mechanism as updating
to a new build of this fork. It is not this kit's full restore: it can only ever write into
the `ota_0`/`ota_1` app slot (that's what `/api/ota_url` does), never the bootloader or
partition table, so it cannot touch the assets partition or wipe NVS the way `5-RESTORE-
STOCK.bat` does.

**It is not configured in a default build.** The button needs the address of a file, and
there is no public address for the right file:

- The merged image above is **not** a valid input. Its first bytes are the *bootloader's*
  own valid ESP image, which would pass the OTA magic check and then get written into the
  app slot as garbage.
- What it needs is the bare app image inside that merged image. `extract_stock_app.py`
  reads the merged image's own partition table (offset `0x8000`, standard ESP-IDF format)
  to find where its `ota_0` app partition sits, and slices out just those bytes:

  ```
  python3 workshop/kit/extract_stock_app.py --out stock-app-only.bin
  ```

- Put `stock-app-only.bin` on any web server the robot can reach. Then either paste its
  address into the portal's **Pull from a URL** box, which does exactly what the button
  does, or set `STOCK_URL` in `portal_page.h` to that address and rebuild so the button
  works by itself. With `STOCK_URL` empty, the button shows these instructions and flashes
  nothing.

Re-run the extractor whenever M5Stack ship a new stock version worth pointing at.

Wi-Fi credentials and servo calibration survive this (an app-slot OTA never touches NVS),
which is the opposite tradeoff from `5-RESTORE-STOCK.bat`. On-screen graphics may look wrong
until stock's own updater re-syncs its assets to match, and once stock is running, this
fork's anti-auto-overwrite guard no longer applies — stock resumes updating itself. This is
a one-way door from the portal's side: stock firmware has no portal, so the page that had
the button is gone after it reboots. The USB kit remains the only way back if the network
path doesn't work out.

## Notes that cost time to learn

- **Flashing happens on a different PC from the build.** The firmware was developed on a
  machine with no USB connection to the robot, so nothing here could be tested from the
  build side. The kit is how a build reaches hardware.
- **A charge-only USB-C cable looks identical to a data one** and fails silently. If no COM
  port appears at all, suspect that first.
- **Download mode**: hold RESET ~2s until the internal green LED lights. A black screen
  during this is correct.
- **Prefer the five-file flash over a merged image.** Merging spans `0x0`→end contiguously,
  which blanks `0x9000` (NVS) and so wipes WiFi credentials and servo zero-point
  calibration. The individual writes leave NVS alone.
- **`6-FIND-DEVICE.bat` has changed and is untested on Windows in its current form.** The
  PowerShell parses cleanly and its MAC handling was tested, but the network sweep was not
  run again after the change. `4-BOOTLOG.bat` prints the robot's address over USB if the
  finder lets you down.
