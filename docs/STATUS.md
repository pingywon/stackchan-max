# Status

What exists, what has been proven, what is still open, and how the pieces fit together.
For the overview with pictures, read the [README](../README.md) first.

- **Base:** [m5stack/StackChan](https://github.com/m5stack/StackChan) at commit `b72b3ed`
  (2 July 2026), which builds on [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) v2.2.4.
- **Toolchain:** ESP-IDF 5.5.4, target `esp32s3`, LVGL 9.4.
- **Version:** `9.9.3-stackychan`. This is the firmware version. It is defined once, as
  `PROJECT_VER` in `firmware/CMakeLists.txt`; the robot's page and boot log read it from
  the running firmware, and `workshop/tools/check_version.py` fails if any document
  quotes a different one. The number is set higher than any M5Stack release on purpose
  (see "Stock firmware overwrites yours" below).
- **Hardware:** one M5Stack StackChan kit (CoreS3, ESP32-S3, 16 MB flash). Nothing here
  is useful without one, apart from the desktop tools.

## Contents

- [How to read the evidence](#how-to-read-the-evidence)
- [What is built](#what-is-built)
- [What is proven](#what-is-proven)
- [Open problems](#open-problems)
- [Not built yet](#not-built-yet)
- [How it is put together](#how-it-is-put-together)
- [Things that cost real time](#things-that-cost-real-time)
- [Development history](#development-history)

## How to read the evidence

The firmware was developed on a machine that could compile and simulate but had no USB
connection and no network route to the robot. Every flash was done by hand on a separate
Windows PC, and what came back was a boot log or a description of what happened. So this
document uses four words carefully:

| word | meaning |
|---|---|
| **seen on the robot** | observed on the real hardware, usually with a boot log |
| **desk-tested** | passes a test that needs no robot: the simulator, the mock portal, or the bridge self-test against a fake device and faked web replies |
| **built** | compiles into the firmware; never run |
| **blocked** | written, but cannot work until something else is solved |

## What is built

| area | what it is |
|---|---|
| **MAX skin** | The default face. A baked 320x240 backplate plus live lenses, glints, brows and grin. |
| **VOLT skin** | A neon HUD face drawn only with LVGL shapes and shadow glow. No bitmap. |
| **Skin factory** | `SkinBase` + `create_avatar()`. The face is a runtime choice, not a hardcode. |
| **Live skin switching** | `StackChanAvatarDisplay::SwapSkin()` changes the face without a reboot, from the portal or from the on-device CHARACTER screen. If the AI screen has not built an avatar yet, it takes effect when that screen next opens. |
| **Gaze** | `GazeModifier`: the eyes lead the head into a turn and settle when it stops. Works on every skin. |
| **Web portal** | Port 80 and mDNS (`http://stackchan.local/`). Overview, network, time, AI backend, personas, memory, face and body, music, wake word, update. |
| **Network settings** | DHCP or static address, mask, gateway, DNS, hostname, with an automatic rollback. |
| **Time** | Three NTP servers and a POSIX timezone rule, applied live. |
| **AI backend** | WebSocket address, token, and a provider + tier preference sent to the server. |
| **Personas** | Up to 8 named system prompts, each with a voice, a speed and a face. |
| **Memory** | Notes written in the portal and added to every conversation, plus a per-device override of how many turns the bridge remembers. Bridge only. |
| **Wake word** | Eleven trained phrases held in flash, picked in the portal. |
| **Info screens** | CLOCK (no network needed), WEATHER (Open-Meteo, refreshed every 10 minutes), STOCKS (one ticker from Yahoo Finance's public chart endpoint, which is undocumented and may change; refreshed every minute). Both keep the last good reading if a fetch fails. |
| **Bridge server** | `server/bridge`: a small Python server that lets the robot talk through Claude, ChatGPT, Groq, Gemini, OpenRouter's free models, Cerebras, Mistral or a local Ollama, with its own sign-in page. |
| **Tools for the model** | The robot's own tools (head, LEDs, reminders, gestures), the bridge's weather and Pi-hole tools, and any external MCP server (Claude only). |
| **Screensaver setting** | The launcher menu's idle screensaver: on or off, timeout, style. Applied live. |
| **Stock-updater guard** | The stock firmware's silent self-replacement is switched off. |
| **Simulator** | `workshop/sim` renders the real skin code to PNG on a desktop. |
| **Recovery kit** | `workshop/kit`: Windows scripts to diagnose, erase, flash, read the boot log, find the robot, restore stock. |

## What is proven

| feature | evidence |
|---|---|
| MAX face on the real screen | **Seen on the robot.** |
| Portal served by the robot | **Seen on the robot.** A crash in an early build (the `%llu` bug below) was found there. |
| A trained wake word fires | **Seen on the robot.** After the two wake-word fixes below, the boot log showed `Audio detection task started` and `Wake word detected: Hi,Stack Chan`. |
| Stock firmware replacing a custom build | **Seen on the robot.** That is why the guard exists. |
| The conversation after the wake word | Fixed in code (audio task stacks moved to PSRAM). **Not confirmed** on the robot. |
| Persona face picker | Fixed in code. **Not confirmed** on the robot. |
| Chunked firmware upload | Built. No hardware confirmation on record. |
| VOLT face, gaze | Desk-tested in the simulator (VOLT) and built (gaze). No written hardware confirmation. |
| Live skin switching; CHARACTER, CLOCK, WEATHER and STOCKS screens | Built. No hardware run on record. |
| `play_gesture` tool | Built. No run on the robot on record. |
| Bridge: every provider | Desk-tested: `selftest.py` runs 137 checks against a fake robot and faked providers. **No real conversation has happened**, and the bridge has never been connected to the robot. Provider calls are written to each vendor's documented API and have never run with a working key. |
| Bridge sign-in page | Driven in a browser with deliberately wrong keys, to see each provider's real rejection. |
| Weather and Pi-hole tools | Desk-tested with faked web replies. The Pi-hole v6 path is written from Pi-hole's API notes and has never met a real v6 install. |
| xiaozhi.me relay | Handshake, tool list and keep-alive were checked against the real service. A real tool call was not. |
| SD-card music, `dance_to_song` | **Blocked** on the GPIO35 conflict below. |
| KIE.ai media setting | A stored setting. Nothing calls KIE.ai. |
| Windows kit scripts | The flash and erase scripts were used on the real robot in earlier forms. There is no record either way for the others. The device finder was changed for this repository (the robot's MAC is now an optional argument, and with none it asks each Espressif device whether it is the portal); its PowerShell parses cleanly and the MAC handling was tested, but the network sweep has not been run since. The restore script now looks for `stock\StackChan-UserDemo.bin` with no version in the name. |

The last full build from a clean checkout is recorded in the README's "Build and flash"
section. A clean build shows the code compiles. It says nothing about how it runs.

## Open problems

- **The conversation pipeline fix is unconfirmed.** On the first hardware flash of the
  eleven-phrase build, the wake word fired and then nothing followed. The cause found was
  a capture task that silently failed to start (see "Tasks that never start"). The fix is
  in, but nobody has yet reported a conversation working on the robot with it.
- **The persona face picker fix is unconfirmed** for the same reason.
- **No card can be mounted** (GPIO35, below), so music upload and `dance_to_song` do nothing.
- **The bridge is untested in real use.** Treat the first live conversation as the real
  test, and run it with `BRIDGE_LOG_LEVEL=DEBUG`.
- **Not yet reviewed:** the SD-card music queue, the weather and Pi-hole tools against
  real services, the OpenAI-style tool-calling loop against a real provider, the gesture
  tool, and the memory notes round trip on hardware.
- **The same unsafe pattern may exist elsewhere.** Three bugs here were a task created
  with `xTaskCreate` on internal RAM with the return value ignored. The rest of the
  upstream tree has not been searched for the same shape.
- **Portal display bugs.** On the Face and Wake word pages the skin and phrase names are
  nearly unreadable: the label uses the class `t`, which is also the toast class, and the
  button sets no text colour. The Face page also still says "applies on the next boot",
  which is no longer true.
- **"Restore stock firmware" is not configured in a default build.** It needs the address
  of an app image you host yourself; see `workshop/kit/README.md`. Until then the button
  only explains what to do.
- **The AI screen's sleep timer is still hardcoded** (`PowerSaveTimer` in
  `hal/board/stackchan.cc`). Only the launcher menu's screensaver has a portal setting.
- **The portal's own AI card says "needs ChatGPT signed in"** for hearing and voice, though
  the bridge also accepts Groq for both.
- The repository is three commits behind `m5stack/StackChan`.

## Not built yet

- A BMO skin (flat face, dot eyes, small smile). The next one wanted.
- A photo as the face. The asset path it needs already works (`photo2asset.py`).
- More than the six stock emotions. That needs the enum, every skin and the server's
  emotion mapping changed together.
- Tools that let the model drive the face directly. Today it can move the head and LEDs only.
- A "push to robot" button in the keyframe editor. The portal endpoints exist.
- Touch-to-talk as a fallback when the wake word is not heard.
- Wake-word models on the SD card. Investigated and not possible as hoped: the speech
  library chooses flash or SD card at compile time, all or nothing, so "eleven in flash
  plus extras on a card" does not exist as a runtime choice.
- A wholly custom wake phrase.
- A media-generation tool behind the KIE.ai setting.
- Home Assistant or MQTT.
- Decorators (angry, dizzy, heart, shy, sweat) loaded from the assets partition instead of
  compiled in.

## How it is put together

### The JSON control plane

`firmware/main/stackchan/json/json_helper.cpp` is upstream's, and it drives everything at
runtime. The Dance app feeds it over Bluetooth; the portal feeds it over HTTP. The schema
is the same either way.

```json
{"leftEye": {"x":-20,"y":0,"rotation":150,"weight":80,"size":10}}
{"yawServo": {"angle":300,"speed":400}}
{"leftRgbColor":"#FF00AA","rightRgbColor":"#00FFEE"}
```

A keyframe sequence is a JSON **array**; anything else is rejected. Each frame carries
both eyes, the mouth, both servos, both LED strips and `durationMs`.
`workshop/portal/keyframe-editor.html` writes this format. New expressions, dances and
light shows are data, not recompiles.

### Skins

`Element` → `Feature` → concrete eye and mouth classes. A skin implements `SkinBase` and is
registered in `skins.cpp`. Keep to the `Feature` contract (position −100..100, weight
0..100, size −100..100, rotation in tenths of a degree) and every existing modifier drives
the skin for free: blink, breath, lip-sync, idle motion, head-pet, IMU, gaze.

To add one: draw the art (`workshop/design/`), convert it (`photo2asset.py`), drop the
`.bin` in `firmware/main/assets/assets_bin/` (it is packed automatically), write the
class, register it, and render it in the simulator before building firmware.

### Portal

`firmware/main/portal/`. It starts from a task that waits for an address, so it works in
both boot modes. `portal.cpp` holds the server, the control-plane passthrough and the
firmware upload, the things that must keep working when everything else is broken.
`portal_api.cpp` holds the settings endpoints, `portal_config.cpp` owns stored settings,
and `portal_page.h` is the whole page as one embedded string.

```
GET  /                 the page (embedded; no filesystem needed)
GET  /api/info         version, running slot, ip, uptime, heap, skin

GET  /api/net          stored addressing + live interface state
POST /api/net          save, then apply (a static address is staged, see below)
POST /api/net/confirm  cancel a staged rollback

GET  /api/time         NTP servers, timezone, current time, sync state
POST /api/time         save and apply
POST /api/time/sync    resync now

GET  /api/ai           websocket url, whether a token is set, provider/model preference
POST /api/ai           save them (the token is write-only)
GET  /api/kie          KIE.ai model preference (stored only)
POST /api/kie
GET  /api/weather      weather location (used by the WEATHER screen and sent to the bridge)
POST /api/weather
GET  /api/stock        ticker symbol for the STOCKS screen
POST /api/stock

GET  /api/personas     saved personas + which is active
POST /api/personas     replace the list (max 8, prompts capped at 700 characters)
POST /api/persona      set the active one (also applies its face)
GET  /api/memory       notes + history_turns
POST /api/memory

GET  /api/wake         wake-word models in this build + which is selected
POST /api/wake         select one (next boot)

GET  /api/skins        available skins + active
POST /api/skin         select one (live if the AI screen has an avatar; else it answers
                       reboot_required)
GET  /api/screensaver  launcher-menu screensaver: enabled, timeout, style
POST /api/screensaver  save (applied live)
POST /api/reboot

POST /api/avatar       -> updateAvatarFromJson()
POST /api/motion       -> updateMotionFromJson()
POST /api/rgb          -> updateNeonLightFromJson()
POST /api/emotion      -> Avatar::setEmotion()

POST /api/echo         swallow a body and report the count; touches no flash
POST /api/ota          single-shot upload (curl-friendly)
POST /api/ota_url      the device downloads the image itself
POST /api/ota/begin    chunked upload: erase
POST /api/ota/chunk    chunked upload: about 32 KB, retryable
POST /api/ota/end      chunked upload: validate, switch slot, reboot
POST /api/ota/abort

GET  /api/music/list   sd_card_available + songs (always "no card" today)
POST /api/music/begin|chunk|end|abort|delete
```

To see the page with no robot: `python3 workshop/portal/mock_server.py --port 8130`.

### Where settings live

Two NVS namespaces, split on purpose.

| namespace | keys | read by |
|---|---|---|
| `stackychan` | `net_*`, `ntp*`, `wake_model`, `skin`, `personas`, `persona_id`, `ai_provider`, `ai_model`, `memory_notes`, `history_turns`, `weather_loc`, `kie_model` and more (`portal_config.cpp` has the full list) | this fork |
| `websocket` | `url`, `token`, `version` | **upstream** `WebsocketProtocol::OpenAudioChannel()` |

The AI address is written into upstream's own namespace instead of being copied into ours,
because the protocol layer already reads it on every connection. Saving in the portal
therefore takes effect on the next conversation with no glue code and no patch.

### A static address cannot strand the robot

Applying a static address kills the connection it was applied over, so the browser cannot
watch it succeed. Instead:

1. `POST /api/net` saves, answers first, then arms a 120-second revert timer and applies.
2. The page gives you a link to the new address.
3. Loading the portal there is the confirmation: it sees `revert_in > 0` and posts
   `/api/net/confirm`.
4. If nobody gets there, the timer fires, the setting goes back to DHCP and the robot reboots.

A power cut half way is covered separately: `net_probe` stays set in NVS, and
`network_recover_at_boot()` forces DHCP for that boot.

### Wake word

One engine: Espressif's WakeNet (`AfeWakeWord`), eleven trained phrases, all in flash.
An earlier build also had a typed-phrase engine (MultiNet with on-device
text-to-phoneme). It was removed to free the 2.1 MB it cost, which is what let the
trained list grow from four phrases to eleven.

Packed today: `wn9_histackchan_tts3` "Hi Stack Chan", `wn9_jarvis_tts` "Jarvis",
`wn9_computer_tts` "Computer", `wn9_himfive` "Hi M Five", `wn9_hiwalle_tts2` "Hi Wall-E",
`wn9_mycroft_tts` "Mycroft", `wn9_heywanda_tts` "Hey Wanda", `wn9_heywillow_tts`
"Hey Willow", `wn9_sophia_tts` "Sophia", `wn9_heykira_tts3` "Hey Kira",
`wn9_hijason_tts2` "Hi Jason". About 296 KB each, about 90% of the assets partition.
There is no "Alexa": that phrase also wakes any real Echo device in the same room.

To add a phrase, add a `CONFIG_SR_WN_WN9_*` line to `sdkconfig.defaults` **and** a row to
`main/portal/wake_words.h`. Edit both. Any change to the set needs a USB flash, because
the models live in the assets partition and over-the-air updates only write a program slot.

### Personas, and using a different model

**The robot contains no language model.** It streams Opus audio to a server and plays back
what comes home. Recognition, the model and the voice are all on the server. "Use Claude"
is therefore not a firmware setting; it is a choice of which server the robot points at.

What the firmware can do is say what it wants. `stackychan-persona-hello.patch` adds
fields to the protocol's opening message:

```jsonc
{"type":"hello", …,
 "persona_id":"max",
 "persona":{"id":"max","name":"MAX","prompt":"You are MAX, a fast-talking…"},
 "llm":{"provider":"anthropic","model":"claude-sonnet-5"}}
```

They are additions only, so a stock xiaozhi server ignores them and still works.
`server/bridge/` is a server that honours them. Its [README](../server/bridge/README.md)
documents the protocol, the sign-in page and every setting.

Two details that are easy to get wrong:

- A persona's voice is one of six names (`alloy`, `echo`, `fable`, `onyx`, `nova`,
  `shimmer`) plus a speed from 0.25 to 2.0. There is no pitch control, because the speech
  API has none, and a field that silently did nothing would be worse than no field.
- Once the portal has stored a custom server address, the boot-time check against the
  stock configuration server can no longer overwrite it
  (`stackychan-ota-protect-custom-url.patch`).

### Tools the model can call

- **The robot's own**, discovered automatically over the same WebSocket as the audio:
  head angles, LED colour, reminders, `play_gesture` (happy, robot, panic, look_around),
  and, only when an SD card is mounted, `list_songs` and `dance_to_song`.
- **The bridge's own:** weather (Open-Meteo, no key) and Pi-hole.
- **Any external MCP server**, through Anthropic's connector. Claude only; a setting, not code.

`xiaozhi_mcp_relay.py` is the reverse direction: it offers the weather and Pi-hole tools
to the stock xiaozhi.me cloud's own agent, for people who keep the robot on that cloud.

### SD card and music: written, blocked

The whole feature exists: chunked song upload in the portal, an MP3 decoder that feeds the
shared playback queue so music does not fight the voice, and two tools. None of it can
mount a card.

The pins are known from M5Stack's documentation: CS=GPIO4, SCK=GPIO36, MISO=GPIO35,
MOSI=GPIO37. The problem is that **the card shares the display's SPI bus, and GPIO35 is
both the card's data-in line and the display's data/command line.** The known fix flips
that pin's direction around every card transaction. ESP-IDF's convenience call
(`esp_vfs_fat_sdspi_mount()`) offers no place to hook that in, so a real fix means patching
the SD-over-SPI driver or writing a lower-level integration. Getting the timing wrong
risks the working display, and there was no way to test it, so it was left unattempted.

What happens today is deliberate and safe: `InitializeSdCard()` asks to initialise a bus
the display already owns, gets `ESP_ERR_INVALID_STATE`, logs a warning, and reports "no
card". Do not force the bus to be shared without solving GPIO35 first.

### Restore stock firmware from the portal

The Update page has a "Restore stock firmware" button. It can only do part of what the
USB kit does. A real factory reset writes the bootloader and partition table at offset
`0x0`, and no over-the-air mechanism may do that: an interrupted write there bricks the
device. So the button sends a stock **app image** through the same download path a normal
update uses.

That image has to be a bare app image, not M5Stack's public download, which is a full
merged flash image. `workshop/kit/extract_stock_app.py` slices the app out of the merged
image. You then host the result somewhere the robot can reach. There is no public address
for such a file, so `STOCK_URL` in `portal_page.h` is empty by default and the button
explains this instead of flashing. `workshop/kit/README.md` has the steps.

Afterwards: Wi-Fi and servo calibration survive, graphics may look wrong until stock
re-syncs its own assets, stock resumes updating itself, and the portal no longer exists.
The USB kit is the only guaranteed way back.

## Things that cost real time

**Stock firmware overwrites yours.** `Application::CheckNewVersion()` downloads and flashes
whatever the update server offers at boot, with no prompt, then reboots. On the robot, our
program booted from one slot, stock landed in the other and took over. It is guarded twice:
`CONFIG_STACKYCHAN_DISABLE_AUTO_OTA`, and a `PROJECT_VER` higher than any M5Stack release.
Check `App version:` in the boot log before believing anything else. `9.9.3-stackychan` is
this firmware. A plain version such as `1.4.4` means stock is running and you are
debugging the wrong thing.

**Render before you ship.** The first build went out without ever being drawn anywhere.
MAX's eyebrows were the colour of his hair and sat in the hairline, so all six emotions
looked the same. The simulator found it on its first run.

**The first build did not boot at all.** It was written as one 12.9 MB merged image. Five
separate writes at fixed offsets have worked since, and they leave the settings area
(`0x9000`) alone, which a merged write blanks.

**This tree mixes `.cc` and `.cpp`.** A search limited to `*.cpp` and `*.h` missed
`stackchan_display.cc`, which is where the AI screen builds its face. The chosen skin was
invisible in normal use for three builds while looking correct everywhere else.

**`%llu` reboots the robot.** The build uses a reduced `printf` that does not support
64-bit numbers. `/api/info` printed an uptime with `%llu`; every later argument was read
from the wrong place, a zero was treated as a string pointer, and the web task died within
seconds of the portal being open. It looked like a fault in the AI screen. The endpoint
now builds its reply with cJSON, and `workshop/tools/check_printf_formats.py` searches the
source for the same mistake. The compiler cannot catch it.

**One big upload dies; many small ones do not.** A single 3.85 MB upload failed with no
error at all. The PC and the robot were on different subnets, and a multi-megabyte request
held open across a router is the classic transfer that stops silently. Uploads are now
about 120 pieces of 32 KB, each retried on its own, and `/api/echo` exists so a network
fault can be told from a firmware fault with one click.

**Two wake-word detectors were running at once.** The fork's patch pinned the chosen
phrase in `wakenet_model_name` but never cleared the separate `wakenet_model_name_2`
field, which the library fills in by itself. The log showed
`WakeNet(wn9_histackchan_tts3,wn9_computer_tts)`. With four phrases packed it went
unnoticed. With eleven, free internal memory fell to 163 bytes, the detection task never
started, and no phrase was ever heard. The fix is one line.

**A leftover speech model can hijack the wake word.** Upstream prefers its other engine
whenever any model for it is present in the assets partition. A robot flashed without a
full erase can still hold one from an older build, and then boots cleanly, reports no
error and hears nothing. `stackychan-prefer-wakenet.patch` makes the trained-phrase engine
win unconditionally.

**Tasks that never start.** Three times, a task was created with plain `xTaskCreate`, on
the small internal memory pool, with the return value ignored: the wake-word task, the
audio capture task and the music task. Under memory pressure the creation fails and
nothing says so. Each fix is the same: give the task a stack in the 8 MB of external RAM
(`stackychan-audio-task-psram-stack.patch`).

**The speech library does not pack models in the order you list them.** The first packed
model has not always been the intended default. `wake_word_init_default()` therefore
writes the intended phrase into settings at first boot, so an empty setting cannot quietly
become whatever happened to be packed first.

**Nothing checked the size of the assets partition.** ESP-IDF checks the program size and
says nothing about assets, so an oversized image "built" and failed only at flash time.
The build now fails with the overflow in bytes (`stackychan-assets-size-guard.patch`).

**`fetch_repos.py` fails quietly.** If a patch does not apply it prints `skipped` and
carries on. The build then breaks much later with an unrelated error. It must report nine
applied patches. Also, it does not reset an existing checkout before re-applying, so run
`git checkout -- .` inside `firmware/xiaozhi-esp32/` before running it a second time.

**A new source folder needs `idf.py reconfigure`.** The source list is gathered with a
glob and cached. Add a file in a new directory and a plain build compiles cleanly, then
fails to link with missing symbols that are plainly defined.

**Write patches with `git diff`, never by hand.** A hand-typed patch is easy to get subtly
wrong (a blank context line needs a leading space; hunk counts must match), and the error
message does not point at the mistake.

**The portal page is a C++ string.** A JavaScript error in it is invisible to the compiler,
and one bad escape kills the whole script while the page still draws, which looks like a
styling problem. Run `workshop/portal/check_page.py` before every build.

**A setting that did nothing.** The portal saved one address under a storage name that no
code ever read; the firmware read a different one. The field was inert from the day it was
added. Check which name the reading side really uses.

**Rotation getters lie.** `Element::setRotation()` clamps to `0..3600`, but the skins pass
the raw value to LVGL. A negative rotation draws correctly while `getRotation()` reports
`0`, and the stock emotions depend on negatives. Never read a rotation back and write it again.

**Servo units are tenths of a degree,** clamped in firmware: yaw ±1280, pitch 30..870.

**Never put a custom skin on the setup path.** `app_setup/workers/connectivity.cpp` is
pinned to the stock skin on purpose. It is the screen you need in order to recover a robot.

More source-level notes on the upstream firmware: [`workshop/notes/FINDINGS.md`](../workshop/notes/FINDINGS.md).

## Development history

These numbers were the names of flash kits made during development. None was published
as a release of this repository, and they are not the firmware's version (see the top of
this page for that).

| kit | what changed |
|---|---|
| 1.0.0 | MAX, VOLT, skin factory, gaze. Did not boot (the merged-image write). |
| 1.0.1 to 1.0.4 | Setup path pinned to the stock skin; simulator added; eyebrow bug fixed; stock updater switched off; the AI screen's face routed through the factory, the first build where the new face was actually visible. |
| 1.1 | Web portal, mDNS, device finder. |
| 1.2 | Upload rebuilt as chunked pieces; `/api/echo` network test. |
| 1.3 | Portal rebuilt to configure the whole device; four wake phrases; the bridge added. |
| 1.4 | Typed wake phrases; assets partition grown to 5.69 MB (USB flash needed); assets size guard; the `%llu` reboot loop found and fixed. |
| 1.5 | Bridge relays the robot's own tools and external MCP servers; screensaver setting; "Restore stock" button. |
| 1.6 | Typed wake phrases removed, trained phrases four to eleven; gesture tool; tool calling on OpenAI-style providers; weather and Pi-hole tools; memory notes; persona voice and speed; SD-card music (blocked); xiaozhi.me relay. Its first hardware flash found four bugs: two in the wake word (fixed and confirmed), the persona face picker and the capture task (fixed, unconfirmed). |
| 1.7 | Memory fully in the portal; live skin switching and the CHARACTER screen; provider + tier picker; CLOCK, WEATHER and STOCKS screens; a third unchecked-task bug fixed in the music player. |
| after 1.7 | Bridge only: the sign-in page, then the free providers and Groq speech. |
