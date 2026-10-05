# StackChan firmware — source findings

Everything here was read out of the `m5stack/StackChan` source, not from documentation. File:line references are to `firmware/`.

> **This is a snapshot of upstream at the commit the fork is based on (`b72b3ed`, 2 July 2026).** It
> describes M5Stack's firmware, not what this fork has since changed. Where the fork now
> differs, a note says so. For the fork's own state read `docs/STATUS.md`.

---

## 1. Build identity

| | |
|---|---|
| Upstream | `78/xiaozhi-esp32` tag **`v2.2.4`** + local patch (`repos.json`) |
| ESP-IDF | `idf_component.yml` requires **`>=5.5.2`**; the community build guide uses **5.5.4** |
| LVGL | **`lvgl/lvgl ~9.4.0`** — LVGL 9 API (`lv_image_dsc_t`, `lv_screen_active()`) |
| UI stack | `smooth_ui_toolkit` v2.12.0 + `mooncake` v2.3.3 (Forairaaaaa) |
| Asset packer | `espressif/esp_mmap_assets >= 1.2` — build-time pack, runtime mmap |
| Wake word | `CONFIG_SR_WN_WN9_HISTACKCHAN_TTS3=y` — bespoke Espressif-trained WakeNet9 |
| License | MIT (M5Stack, 2026) |

### Partition table (`partitions.csv`)

| Name | Type | SubType | Offset | Size |
|---|---|---|---|---|
| nvs | data | nvs | 0x9000 | 0x4000 (16KB) |
| otadata | data | ota | 0xd000 | 0x2000 |
| phy_init | data | phy | 0xf000 | 0x1000 |
| ota_0 | app | ota_0 | 0x20000 | 0x4f0000 |
| ota_1 | app | ota_1 | — | 0x4f0000 |
| **assets** | data | spiffs | **0xA00000** | **4MB** |
| coredump | data | coredump | — | 0x10000 |

`CONFIG_PARTITION_TABLE_CUSTOM=y`. NVS is **only 16KB** — keep portal settings small; binaries
belong in the assets partition.

*Fork note: the assets partition is now `0x5B0000` (5.69 MB), grown into previously unused
flash to hold eleven wake-word models. Everything before it is unchanged.*

### The patch (`patches/xiaozhi-esp32.patch`)

115 added / 107 removed across 5 files: `main/application.cc`, `main/assets.{cc,h}`,
`main/boards/common/i2c_device.{cc,h}`.

Substantively it **removes `Assets::EmoteStrategy`** — upstream's asset path bound to
`emote::EmoteDisplay` — because M5Stack renders with their own LVGL avatar. Partition discovery,
mmap and name-addressed `GetAssetData()` all survive.

> Consequence: `78/xiaozhi-assets-generator` output is only **partially** usable. Wake-word models
> and fonts should load; its **emoji packs target the removed EmoteDisplay and will not render.**

---

## 2. The JSON control plane — `main/stackchan/json/json_helper.cpp`

Four runtime entry points, exposed on the singleton (`stackchan.h`) as `updateAvatarFromJson()`,
`updateMotionFromJson()`, `updateNeonLightFromJson()`, plus
`animation::parse_sequence_from_json()`.

**Avatar** — keys `leftEye` / `rightEye` / `mouth`; every field optional:

```json
{"leftEye": {"x": -20, "y": 0, "rotation": 150, "weight": 80, "size": 10}}
```

**Motion** — keys `yawServo` / `pitchServo`; three mutually exclusive modes, checked in this order:

```json
{"yawServo":   {"rotate": 400},
 "pitchServo": {"angle": 450, "spring": {"stiffness": 170.0, "damping": 26.0}}}
```

1. `rotate` present → `servo.rotate(v)` and **return immediately**
2. no `angle` → return (nothing happens)
3. `speed` present → `moveWithSpeed(angle, speed)`
4. `spring` present → `moveWithSpringParams(angle, stiffness, damping)` (defaults 170.0 / 26.0)
5. otherwise → `move(angle)` (default spring)

**Keyframe sequence** — top level **must be a JSON array** or it's rejected outright
(`"json is not an array"`):

