/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 */
#include "portal_config.h"
#include "wake_words.h"

#include <settings.h>

#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <esp_wifi.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cJSON.h>
#include <mooncake_log.h>

#include <cstdio>
#include <cstring>
#include <cctype>
#include <ctime>
#include <sys/time.h>

#include <stackchan/avatar/skins/skins.h>
#include <hal/hal.h>
#include <hal/board/hal_bridge.h>

static const char* _tag = "portal-cfg";

namespace stackchan::portal::config {

namespace {

constexpr const char* NS = "stackychan";

/** NVS keys are capped at 15 characters — these are all deliberately short. */
constexpr const char* K_NET_STATIC = "net_static";
constexpr const char* K_NET_IP     = "net_ip";
constexpr const char* K_NET_MASK   = "net_mask";
constexpr const char* K_NET_GW     = "net_gw";
constexpr const char* K_NET_DNS1   = "net_dns1";
constexpr const char* K_NET_DNS2   = "net_dns2";
constexpr const char* K_NET_HOST   = "net_host";
constexpr const char* K_NET_PROBE  = "net_probe";

constexpr const char* K_NTP1    = "ntp1";
constexpr const char* K_NTP2    = "ntp2";
constexpr const char* K_NTP3    = "ntp3";
constexpr const char* K_NTP_ON  = "ntp_on";

constexpr const char* K_AI_PROV       = "ai_provider";
constexpr const char* K_AI_MODEL      = "ai_model";
constexpr const char* K_CUSTOM_BACKEND = "custom_backend";

constexpr const char* K_KIE_MODEL = "kie_model";
constexpr const char* K_WEATHER_LOC = "weather_loc";
constexpr const char* K_STOCK_SYMBOL = "stock_sym";

constexpr const char* K_PERSONAS = "personas";
constexpr const char* K_PERSONA  = "persona_id";

constexpr const char* K_MEMORY_NOTES = "memory_notes";
constexpr const char* K_HISTORY_TURNS = "history_turns";

constexpr const char* K_WAKE = "wake_model";
constexpr const char* K_SKIN = "skin";

constexpr const char* K_SCR_ON    = "scr_on";
constexpr const char* K_SCR_MS    = "scr_ms";
constexpr const char* K_SCR_STYLE = "scr_style";

esp_netif_t* sta_netif()
{
    return esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
}

std::string ip_to_string(uint32_t addr)
{
    esp_ip4_addr_t a;
    a.addr = addr;
    char buf[16];
    std::snprintf(buf, sizeof(buf), IPSTR, IP2STR(&a));
    return buf;
}

/** Parse dotted-quad. Returns false on anything that is not four 0..255 octets. */
bool parse_ip(const std::string& text, uint32_t& out)
{
    if (text.empty()) {
        return false;
    }
    unsigned a, b, c, d;
    char extra;
    if (std::sscanf(text.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4) {
        return false;
    }
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        return false;
    }
    out = esp_netif_htonl(static_cast<uint32_t>((a << 24) | (b << 16) | (c << 8) | d));
    return true;
}

/* The staged-revert timer. Fires only when a static apply is never confirmed. */
esp_timer_handle_t g_revert_timer  = nullptr;
int64_t g_revert_deadline_us       = 0;

void revert_task(void*)
{
    mclog::tagError(_tag,
                    "static address was never confirmed — reverting to DHCP and rebooting");
    {
        Settings s(NS, true);
        s.SetBool(K_NET_STATIC, false);
        s.SetInt(K_NET_PROBE, 0);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

void force_dhcp_and_reboot(void*)
{
    // esp_timer callbacks run on the timer task, which has a small stack and is not the
    // place to open NVS. Hand the work to a task with room to do it.
    xTaskCreate(revert_task, "net_revert", 4096, nullptr, 5, nullptr);
}

}  // namespace

/* ------------------------------------------------------------------ network -- */

NetworkConfig network_get()
{
    Settings s(NS, false);
    NetworkConfig cfg;
    cfg.static_ip = s.GetBool(K_NET_STATIC, false);
    cfg.ip        = s.GetString(K_NET_IP, "");
    cfg.netmask   = s.GetString(K_NET_MASK, "255.255.255.0");
    cfg.gateway   = s.GetString(K_NET_GW, "");
    cfg.dns1      = s.GetString(K_NET_DNS1, "");
    cfg.dns2      = s.GetString(K_NET_DNS2, "");
    cfg.hostname  = s.GetString(K_NET_HOST, "stackchan");
    return cfg;
}

void network_save(const NetworkConfig& cfg)
{
    Settings s(NS, true);
    s.SetBool(K_NET_STATIC, cfg.static_ip);
    s.SetString(K_NET_IP, cfg.ip);
    s.SetString(K_NET_MASK, cfg.netmask);
    s.SetString(K_NET_GW, cfg.gateway);
    s.SetString(K_NET_DNS1, cfg.dns1);
    s.SetString(K_NET_DNS2, cfg.dns2);
    s.SetString(K_NET_HOST, cfg.hostname);
}

NetworkStatus network_status()
{
    NetworkStatus st;
    esp_netif_t* netif = sta_netif();

    uint8_t mac[6] = {0};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        char buf[18];
        std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
                      mac[3], mac[4], mac[5]);
        st.mac = buf;
    }

