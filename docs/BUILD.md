# Build and flash

How to compile this firmware and put it on a robot. For the overview, read the
[README](../README.md) first.

You need ESP-IDF **5.5.4**. The target is `esp32s3`.

```bash
git clone --depth 1 -b v5.5.4 --recursive https://github.com/espressif/esp-idf.git
./esp-idf/install.sh esp32s3
source ./esp-idf/export.sh

cd firmware
python3 fetch_repos.py      # must print "Applied patch" 9 times and never "skipped"
idf.py set-target esp32s3
idf.py build
```

`fetch_repos.py` downloads the upstream voice-assistant code
([xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) v2.2.4) and applies nine patches:
M5Stack's own, then eight from this fork. **Read its output.** If a patch fails it prints
`skipped` and carries on, and the build breaks much later with an unrelated error.

**Last checked 2026-10-05** on a Debian 13 PC, from a clean copy of this repository:
all nine patches applied, and `idf.py build` finished with no errors (15 to 20 minutes
on a busy 3-core virtual machine). It produced a 4.01 MB program (`stack-chan.bin`, 81% of its slot) and a 5.11 MB
assets image (`generated_assets.bin`, 89.9% of its partition, so there is little room
for a twelfth wake phrase). That shows the code compiles. It says nothing about how it
runs: **that build was not flashed.**

**Flashing needs a USB cable the first time.** The first install changes the partition
layout (the assets area grows from 4 MB to 5.69 MB to hold the wake-word models). An
over-the-air update cannot move a partition boundary.

Put the robot in download mode: hold RESET for about two seconds until the internal
green LED lights. Then, from `firmware/`, write the five images at their fixed places.
This is the command the build prints when it finishes (`idf.py -p PORT flash` does the
same):

```bash
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0      build/bootloader/bootloader.bin \
  0x8000   build/partition_table/partition-table.bin \
  0xd000   build/ota_data_initial.bin \
  0x20000  build/stack-chan.bin \
  0xa00000 build/generated_assets.bin
```

On Windows, [`workshop/kit/`](../workshop/kit/README.md) wraps the same five writes, at the
same offsets, in double-click scripts with a bundled `esptool.exe`. That kit is how the
one real robot has been flashed. The command above **has not itself been run against
hardware** by this project; treat it as correct on paper.

After that, updates that only change the program are meant to go through the portal's
Update page (that path has no hardware confirmation on record yet, see [What is proven](STATUS.md#what-is-proven)).
Anything that changes the wake-word set or the partition table needs USB again.

Then check the boot log. It should say `App version: 9.9.3-stackychan`. If it says
a plain version such as `1.4.4` or `1.5.1`, M5Stack's stock firmware is running and you are
debugging the wrong thing.

**To go back to stock:** M5Burner, or the kit's `5-RESTORE-STOCK.bat`, writes M5Stack's
factory image. Nothing here is permanent.

## Where things live

```
firmware/                 the robot's firmware (M5Stack's, plus this fork's changes)
  main/portal/            the web portal: server, settings, the embedded page
  main/stackchan/avatar/  skins: skin_base.h, default/, volt/, max/
  main/stackchan/modifiers/gaze.h
  main/apps/              on-device screens (clock, weather, stock, face picker are new)
  patches/                M5Stack's patch and this fork's eight
server/bridge/            the Python bridge to Claude, ChatGPT and the free providers
workshop/                 tools that run on a computer, not on the robot
  sim/                    the skin simulator
  tools/                  photo2asset.py, verify_asset.py, check_printf_formats.py
  design/                 the scripts that drew the characters
  portal/                 mock server, page checker, keyframe editor
  kit/                    Windows flash and recovery scripts
  notes/FINDINGS.md       notes from reading the upstream source
app/  remote/  server/    M5Stack's phone app, remote control and own server, unchanged
docs/STATUS.md            what is built, what is proven, open problems, how it fits together
docs/BUILD.md             this page
docs/LESSONS.md           the mistakes that cost real time
docs/img/                 the pictures used by the README and the site
site/                     the project site (one page, published on GitHub Pages)
```