```json
[{"leftEye":{"y":30,"weight":100}, "rightEye":{"y":30,"weight":100},
  "mouth":{"weight":60},
  "yawServo":{"angle":300,"speed":400}, "pitchServo":{"angle":600,"speed":300},
  "leftRgbColor":"#FF00AA", "rightRgbColor":"#00FFEE",
  "durationMs":450}]
```

Note the keyframe servo struct only carries `angle` + `speed` — no `spring` and no `rotate`.

**Neon lights** — `leftRgbColor` / `leftRgbDuration` / `rightRgbColor` / `rightRgbDuration`
(colours are strings; durations are floats).

Parse failures are logged and the call returns — malformed JSON degrades safely rather than
wedging the update loop.

### It is already wired to a transport

`main/apps/app_dance/app_dance.cpp` feeds all three from **BLE**:

```cpp
GetHAL().onBleAvatarData.connect(...) -> GetStackChan().updateAvatarFromJson(ptr);
GetHAL().onBleMotionData.connect(...) -> GetStackChan().updateMotionFromJson(ptr);
GetHAL().onBleRgbData.connect(...)    -> GetStackChan().updateNeonLightFromJson(ptr);
```

An HTTP/WebSocket portal is the same three calls with a different source. `app_dance.cpp` is the
reference implementation.

It also shows a servo subtlety worth copying: `check_auto_angle_sync_mode()` turns
`setAutoAngleSyncEnabled(true)` back on when commands stop for >2000ms, avoiding stutter during
high-frequency streaming while still preventing jumps after manual handling.

---

## 3. Skin geometry — `avatar/skins/default/`

Panel is 320×240, background `secondaryColor`, all features centre-aligned.

**Eyes** (`eyes.cpp`):

| constant | value |
|---|---|
| `_eye_pos` | `(-70, -16)` — left; right is `+70` |
| `_eye_min/max_offset` | `±16` px, mapped from position −100..100 |
| `_eye_size_limit` | `(8, 32)` — diameter mapped from size −100..100 |
| container | 32×32, pivot at 16,16 |

`weight` drives the eyelid: `_eyelid_offset_y = -map_range(weight, 0,100, 0, eyelidHeight)`.
**weight 100 = fully open, 0 = closed.** Eyelid is `secondaryColor`, eye disc is `primaryColor`.

**Mouth** (`mouth.cpp`):

| constant | value |
|---|---|
| `_mouth_pos` | `(0, 26)` |
| offset | `±16` px from position |
| size | width `90 → 60`, height `6 → 50` as weight goes 0 → 100 |
| radius | `0 → 16` |

> **Counterintuitive:** the mouth gets **narrower** as it opens (90→60 wide) while growing taller.
> Closed = wide thin bar; open = narrow tall rounded rect. `DefaultMouth` ignores `size` entirely.

**Emotion presets** (`DefaultEyes::setEmotion`) — `[weight, rotation]`, right eye negates rotation:

| Emotion | weight | rotation |
|---|---|---|
| Neutral | 100 | 0 |
| Happy | 72 | 1550 |
| Angry | 70 | 450 |
| Sad | 70 | −400 |
| Doubt | 75 | 0 |
| Sleepy | 35 | −50 |

The `Emotion` enum has **only these six** values (`elements/emotion.h`). Adding more touches the
enum, every skin's `setEmotion`, and the agent's emotion mapping.

---

## 4. Two quirks that will bite

### 4a. `getRotation()` disagrees with what's rendered

`Element::setRotation()` clamps to **0..3600**:

```cpp
_rotation = uitk::clamp(rotation, 0, 3600);
```

but `DefaultEyes::setRotation()` / `DefaultMouth::setRotation()` pass the **raw, unclamped**
argument to LVGL:

```cpp
Element::setRotation(rotation);        // stores clamped
_container->setRotation(rotation);     // renders RAW
```

So negative rotations **render correctly** but `getRotation()` reports `0`. The built-in emotion
presets depend on this — `Sad` is −400, `Sleepy` is −50, and every right eye negates its rotation.

**Implication:** never round-trip rotation through `getRotation()`; keep your own authoritative
value. Any modifier that reads-modifies-writes rotation will silently snap negatives to zero.

### 4b. Servo limits are tenths of a degree, and tighter than the docs suggest