    if (netif == nullptr) {
        return st;
    }

    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
        st.ip      = ip_to_string(ip.ip.addr);
        st.netmask = ip_to_string(ip.netmask.addr);
        st.gateway = ip_to_string(ip.gw.addr);
        st.link_up = ip.ip.addr != 0;
    }

    esp_netif_dns_info_t dns;
    if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
        st.dns1 = ip_to_string(dns.ip.u_addr.ip4.addr);
    }
    if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns) == ESP_OK) {
        st.dns2 = ip_to_string(dns.ip.u_addr.ip4.addr);
    }

    esp_netif_dhcp_status_t dhcp_status;
    if (esp_netif_dhcpc_get_status(netif, &dhcp_status) == ESP_OK) {
        st.dhcp = (dhcp_status == ESP_NETIF_DHCP_STARTED);
    }

    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        st.ssid = reinterpret_cast<const char*>(ap.ssid);
        st.rssi = ap.rssi;
    }

    return st;
}

bool network_apply()
{
    esp_netif_t* netif = sta_netif();
    if (netif == nullptr) {
        mclog::tagWarn(_tag, "no STA interface yet, cannot apply addressing");
        return false;
    }

    NetworkConfig cfg = network_get();

    if (!cfg.hostname.empty()) {
        esp_netif_set_hostname(netif, cfg.hostname.c_str());
    }

    if (!cfg.static_ip) {
        // Idempotent: starting an already-started client is not an error worth reporting.
        esp_netif_dhcpc_start(netif);
        mclog::tagInfo(_tag, "addressing: DHCP");
        return true;
    }

    esp_netif_ip_info_t ip = {};
    if (!parse_ip(cfg.ip, ip.ip.addr) || !parse_ip(cfg.netmask, ip.netmask.addr)) {
        mclog::tagError(_tag, "static config rejected: ip or netmask unparseable");
        return false;
    }
    if (!cfg.gateway.empty() && !parse_ip(cfg.gateway, ip.gw.addr)) {
        mclog::tagError(_tag, "static config rejected: gateway unparseable");
        return false;
    }

    esp_netif_dhcpc_stop(netif);
    esp_err_t err = esp_netif_set_ip_info(netif, &ip);
    if (err != ESP_OK) {
        mclog::tagError(_tag, "set_ip_info failed: {}", esp_err_to_name(err));
        return false;
    }

    uint32_t dns_addr = 0;
    if (parse_ip(cfg.dns1, dns_addr)) {
        esp_netif_dns_info_t dns   = {};
        dns.ip.type                = ESP_IPADDR_TYPE_V4;
        dns.ip.u_addr.ip4.addr     = dns_addr;
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
    }
    if (parse_ip(cfg.dns2, dns_addr)) {
        esp_netif_dns_info_t dns   = {};
        dns.ip.type                = ESP_IPADDR_TYPE_V4;
        dns.ip.u_addr.ip4.addr     = dns_addr;
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns);
    }

    mclog::tagInfo(_tag, "addressing: static {} / {} via {}", cfg.ip, cfg.netmask, cfg.gateway);
    return true;
}

