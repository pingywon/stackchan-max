# skinsim — host-side skin simulator

Compiles the **real** skin sources (`max.cpp`, `volt.cpp`, and the stock default skin)
against desktop LVGL v9.4.0, renders them into a memory framebuffer at the device's exact
resolution and colour depth (320×240, `LV_COLOR_DEPTH 16`), and writes PNGs.

No hardware. No flashing. Seconds per iteration instead of a 20-minute rebuild-and-flash.

## Why this exists

v1.0.0 shipped having never been rendered anywhere. It didn't boot, and on top of that it
carried a bug that a single screenshot would have caught: **MAX's eyebrow colour was
identical to the hair colour, and the brows sat in the hairline** — so they were invisible,
and since brow rotation is how every emotion reads, all six emotions looked the same.

The simulator found that in its first run.

## Trusting it

The default skin is rendered alongside the custom ones on every run. It is stock M5Stack
code whose appearance is known, so if `default_*.png` looks right, the harness is faithful
and the custom output can be believed.

## Build

Needs two things first:

- LVGL v9.4.0 checked out (by default in `workshop/sim/lvgl`, which is git-ignored).
- `smooth_ui_toolkit` and `mooncake_log` under `firmware/components/`. They are not in
  the repo; `python3 fetch_repos.py` in `firmware/` downloads them (along with the rest
  of the firmware's dependencies).

```bash
cd workshop/sim
git clone --depth 1 -b v9.4.0 https://github.com/lvgl/lvgl.git lvgl
( cd ../../firmware && python3 fetch_repos.py )

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target skinsim -j2
```

Override the LVGL location with `-DLVGL_DIR=/path/to/lvgl`, and the two libraries with
`-DSUIT_DIR=` and `-DMCLOG_DIR=`. The first build compiles all of LVGL and takes a few
minutes; after that a change to a skin rebuilds in seconds.

## Run

```bash
./build/skinsim <outdir> <assets_dir>

# e.g.
mkdir -p /tmp/simout
./build/skinsim /tmp/simout ../../firmware/main/assets/assets_bin
```

The output folder must already exist; the program does not create it.

Writes, for each of `max` / `volt` / `default`:

| frame | what it exercises |
|---|---|
| `neutral` `happy` `angry` `sad` `doubt` `sleepy` | all six `Emotion` presets |
| `blink` | `weight = 0` — the geometry path at its extreme |
| `speaking` | `weight = 100` mouth, i.e. lip-sync's limit |
| `gaze_right` `gaze_upleft` | eye position extremes, what `GazeModifier` drives |
| `size_min` `size_max` | the `size` axis of the `Feature` contract |

It also constructs and destroys each avatar, so lifetime bugs surface as crashes here
rather than on the device.

## Stubs

`stubs.cpp` implements `assets::get_image()` by reading `.bin` files off disk and parsing
them byte-for-byte the way `main/assets/assets.cpp` parses the mmap'd partition — same
header layout, same extension branching. The image path being exercised is the real one.

## What it does not cover

Boot order, HAL init, servos, audio, WiFi, memory pressure, and anything timing-dependent.
It answers "does this render correctly", not "does this boot". Those need the device.