`hal_servo.cpp:340-350`:

```cpp
yaw_servo_config.angleLimit   = Vector2i(-1280, 1280);   // -128.0 .. +128.0 degrees
pitch_servo_config.angleLimit = Vector2i(30, 870);       //    3.0 ..   87.0 degrees
```

Both are **tenths of a degree**. `servo.cpp:110,121` clamps every move to these limits, so the
firmware already protects the pitch axis at 3°–87°. M5Stack's docs separately *recommend* staying
inside 5°–85°.

There is also a runtime-learned limit (`_runtime_raw_pos_limit`) that narrows as the servo reports
stalls — see `hal_servo.cpp:292-308`.

---

## 5. Assets — how images load

`main/assets/assets.cpp`, `assets::get_image(name)` → `Assets::GetInstance().GetAssetData(...)`,
then branches on extension:

- `.bin` → 12-byte `lv_image_header_t` + raw pixels, mmap'd and blitted with **no decode**
- `.png` / `.jpg` / `.jpeg` / `.gif` → `LV_COLOR_FORMAT_RAW_ALPHA`, decoded by LVGL at runtime
- anything else → `LV_COLOR_FORMAT_RAW` fallback

**A JPEG/PNG in the assets partition is renderable by name with no firmware rebuild.** For a
full-screen avatar prefer `.bin` RGB565 (150 KiB at 320×240) — no per-frame decode.

LVGL 9.4 header, little-endian, 12 bytes (verified against `lvgl v9.4.0/src/draw/lv_image_dsc.h`):

```
byte 0     magic   = 0x19 (LV_IMAGE_HEADER_MAGIC)
byte 1     cf      = 0x12 RGB565 | 0x14 RGB565A8 | 0x0F RGB888 | 0x10 ARGB8888
bytes 2-3  flags
bytes 4-5  w
bytes 6-7  h
bytes 8-9  stride (bytes per row)
bytes 10-11 reserved_2
```

`tools/photo2asset.py` writes this; `tools/verify_asset.py` reads it back. Both round-trip PASS.

Upstream's asset inventory was only 18 files (`assets_bin/`), so its 4MB was nearly empty.
*Fork note: no longer true. Eleven wake-word models now fill most of the 5.69 MB
partition; check the build's own `assets.bin: N bytes of M` line before adding anything.*

---

## 6. Other surface area worth knowing

- **Servos are Feetech serial-bus**, not PWM — `hal/drivers/FTServo_Arduino/` (SCS, SCSCL,
  SMS_STS, HLSCL). Position feedback is readable via `getCurrentAngle()`.
- **`Motion` has useful high-level APIs** beyond raw angles: `lookAtNormalized(x, y, speed)` and
  `lookAtPoint(x, y, z, speed)`, plus `goHome()`, `setTorqueEnabled()`,
  `setAutoTorqueReleaseEnabled()`.
- **MCP is thin** — `hal/hal_mcp.cpp` exposes only `self.robot.set_head_angles`,
  `get_head_angles`, `set_led_color`, `create_reminder`, `get_reminders`, `stop_reminder`.
  **No expression/avatar tool.** Easy win given the JSON plane exists.
  *Fork note: the fork has since added `play_gesture`, and `list_songs` / `dance_to_song`
  (registered only with an SD card mounted). There is still no face tool.*
- **App framework**: `main/apps/` on Mooncake lifecycle (`onCreate/onOpen/onRunning/onClose`),
  with `app_template/` as a starting point and an App Center for downloads. Ship our work as an
  app rather than patching existing ones.
- **Modifiers** are pooled plugins over a `Modifiable` interface exposing `motion()`, `avatar()`,
  `leftNeonLight()`, `rightNeonLight()`: `blink`, `breath`, `dance`, `head_pet`,
  `idle_expression`, `idle_motion`, `imu`, `speaking`, `timed`.
- **Decorators** (`angry/dizzy/heart/shy/sweat`) are compiled-in `.c` arrays ~31KB each — new ones
  need a recompile unless rerouted through `assets::get_image()`.
- `hal_ws_avatar.cpp` (18KB) is an existing WebSocket avatar channel — study before building a new
  transport.