void arm_network_revert(int seconds)
{
    if (seconds <= 0) {
        return;
    }
    {
        Settings s(NS, true);
        s.SetInt(K_NET_PROBE, 1);
    }

    if (g_revert_timer != nullptr) {
        esp_timer_stop(g_revert_timer);
        esp_timer_delete(g_revert_timer);
        g_revert_timer = nullptr;
    }

    esp_timer_create_args_t args = {};
    args.callback                = &force_dhcp_and_reboot;
    args.name                    = "net_revert";
    if (esp_timer_create(&args, &g_revert_timer) != ESP_OK) {
        mclog::tagError(_tag, "could not create the revert timer — applying anyway");
        return;
    }
    esp_timer_start_once(g_revert_timer, static_cast<uint64_t>(seconds) * 1000000ULL);
    g_revert_deadline_us = esp_timer_get_time() + static_cast<int64_t>(seconds) * 1000000LL;
    mclog::tagInfo(_tag, "static apply staged — reverting in {}s unless confirmed", seconds);
}

void network_confirm()
{
    if (g_revert_timer != nullptr) {
        esp_timer_stop(g_revert_timer);
        esp_timer_delete(g_revert_timer);
        g_revert_timer = nullptr;
    }
    g_revert_deadline_us = 0;
    Settings s(NS, true);
    s.SetInt(K_NET_PROBE, 0);
    mclog::tagInfo(_tag, "static addressing confirmed reachable");
}

int network_revert_remaining()
{
    if (g_revert_deadline_us == 0) {
        return 0;
    }
    int64_t left = g_revert_deadline_us - esp_timer_get_time();
    return left <= 0 ? 0 : static_cast<int>(left / 1000000);
}

void network_recover_at_boot()
{
    Settings s(NS, true);
    if (s.GetInt(K_NET_PROBE, 0) == 1) {
        mclog::tagError(_tag,
                        "previous static address never confirmed — forcing DHCP for this boot");
        s.SetBool(K_NET_STATIC, false);
        s.SetInt(K_NET_PROBE, 0);
    }
}

/* --------------------------------------------------------------------- time -- */

TimeConfig time_get()
{
    Settings s(NS, false);
    TimeConfig cfg;
    cfg.ntp1    = s.GetString(K_NTP1, "pool.ntp.org");
    cfg.ntp2    = s.GetString(K_NTP2, "time.google.com");
    cfg.ntp3    = s.GetString(K_NTP3, "time.cloudflare.com");
    cfg.enabled = s.GetBool(K_NTP_ON, true);
    cfg.tz      = GetHAL().getTimezone();
    if (cfg.tz.empty()) {
        cfg.tz = "EST5EDT,M3.2.0,M11.1.0";
    }
    return cfg;
}

void time_save(const TimeConfig& cfg)
{
    {
        Settings s(NS, true);
        s.SetString(K_NTP1, cfg.ntp1);
        s.SetString(K_NTP2, cfg.ntp2);
        s.SetString(K_NTP3, cfg.ntp3);
        s.SetBool(K_NTP_ON, cfg.enabled);
    }
    // The HAL owns the timezone: it persists it and keeps the RTC in step.
    GetHAL().setTimezone(cfg.tz);
}

