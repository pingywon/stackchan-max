/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 *
 * Persistent device configuration, owned by the portal.
 *
 * Everything the portal can change lives in NVS so it survives a reboot and an OTA. Two
 * namespaces are in play and the split matters:
 *
 *   "stackychan"  ours     network, time, wake word, skin, personas
 *   "websocket"   upstream url + token for the AI backend, read by WebsocketProtocol
 *
 * We deliberately write the upstream namespace rather than inventing our own copy. The
 * xiaozhi protocol layer reads "websocket"/url at every connection attempt, so a value
 * written here takes effect on the next conversation with no glue code and no patch.
 *
 * Static addressing carries a real risk: a wrong gateway or netmask takes the portal off
 * the network, and the portal is how you would fix it. So a static apply is staged —
 * see arm_network_revert() — and rolls back to DHCP unless the browser confirms it can
 * still reach the device at the new address.
 */
#pragma once

#include <string>
#include <vector>

namespace stackchan::portal::config {

/* ------------------------------------------------------------------ network -- */

struct NetworkConfig {
    bool static_ip = false;  //!< false = DHCP
    std::string ip;
    std::string netmask = "255.255.255.0";
    std::string gateway;
    std::string dns1;
    std::string dns2;
    std::string hostname = "stackchan";
};

NetworkConfig network_get();
void network_save(const NetworkConfig& cfg);

/** Live interface state, read from the netif rather than from settings. */
struct NetworkStatus {
    std::string ip;
    std::string netmask;
    std::string gateway;
    std::string dns1;
    std::string dns2;
    std::string mac;
    std::string ssid;
    int rssi     = 0;
    bool dhcp    = true;  //!< true when the DHCP client is running on the STA interface
    bool link_up = false;
};

NetworkStatus network_status();

/**
 * @brief Apply the stored addressing mode to the live STA interface.
 *
 * Called at portal start and again whenever the config is saved. Applying static
 * addressing changes the device's IP immediately, which drops every open socket.
 */
bool network_apply();

/**
 * @brief Stage a revert to DHCP unless confirmed.
 *
 * Called right before a static apply. If network_confirm() is not called within
 * `seconds`, the device reverts to DHCP and reboots — so a typo costs a wait, not a
 * cable. The portal page confirms automatically once it has re-reached the device.
 */
void arm_network_revert(int seconds);

/** @brief Cancel a pending revert. The new addressing is now trusted. */
void network_confirm();

/** @brief Seconds left on a pending revert, or 0 when nothing is staged. */
int network_revert_remaining();

/**
 * @brief Roll back a static config that never came up.
 *
 * Called early at boot. If the previous static apply was never confirmed, the settings
 * are forced back to DHCP before the interface is brought up.
 */
void network_recover_at_boot();

/* --------------------------------------------------------------------- time -- */

struct TimeConfig {
    std::string ntp1 = "pool.ntp.org";
    std::string ntp2 = "time.google.com";
    std::string ntp3 = "time.cloudflare.com";
    std::string tz   = "EST5EDT,M3.2.0,M11.1.0";  //!< POSIX TZ string
    bool enabled     = true;
};

TimeConfig time_get();
void time_save(const TimeConfig& cfg);

/** @brief (Re)start SNTP with the stored servers and apply the stored timezone. */
void time_apply();

/** @brief Force an immediate resync. Returns false when SNTP is disabled. */
bool time_sync_now();

/** @brief Local time as ISO-8601, plus whether the clock has ever been set. */
std::string time_now_iso();
bool time_is_synced();

/* ---------------------------------------------------------------- ai server -- */

struct AiConfig {
    std::string url;       //!< websocket endpoint, ws:// or wss://
    std::string token;     //!< sent as the Authorization header
    int version = 1;       //!< xiaozhi protocol version
    std::string ota_url;   //!< config/OTA endpoint the firmware polls at boot
    std::string provider;  //!< advisory: which LLM the backend should use
    std::string model;     //!< advisory: which model of that provider
};

AiConfig ai_get();
void ai_save(const AiConfig& cfg);

/**
 * @brief Re-derive the custom_backend flag from the saved URL, at boot.
 *
 * The flag is only ever written by ai_save(). A device with a custom backend URL saved
 * before that flag existed (any pre-v1.6.0 upgrade that isn't a full NVS-erasing flash)
 * would otherwise boot with the flag still at its default -- the exact window
 * stackychan-ota-protect-custom-url.patch exists to close. Call before the OTA version
 * check runs.
 */
void custom_backend_migrate_at_boot();

/* --------------------------------------------------------------------- kie.ai -- */

/**
 * @brief Connection + model preference for KIE.ai (image/video generation gateway).
 *
 * Config only for now — no on-device or bridge tool calls into KIE.ai yet. This just
 * gets the model choice to the bridge (via the persona-hello patch, same mechanism as
 * provider/model above) so it's ready whenever that tool gets built.
 */
struct KieConfig {
    std::string model;  //!< e.g. "kling-2.6/text-to-video"
};

KieConfig kie_get();
void kie_save(const KieConfig& cfg);

/* -------------------------------------------------------------------- weather -- */

/**
 * @brief Where the weather tool answers for — a place name or "lat,lon".
 *
 * Round-trips to the bridge the same way (persona-hello patch); the bridge's own
 * WEATHER_LOCATION env var is the fallback/default when a device hasn't set one.
 */
struct WeatherConfig {
    std::string location;  //!< e.g. "Chicago, IL" or "41.88,-87.63"
};

WeatherConfig weather_get();
void weather_save(const WeatherConfig& cfg);

/* --------------------------------------------------------------------- stock -- */

/**
 * @brief Ticker symbol shown by the on-device Stocks screen (e.g. "AAPL").
 *
 * Device-local only -- unlike persona/weather/memory this never round-trips to the
 * bridge, since price lookups happen directly from the device (see app_stock).
 */
struct StockConfig {
    std::string symbol;  //!< e.g. "AAPL", empty = not configured
};

StockConfig stock_get();
void stock_save(const StockConfig& cfg);

/* ----------------------------------------------------------------- personas -- */

struct Persona {
    std::string id;
    std::string name;
    std::string prompt;
    std::string voice;   //!< one of the fixed OpenAI-compatible TTS voice names, or empty
    float speed = 1.0f;  //!< TTS speed, 0.25..4.0 — matches the OpenAI speech API's range
    int skin = -1;  //!< skin to switch to when selected, -1 to leave alone
};

std::vector<Persona> personas_get();
void personas_save(const std::vector<Persona>& list);

std::string persona_active_id();
bool persona_set_active(const std::string& id);

/**
 * @brief The active persona as a JSON object, or "null".
 *
 * This is what gets attached to the protocol hello so the backend knows which character
 * it is voicing. Kept as a string because that is the only form the patch needs.
 */
std::string persona_active_json();

/* ------------------------------------------------------------------- memory -- */

/**
 * @brief Short-term memory notes — owner-edited, global, separate from personas.
 *
 * Not auto-learned by the LLM mid-conversation; just a portal-editable text area
 * ("things to remember") that gets prepended to the system prompt every conversation,
 * regardless of which persona is active. Round-trips to the bridge the same way
 * provider/model/persona do, via the persona-hello patch.
 *
 * history_turns is a different concept living in the same portal card because that is
 * where people look for it: how many user/assistant turn-pairs the *bridge* keeps in a live
 * conversation's rolling context before trimming the oldest -- true short-term memory,
 * as opposed to `notes`' fixed owner-written facts. It's ephemeral (in-process RAM on the
 * bridge, gone on reconnect) and ONLY takes effect when running this fork's own
 * server/bridge -- xiaozhi.me's stock cloud service has no concept of it. 0 means "use
 * the bridge's own BRIDGE_HISTORY_TURNS default" rather than forcing a value, so a device
 * that's never touched this setting doesn't override a value someone set directly on the
 * bridge process.
 */
struct MemoryConfig {
    std::string notes;
    int history_turns = 0;
};

MemoryConfig memory_get();
void memory_save(const MemoryConfig& cfg);

/* ---------------------------------------------------------------- wake word -- */

struct WakeWordInfo {
    std::string model;   //!< srmodel directory name, e.g. "wn9_histackchan_tts3"
    std::string phrase;  //!< spoken phrase, e.g. "Hi Stack Chan"
    bool active = false;
};

/** @brief Wake word models actually present in the assets partition. */
std::vector<WakeWordInfo> wake_words_available();

/**
 * @brief Write the intended default into NVS if nothing is stored yet.
 *
 * Necessary, not cosmetic. ESP-SR packs models into srmodels.bin in whatever order it
 * walks the config, and with no stored preference the detector takes the first one it
 * finds — which is currently "Alexa", not "Hi Stack Chan". Seeding the first entry of
 * WAKE_WORD_MODELS makes the default explicit and keeps the portal's idea of the active
 * phrase identical to the device's.
 */
void wake_word_init_default();

/** @brief Selected model name. Empty means "first model found". */
std::string wake_word_get();

/** @brief Persist the selection. Takes effect on the next boot. */
bool wake_word_set(const std::string& model);

/* -------------------------------------------------------------- screensaver -- */

struct ScreensaverConfig {
    bool enabled     = true;
    int timeout_ms   = 30000;  //!< idle time on the launcher menu before it shows
    int style        = 0;      //!< 0 = bouncing logo, 1 = blank (no graphics)
};

ScreensaverConfig screensaver_get();
void screensaver_save(const ScreensaverConfig& cfg);

/* --------------------------------------------------------------------- skin -- */

/** @brief Load the stored skin and apply it. Called once during startup. */
void skin_restore();

/** @brief Persist and apply a skin id. */
bool skin_set(int id);

}  // namespace stackchan::portal::config