void time_apply()
{
    TimeConfig cfg = time_get();

    setenv("TZ", cfg.tz.c_str(), 1);
    tzset();

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    if (!cfg.enabled) {
        mclog::tagInfo(_tag, "SNTP disabled by configuration");
        return;
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    // lwip stores the pointer rather than the string, so these have to outlive the call.
    // They are also cleared and rewritten every time: leaving a stale pointer in a slot
    // the new config does not use would keep querying a server the user just removed.
    static std::string held[3];
    int slot = 0;
    for (const std::string* server : {&cfg.ntp1, &cfg.ntp2, &cfg.ntp3}) {
        if (!server->empty()) {
            held[slot] = *server;
            esp_sntp_setservername(slot, held[slot].c_str());
            slot++;
        }
    }
    for (int i = slot; i < 3; i++) {
        held[i].clear();
        esp_sntp_setservername(i, nullptr);
    }
    if (slot == 0) {
        mclog::tagWarn(_tag, "SNTP enabled but no servers configured");
        return;
    }

    esp_sntp_init();
    mclog::tagInfo(_tag, "SNTP started with {} server(s), TZ={}", slot, cfg.tz);
}

bool time_sync_now()
{
    TimeConfig cfg = time_get();
    if (!cfg.enabled) {
        return false;
    }
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    time_apply();
    return true;
}

bool time_is_synced()
{
    // Anything before 2023 means the clock has never been set from a real source.
    time_t now = 0;
    std::time(&now);
    return now > 1672531200;
}

std::string time_now_iso()
{
    time_t now = 0;
    std::time(&now);
    struct tm local;
    localtime_r(&now, &local);
    char buf[40];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
    return buf;
}

/* ---------------------------------------------------------------- ai server -- */

void custom_backend_migrate_at_boot()
{
    // K_CUSTOM_BACKEND is only ever written by ai_save() -- a device that had a custom
    // backend URL saved before that flag existed (any v1.5.x upgrade that isn't a full
    // NVS-erasing flash) boots with url set but the flag still at its 0 default. The OTA
    // overwrite guard (stackychan-ota-protect-custom-url.patch) reads the flag, not the
    // URL, so on exactly that first boot the stock OTA response can still silently replace
    // a configured custom backend -- the precise failure this patch exists to prevent.
    // Re-derive the flag from the actual URL at every boot, before Ota::CheckVersion() can
    // run, instead of trusting it was ever written correctly.
    Settings ws("websocket", false);
    bool has_url = !ws.GetString("url", "").empty();
    Settings s(NS, true);
    int want = has_url ? 1 : 0;
    if (s.GetInt(K_CUSTOM_BACKEND, 0) != want) {
        s.SetInt(K_CUSTOM_BACKEND, want);
        mclog::tagInfo(_tag, "custom_backend flag migrated to {} (url {})", want,
                       has_url ? "present" : "empty");
    }
}

AiConfig ai_get()
{
    AiConfig cfg;
    {
        // Upstream's namespace: WebsocketProtocol reads these directly at connect time.
        Settings ws("websocket", false);
        cfg.url     = ws.GetString("url", "");
        cfg.token   = ws.GetString("token", "");
        cfg.version = ws.GetInt("version", 1);
    }
    {
        // Ota::GetCheckVersionUrl() reads this exact namespace/key (firmware/xiaozhi-esp32/main/ota.cc).
        // It used to live in namespace "ota" here, which nothing else ever read - the field
        // was silently inert. Write into the location upstream actually consults instead.
        Settings ota("wifi", false);
        cfg.ota_url = ota.GetString("ota_url", "");
    }
    {
        Settings s(NS, false);
        cfg.provider = s.GetString(K_AI_PROV, "");
        cfg.model    = s.GetString(K_AI_MODEL, "");
    }
    return cfg;
}

void ai_save(const AiConfig& cfg)
{
    {
        Settings ws("websocket", true);
        ws.SetString("url", cfg.url);
        ws.SetString("token", cfg.token);
        if (cfg.version > 0) {
            ws.SetInt("version", cfg.version);
        }
    }
    {
        Settings ota("wifi", true);
        ota.SetString("ota_url", cfg.ota_url);
    }
    {
        Settings s(NS, true);
        s.SetString(K_AI_PROV, cfg.provider);
        s.SetString(K_AI_MODEL, cfg.model);
        // Read by Ota::CheckVersion() (stackychan-ota-protect-custom-url.patch) so the
        // stock OTA response's own websocket/mqtt config can never silently overwrite a
        // custom bridge the owner configured here.
        s.SetInt(K_CUSTOM_BACKEND, cfg.url.empty() ? 0 : 1);
    }
    mclog::tagInfo(_tag, "AI endpoint set to '{}' (provider '{}', model '{}')", cfg.url,
                   cfg.provider, cfg.model);
}

/* --------------------------------------------------------------------- kie.ai -- */

KieConfig kie_get()
{
    Settings s(NS, false);
    KieConfig cfg;
    cfg.model = s.GetString(K_KIE_MODEL, "");
    return cfg;
}

void kie_save(const KieConfig& cfg)
{
    Settings s(NS, true);
    s.SetString(K_KIE_MODEL, cfg.model);
    mclog::tagInfo(_tag, "KIE.ai model preference set to '{}'", cfg.model);
}

/* -------------------------------------------------------------------- weather -- */

WeatherConfig weather_get()
{
    Settings s(NS, false);
    WeatherConfig cfg;
    cfg.location = s.GetString(K_WEATHER_LOC, "");
    return cfg;
}

void weather_save(const WeatherConfig& cfg)
{
    Settings s(NS, true);
    s.SetString(K_WEATHER_LOC, cfg.location);
    mclog::tagInfo(_tag, "weather location set to '{}'", cfg.location);
}

/* --------------------------------------------------------------------- stock -- */

StockConfig stock_get()
{
    Settings s(NS, false);
    StockConfig cfg;
    cfg.symbol = s.GetString(K_STOCK_SYMBOL, "");
    return cfg;
}

void stock_save(const StockConfig& cfg)
{
    Settings s(NS, true);
    s.SetString(K_STOCK_SYMBOL, cfg.symbol);
    mclog::tagInfo(_tag, "stock symbol set to '{}'", cfg.symbol);
}

/* ----------------------------------------------------------------- personas -- */

std::vector<Persona> personas_get()
{
    std::vector<Persona> out;
    Settings s(NS, false);
    std::string raw = s.GetString(K_PERSONAS, "");
    if (raw.empty()) {
        return out;
    }

    cJSON* root = cJSON_Parse(raw.c_str());
    if (root == nullptr || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return out;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root)
    {
        Persona p;
        cJSON* v = cJSON_GetObjectItem(item, "id");
        if (cJSON_IsString(v)) p.id = v->valuestring;
        v = cJSON_GetObjectItem(item, "name");
        if (cJSON_IsString(v)) p.name = v->valuestring;
        v = cJSON_GetObjectItem(item, "prompt");
        if (cJSON_IsString(v)) p.prompt = v->valuestring;
        v = cJSON_GetObjectItem(item, "voice");
        if (cJSON_IsString(v)) p.voice = v->valuestring;
        v = cJSON_GetObjectItem(item, "skin");
        if (cJSON_IsNumber(v)) p.skin = v->valueint;
        v = cJSON_GetObjectItem(item, "speed");
        if (cJSON_IsNumber(v)) {
            p.speed = static_cast<float>(v->valuedouble);
            if (p.speed < 0.25f || p.speed > 4.0f) p.speed = 1.0f;
        }
        if (!p.id.empty()) {
            out.push_back(p);
        }
    }
    cJSON_Delete(root);
    return out;
}

void personas_save(const std::vector<Persona>& list)
{
    cJSON* root = cJSON_CreateArray();
    for (const Persona& p : list) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", p.id.c_str());
        cJSON_AddStringToObject(o, "name", p.name.c_str());
        cJSON_AddStringToObject(o, "prompt", p.prompt.c_str());
        cJSON_AddStringToObject(o, "voice", p.voice.c_str());
        cJSON_AddNumberToObject(o, "skin", p.skin);
        cJSON_AddNumberToObject(o, "speed", p.speed);
        cJSON_AddItemToArray(root, o);
    }
    char* text = cJSON_PrintUnformatted(root);
    if (text != nullptr) {
        Settings s(NS, true);
        s.SetString(K_PERSONAS, text);
        cJSON_free(text);
    }
    cJSON_Delete(root);
}

std::string persona_active_id()
{
    Settings s(NS, false);
    return s.GetString(K_PERSONA, "");
}

bool persona_set_active(const std::string& id)
{
    {
        Settings s(NS, true);
        s.SetString(K_PERSONA, id);
    }
    // A persona may carry a face with it. Switching that here keeps the two in step
    // without the UI having to make two calls. Returns true when no face change was
    // needed (nothing to apply) or the swap actually happened live; false only when a
    // face change was needed but the AI-agent screen has no avatar to swap yet.
    bool applied_live = true;
    for (const Persona& p : personas_get()) {
        if (p.id == id && p.skin >= 0) {
            skin_set(p.skin);
            applied_live = hal_bridge::display_swap_skin(static_cast<avatar::SkinId>(p.skin));
            break;
        }
    }
    mclog::tagInfo(_tag, "active persona: '{}'", id);
    return applied_live;
}

std::string persona_active_json()
{
    std::string id = persona_active_id();
    if (id.empty()) {
        return "null";
    }
    for (const Persona& p : personas_get()) {
        if (p.id != id) {
            continue;
        }
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", p.id.c_str());
        cJSON_AddStringToObject(o, "name", p.name.c_str());
        cJSON_AddStringToObject(o, "prompt", p.prompt.c_str());
        if (!p.voice.empty()) {
            cJSON_AddStringToObject(o, "voice", p.voice.c_str());
        }
        cJSON_AddNumberToObject(o, "speed", p.speed);
        char* text = cJSON_PrintUnformatted(o);
        std::string out = text ? text : "null";
        if (text) {
            cJSON_free(text);
        }
        cJSON_Delete(o);
        return out;
    }
    return "null";
}

/* ------------------------------------------------------------------- memory -- */

MemoryConfig memory_get()
{
    Settings s(NS, false);
    MemoryConfig cfg;
    cfg.notes = s.GetString(K_MEMORY_NOTES, "");
    cfg.history_turns = s.GetInt(K_HISTORY_TURNS, 0);
    return cfg;
}

void memory_save(const MemoryConfig& cfg)
{
    Settings s(NS, true);
    s.SetString(K_MEMORY_NOTES, cfg.notes);
    s.SetInt(K_HISTORY_TURNS, cfg.history_turns);
    mclog::tagInfo(_tag, "memory notes updated ({} bytes), history_turns={}", cfg.notes.size(),
                   cfg.history_turns);
}

/* ---------------------------------------------------------------- wake word -- */

void wake_word_init_default()
{
    if (WAKE_WORD_MODEL_COUNT == 0) {
        return;
    }
    Settings s(NS, true);
    if (!s.GetString(K_WAKE, "").empty()) {
        return;
    }
    s.SetString(K_WAKE, WAKE_WORD_MODELS[0].model);
    mclog::tagInfo(_tag, "no wake word stored — defaulting to '{}' ({})",
                   WAKE_WORD_MODELS[0].model, WAKE_WORD_MODELS[0].phrase);
}

std::vector<WakeWordInfo> wake_words_available()
{
    std::string active = wake_word_get();
    std::vector<WakeWordInfo> out;
    for (int i = 0; i < WAKE_WORD_MODEL_COUNT; i++) {
        WakeWordInfo info;
        info.model  = WAKE_WORD_MODELS[i].model;
        info.phrase = WAKE_WORD_MODELS[i].phrase;
        // wake_word_init_default() guarantees a stored value, so this needs no
        // "whatever came first" guess — which would be wrong, since the packing order
        // and the order of this table are not the same.
        info.active = (active == info.model);
        out.push_back(info);
    }
    return out;
}

std::string wake_word_get()
{
    Settings s(NS, false);
    return s.GetString(K_WAKE, "");
}

bool wake_word_set(const std::string& model)
{
    bool known = model.empty();
    for (int i = 0; i < WAKE_WORD_MODEL_COUNT && !known; i++) {
        known = (model == WAKE_WORD_MODELS[i].model);
    }
    if (!known) {
        mclog::tagWarn(_tag, "refusing unknown wake word model '{}'", model);
        return false;
    }
    Settings s(NS, true);
    s.SetString(K_WAKE, model);
    mclog::tagInfo(_tag, "wake word model set to '{}' (applies on next boot)", model);
    return true;
}

/* -------------------------------------------------------------- screensaver -- */

ScreensaverConfig screensaver_get()
{
    Settings s(NS, false);
    ScreensaverConfig cfg;
    cfg.enabled    = s.GetBool(K_SCR_ON, true);
    cfg.timeout_ms = s.GetInt(K_SCR_MS, 30000);
    cfg.style      = s.GetInt(K_SCR_STYLE, 0);
    return cfg;
}

void screensaver_save(const ScreensaverConfig& cfg)
{
    Settings s(NS, true);
    s.SetBool(K_SCR_ON, cfg.enabled);
    // A timeout under a second isn't a screensaver, it's a screen that never stays on;
    // clamp rather than let the portal brick the launcher menu with a typo'd "3".
    int ms = cfg.timeout_ms < 1000 ? 1000 : cfg.timeout_ms;
    s.SetInt(K_SCR_MS, ms);
    s.SetInt(K_SCR_STYLE, cfg.style < 0 || cfg.style > 1 ? 0 : cfg.style);
}

/* --------------------------------------------------------------------- skin -- */

void skin_restore()
{
    Settings s(NS, false);
    int id = s.GetInt(K_SKIN, static_cast<int>(avatar::SkinId::Max));
    if (id >= 0 && id < static_cast<int>(avatar::SkinId::_Count)) {
        avatar::setCurrentSkin(static_cast<avatar::SkinId>(id));
    }
}

bool skin_set(int id)
{
    if (id < 0 || id >= static_cast<int>(avatar::SkinId::_Count)) {
        return false;
    }
    {
        Settings s(NS, true);
        s.SetInt(K_SKIN, id);
    }
    avatar::setCurrentSkin(static_cast<avatar::SkinId>(id));
    return true;
}

}  // namespace stackchan::portal::config
