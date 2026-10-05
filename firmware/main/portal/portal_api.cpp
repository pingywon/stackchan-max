/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 *
 * Settings endpoints.
 *
 * Every handler here follows the same shape: GET returns the current state as JSON, POST
 * takes the same shape back and persists it. Responses are built with cJSON rather than
 * snprintf because these payloads carry user-supplied text — an SSID with a quote in it
 * would otherwise produce a broken document and an unexplainable blank panel.
 *
 * The network handler is the one with teeth. A static address applied over the very link
 * you are configuring can strand the device, so POST /api/net stages the change: it arms
 * a revert timer, applies the address, and only a follow-up POST /api/net/confirm from a
 * browser that successfully re-reached the device makes it permanent.
 */
#include "portal_api.h"
#include "portal_config.h"

#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cJSON.h>
#include <mooncake_log.h>

#include <cctype>
#include <string>
#include <vector>

#include <stackchan/stackchan.h>
#include <stackchan/avatar/skins/skins.h>
#include <hal/board/hal_bridge.h>

static const char* _tag = "portal-api";

namespace stackchan::portal {

namespace {

using namespace stackchan::portal::config;

/** Read a bounded request body. Settings payloads are small by construction. */
bool read_body(httpd_req_t* req, std::string& out, size_t limit = 6144)
{
    int remaining = req->content_len;
    if (remaining < 0 || static_cast<size_t>(remaining) > limit) {
        return false;
    }
    out.clear();
    out.reserve(remaining);

    char chunk[512];
    while (remaining > 0) {
        int want = remaining > static_cast<int>(sizeof(chunk)) ? static_cast<int>(sizeof(chunk))
                                                               : remaining;
        int got = httpd_req_recv(req, chunk, want);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (got <= 0) {
            return false;
        }
        out.append(chunk, got);
        remaining -= got;
    }
    return true;
}

esp_err_t send_cjson(httpd_req_t* req, cJSON* root)
{
    char* text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not serialise response");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, text);
    cJSON_free(text);
    return err;
}

esp_err_t send_ok(httpd_req_t* req)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    return send_cjson(req, root);
}

/** Parse a body into a cJSON object, answering 400 and returning null when it will not. */
cJSON* body_object(httpd_req_t* req)
{
    std::string body;
    if (!read_body(req, body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body missing or too large");
        return nullptr;
    }
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body is not a JSON object");
        return nullptr;
    }
    return root;
}

std::string obj_str(cJSON* o, const char* key, const std::string& fallback = "")
{
    cJSON* v = cJSON_GetObjectItem(o, key);
    return cJSON_IsString(v) ? std::string(v->valuestring) : fallback;
}

int obj_int(cJSON* o, const char* key, int fallback)
{
    cJSON* v = cJSON_GetObjectItem(o, key);
    return cJSON_IsNumber(v) ? v->valueint : fallback;
}

double obj_double(cJSON* o, const char* key, double fallback)
{
    cJSON* v = cJSON_GetObjectItem(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

bool obj_bool(cJSON* o, const char* key, bool fallback)
{
    cJSON* v = cJSON_GetObjectItem(o, key);
    return cJSON_IsBool(v) ? cJSON_IsTrue(v) : fallback;
}

/* ------------------------------------------------------------------ network -- */

esp_err_t h_net_get(httpd_req_t* req)
{
    NetworkConfig cfg = network_get();
    NetworkStatus st  = network_status();

    cJSON* root = cJSON_CreateObject();

    cJSON* c = cJSON_CreateObject();
    cJSON_AddBoolToObject(c, "static_ip", cfg.static_ip);
    cJSON_AddStringToObject(c, "ip", cfg.ip.c_str());
    cJSON_AddStringToObject(c, "netmask", cfg.netmask.c_str());
    cJSON_AddStringToObject(c, "gateway", cfg.gateway.c_str());
    cJSON_AddStringToObject(c, "dns1", cfg.dns1.c_str());
    cJSON_AddStringToObject(c, "dns2", cfg.dns2.c_str());
    cJSON_AddStringToObject(c, "hostname", cfg.hostname.c_str());
    cJSON_AddItemToObject(root, "config", c);

    cJSON* s = cJSON_CreateObject();
    cJSON_AddStringToObject(s, "ip", st.ip.c_str());
    cJSON_AddStringToObject(s, "netmask", st.netmask.c_str());
    cJSON_AddStringToObject(s, "gateway", st.gateway.c_str());
    cJSON_AddStringToObject(s, "dns1", st.dns1.c_str());
    cJSON_AddStringToObject(s, "dns2", st.dns2.c_str());
    cJSON_AddStringToObject(s, "mac", st.mac.c_str());
    cJSON_AddStringToObject(s, "ssid", st.ssid.c_str());
    cJSON_AddNumberToObject(s, "rssi", st.rssi);
    cJSON_AddBoolToObject(s, "dhcp", st.dhcp);
    cJSON_AddBoolToObject(s, "link_up", st.link_up);
    cJSON_AddItemToObject(root, "status", s);

    cJSON_AddNumberToObject(root, "revert_in", network_revert_remaining());
    return send_cjson(req, root);
}

esp_err_t h_net_post(httpd_req_t* req)
{
    cJSON* body = body_object(req);
    if (body == nullptr) {
        return ESP_FAIL;
    }

    NetworkConfig cfg;
    cfg.static_ip = obj_bool(body, "static_ip", false);
    cfg.ip        = obj_str(body, "ip");
    cfg.netmask   = obj_str(body, "netmask", "255.255.255.0");
    cfg.gateway   = obj_str(body, "gateway");
    cfg.dns1      = obj_str(body, "dns1");
    cfg.dns2      = obj_str(body, "dns2");
    cfg.hostname  = obj_str(body, "hostname", "stackchan");
    int revert    = obj_int(body, "revert_seconds", 120);
    cJSON_Delete(body);

    if (cfg.static_ip && (cfg.ip.empty() || cfg.netmask.empty())) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "static mode needs at least ip + netmask");
        return ESP_FAIL;
    }

    network_save(cfg);

    // Answer before the address changes, or the reply never reaches the browser.
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "staged", cfg.static_ip);
    cJSON_AddNumberToObject(root, "revert_in", cfg.static_ip ? revert : 0);
    cJSON_AddStringToObject(root, "next_ip", cfg.static_ip ? cfg.ip.c_str() : "");
    send_cjson(req, root);

    if (cfg.static_ip) {
        arm_network_revert(revert);
    } else {
        network_confirm();  // DHCP can always be recovered from, nothing to stage.
    }

    vTaskDelay(pdMS_TO_TICKS(300));
    network_apply();
    return ESP_OK;
}

esp_err_t h_net_confirm(httpd_req_t* req)
{
    network_confirm();
    return send_ok(req);
}

/* --------------------------------------------------------------------- time -- */

esp_err_t h_time_get(httpd_req_t* req)
{
    TimeConfig cfg = time_get();
    cJSON* root    = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ntp1", cfg.ntp1.c_str());
    cJSON_AddStringToObject(root, "ntp2", cfg.ntp2.c_str());
    cJSON_AddStringToObject(root, "ntp3", cfg.ntp3.c_str());
    cJSON_AddStringToObject(root, "tz", cfg.tz.c_str());
    cJSON_AddBoolToObject(root, "enabled", cfg.enabled);
    cJSON_AddStringToObject(root, "now", time_now_iso().c_str());
    cJSON_AddBoolToObject(root, "synced", time_is_synced());
    return send_cjson(req, root);
}

esp_err_t h_time_post(httpd_req_t* req)
{
    cJSON* body = body_object(req);
    if (body == nullptr) {
        return ESP_FAIL;
    }

    TimeConfig cfg;
    cfg.ntp1    = obj_str(body, "ntp1", "pool.ntp.org");
    cfg.ntp2    = obj_str(body, "ntp2");
    cfg.ntp3    = obj_str(body, "ntp3");
    cfg.tz      = obj_str(body, "tz", "EST5EDT,M3.2.0,M11.1.0");
    cfg.enabled = obj_bool(body, "enabled", true);
    cJSON_Delete(body);

    time_save(cfg);
    time_apply();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "now", time_now_iso().c_str());
    return send_cjson(req, root);
}

esp_err_t h_time_sync(httpd_req_t* req)
{
    bool started = time_sync_now();
    cJSON* root  = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", started);
    cJSON_AddStringToObject(root, "now", time_now_iso().c_str());
    if (!started) {
        cJSON_AddStringToObject(root, "error", "SNTP is disabled");
    }
    return send_cjson(req, root);
}

/* ----------------------------------------------------------------------- ai -- */

esp_err_t h_ai_get(httpd_req_t* req)
{
    AiConfig cfg = ai_get();
    cJSON* root  = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "url", cfg.url.c_str());
    // The token is write-only from the browser's point of view: we say whether one is
    // set, never what it is, so a shared screen cannot leak it.
    cJSON_AddBoolToObject(root, "token_set", !cfg.token.empty());
    cJSON_AddNumberToObject(root, "version", cfg.version);
    cJSON_AddStringToObject(root, "ota_url", cfg.ota_url.c_str());
    cJSON_AddStringToObject(root, "provider", cfg.provider.c_str());
    cJSON_AddStringToObject(root, "model", cfg.model.c_str());
    return send_cjson(req, root);
}

esp_err_t h_ai_post(httpd_req_t* req)
{
    cJSON* body = body_object(req);
    if (body == nullptr) {
        return ESP_FAIL;
    }

    AiConfig cfg = ai_get();
    cfg.url      = obj_str(body, "url", cfg.url);
    cfg.version  = obj_int(body, "version", cfg.version);
    cfg.ota_url  = obj_str(body, "ota_url", cfg.ota_url);
    cfg.provider = obj_str(body, "provider", cfg.provider);
    cfg.model    = obj_str(body, "model", cfg.model);

    // Only overwrite the token when the field was actually sent, so saving the rest of
    // the form does not silently wipe it.
    cJSON* token = cJSON_GetObjectItem(body, "token");
    if (cJSON_IsString(token)) {
        cfg.token = token->valuestring;
    }
    cJSON_Delete(body);

    ai_save(cfg);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "note", "takes effect on the next conversation");
    return send_cjson(req, root);
}

/* -------------------------------------------------------------------- kie.ai -- */

esp_err_t h_kie_get(httpd_req_t* req)
{
    KieConfig cfg = kie_get();
    cJSON* root   = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", cfg.model.c_str());
    return send_cjson(req, root);
}

esp_err_t h_kie_post(httpd_req_t* req)
{
    cJSON* body = body_object(req);
    if (body == nullptr) {
        return ESP_FAIL;
    }
    KieConfig cfg = kie_get();
    cfg.model     = obj_str(body, "model", cfg.model);
    cJSON_Delete(body);

    kie_save(cfg);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    return send_cjson(req, root);
}

/* -------------------------------------------------------------------- weather -- */

esp_err_t h_weather_get(httpd_req_t* req)
{
    WeatherConfig cfg = weather_get();
    cJSON* root        = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "location", cfg.location.c_str());
    return send_cjson(req, root);
}

esp_err_t h_weather_post(httpd_req_t* req)
{
    cJSON* body = body_object(req);
    if (body == nullptr) {
        return ESP_FAIL;
    }
    WeatherConfig cfg = weather_get();
    cfg.location       = obj_str(body, "location", cfg.location);
    cJSON_Delete(body);

    weather_save(cfg);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    return send_cjson(req, root);
}

/* --------------------------------------------------------------------- stock -- */

esp_err_t h_stock_get(httpd_req_t* req)
{
    StockConfig cfg = stock_get();
    cJSON* root      = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "symbol", cfg.symbol.c_str());
    return send_cjson(req, root);
}

esp_err_t h_stock_post(httpd_req_t* req)
{
    cJSON* body = body_object(req);
    if (body == nullptr) {
        return ESP_FAIL;
    }
    StockConfig cfg = stock_get();
    std::string sym  = obj_str(body, "symbol", cfg.symbol);
    cJSON_Delete(body);

    // Upper-case it -- tickers are conventionally upper-case and the price API is
    // case-sensitive-ish in practice, so normalize here rather than trusting the form.
    for (auto& c : sym) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    cfg.symbol = sym;
    stock_save(cfg);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    return send_cjson(req, root);
}

/* ----------------------------------------------------------------- personas -- */

cJSON* persona_to_json(const Persona& p)
{
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", p.id.c_str());
    cJSON_AddStringToObject(o, "name", p.name.c_str());
    cJSON_AddStringToObject(o, "prompt", p.prompt.c_str());
    cJSON_AddStringToObject(o, "voice", p.voice.c_str());
    cJSON_AddNumberToObject(o, "speed", p.speed);
    cJSON_AddNumberToObject(o, "skin", p.skin);
    return o;
}

esp_err_t h_personas_get(httpd_req_t* req)
{
    cJSON* root  = cJSON_CreateObject();
    cJSON* items = cJSON_CreateArray();
    for (const Persona& p : personas_get()) {
        cJSON_AddItemToArray(items, persona_to_json(p));
    }
    cJSON_AddItemToObject(root, "personas", items);
    cJSON_AddStringToObject(root, "active", persona_active_id().c_str());
    return send_cjson(req, root);
}

esp_err_t h_personas_post(httpd_req_t* req)
{
    std::string body;
    if (!read_body(req, body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body missing or too large");
        return ESP_FAIL;
    }
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body is not a JSON object");
        return ESP_FAIL;
    }

    cJSON* items = cJSON_GetObjectItem(root, "personas");
    if (!cJSON_IsArray(items)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected a 'personas' array");
        return ESP_FAIL;
    }

    // NVS strings top out around 4 KB, so the whole list has to stay small. Refusing
    // loudly here beats a silent truncation that loses a persona nobody notices.
    std::vector<Persona> list;
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, items)
    {
        Persona p;
        p.id     = obj_str(item, "id");
        p.name   = obj_str(item, "name");
        p.prompt = obj_str(item, "prompt");
        p.voice  = obj_str(item, "voice");
        p.speed  = static_cast<float>(obj_double(item, "speed", 1.0));
        if (p.speed < 0.25f || p.speed > 4.0f) {
            p.speed = 1.0f;
        }
        p.skin   = obj_int(item, "skin", -1);
        if (p.id.empty()) {
            continue;
        }
        if (p.prompt.size() > 700) {
            p.prompt.resize(700);
        }
        list.push_back(p);
        if (list.size() >= 8) {
            break;
        }
    }
    cJSON_Delete(root);

    personas_save(list);

    // Editing the *active* persona's face used to only take effect the next time it was
    // re-selected -- saving here never touched the live skin, so the Face dropdown looked
    // like it did nothing. Apply it immediately when the edited persona is the active one.
    std::string active_id = persona_active_id();
    bool reboot_required   = false;
    for (const Persona& p : list) {
        if (p.id == active_id && p.skin >= 0) {
            skin_set(p.skin);
            bool applied_live = hal_bridge::display_swap_skin(static_cast<avatar::SkinId>(p.skin));
            reboot_required = !applied_live;
            break;
        }
    }

    cJSON* out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddNumberToObject(out, "saved", static_cast<int>(list.size()));
    cJSON_AddBoolToObject(out, "reboot_required", reboot_required);
    return send_cjson(req, out);
}

esp_err_t h_persona_active(httpd_req_t* req)
{
    cJSON* body = body_object(req);
    if (body == nullptr) {
        return ESP_FAIL;
    }
    std::string id = obj_str(body, "id");
    cJSON_Delete(body);

    // persona_set_active() now applies the persona's face live when it can (SwapSkin(),
    // via the same LVGL-locked path the Face & body page uses) -- only actually returns
    // false when a face change was needed but the AI-agent screen has no avatar to swap
    // yet (boot/setup path, or the launcher menu with AI agent never opened this boot).
    bool applied_live = persona_set_active(id);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "active", id.c_str());
    cJSON_AddNumberToObject(root, "skin", static_cast<int>(avatar::get_current_skin()));
    cJSON_AddBoolToObject(root, "reboot_required", !applied_live);
    return send_cjson(req, root);
}

/* ------------------------------------------------------------------- memory -- */

esp_err_t h_memory_get(httpd_req_t* req)
{
    MemoryConfig cfg = memory_get();
    cJSON* root      = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "notes", cfg.notes.c_str());
    cJSON_AddNumberToObject(root, "history_turns", cfg.history_turns);
    return send_cjson(req, root);
}

esp_err_t h_memory_post(httpd_req_t* req)
{
    cJSON* body = body_object(req);
    if (body == nullptr) {
        return ESP_FAIL;
    }
    MemoryConfig cfg  = memory_get();
    cfg.notes         = obj_str(body, "notes", cfg.notes);
    cfg.history_turns = obj_int(body, "history_turns", cfg.history_turns);
    cJSON_Delete(body);

    // Same reasoning as the persona prompt cap: NVS strings top out around 4 KB, and a
    // loud truncation here beats a silent one nobody notices until the notes go missing.
    if (cfg.notes.size() > 2000) {
        cfg.notes.resize(2000);
    }
    // 0 means "let the bridge use its own BRIDGE_HISTORY_TURNS default"; above that,
    // clamp to a sane range rather than let a typo send an absurd context window.
    if (cfg.history_turns != 0) {
        if (cfg.history_turns < 1) cfg.history_turns = 1;
        if (cfg.history_turns > 50) cfg.history_turns = 50;
    }
    memory_save(cfg);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "saved_length", static_cast<int>(cfg.notes.size()));
    return send_cjson(req, root);
}

/* ---------------------------------------------------------------- wake word -- */

esp_err_t h_wake_get(httpd_req_t* req)
{
    cJSON* root  = cJSON_CreateObject();
    cJSON* items = cJSON_CreateArray();
    for (const WakeWordInfo& w : wake_words_available()) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "model", w.model.c_str());
        cJSON_AddStringToObject(o, "phrase", w.phrase.c_str());
        cJSON_AddBoolToObject(o, "active", w.active);
        cJSON_AddItemToArray(items, o);
    }
    cJSON_AddItemToObject(root, "models", items);
    cJSON_AddStringToObject(root, "selected", wake_word_get().c_str());

    return send_cjson(req, root);
}

esp_err_t h_wake_post(httpd_req_t* req)
{
    cJSON* body = body_object(req);
    if (body == nullptr) {
        return ESP_FAIL;
    }
    std::string model = obj_str(body, "model");
    cJSON_Delete(body);

    if (!wake_word_set(model)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "that model is not in this build");
        return ESP_FAIL;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "reboot_required", true);
    return send_cjson(req, root);
}

/* -------------------------------------------------------------------- skins -- */

esp_err_t h_skins_get(httpd_req_t* req)
{
    cJSON* root  = cJSON_CreateObject();
    cJSON* items = cJSON_CreateArray();
    for (int i = 0; i < static_cast<int>(avatar::SkinId::_Count); i++) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "id", i);
        cJSON_AddStringToObject(o, "name", avatar::skin_name(static_cast<avatar::SkinId>(i)));
        cJSON_AddItemToArray(items, o);
    }
    cJSON_AddItemToObject(root, "skins", items);
    cJSON_AddNumberToObject(root, "active", static_cast<int>(avatar::get_current_skin()));
    return send_cjson(req, root);
}

esp_err_t h_skin_post(httpd_req_t* req)
{
    cJSON* body = body_object(req);
    if (body == nullptr) {
        return ESP_FAIL;
    }
    int id = obj_int(body, "id", -1);
    cJSON_Delete(body);

    if (!skin_set(id)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown skin id");
        return ESP_FAIL;
    }

    // Applied live when possible: SwapSkin() takes the same LVGL lock SetupUI() itself
    // uses, so mutating the avatar from this HTTP task is safe -- the old "rebuild from
    // an HTTP thread isn't worth the tearing risk" reasoning predates that lock existing.
    // Only falls back to reboot_required when the AI-agent screen hasn't built an avatar
    // yet at all (still on the boot/setup path, or the launcher menu with the AI agent
    // never opened this boot).
    bool applied_live = hal_bridge::display_swap_skin(static_cast<avatar::SkinId>(id));

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "reboot_required", !applied_live);
    cJSON_AddStringToObject(root, "name",
                            avatar::skin_name(static_cast<avatar::SkinId>(id)));
    return send_cjson(req, root);
}

/* ------------------------------------------------------------- screensaver -- */

esp_err_t h_screensaver_get(httpd_req_t* req)
{
    ScreensaverConfig cfg = screensaver_get();
    cJSON* root           = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", cfg.enabled);
    cJSON_AddNumberToObject(root, "timeout_ms", cfg.timeout_ms);
    cJSON_AddNumberToObject(root, "style", cfg.style);
    return send_cjson(req, root);
}

esp_err_t h_screensaver_post(httpd_req_t* req)
{
    cJSON* body = body_object(req);
    if (body == nullptr) {
        return ESP_FAIL;
    }
    ScreensaverConfig cfg = screensaver_get();
    cfg.enabled           = obj_bool(body, "enabled", cfg.enabled);
    cfg.timeout_ms        = obj_int(body, "timeout_ms", cfg.timeout_ms);
    cfg.style             = obj_int(body, "style", cfg.style);
    cJSON_Delete(body);

    screensaver_save(cfg);

    // Applied on the launcher's own poll cycle (see AppLauncher::screensaver_update),
    // not here — no reboot needed, and no live app to notify from an HTTP thread.
    return send_ok(req);
}

/* ------------------------------------------------------------------- system -- */

esp_err_t h_reboot(httpd_req_t* req)
{
    mclog::tagInfo(_tag, "reboot requested from the portal");
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "rebooting", true);
    send_cjson(req, root);
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK;
}

const httpd_uri_t API_URIS[] = {
    {"/api/net",         HTTP_GET,  h_net_get,       nullptr},
    {"/api/net",         HTTP_POST, h_net_post,      nullptr},
    {"/api/net/confirm", HTTP_POST, h_net_confirm,   nullptr},
    {"/api/time",        HTTP_GET,  h_time_get,      nullptr},
    {"/api/time",        HTTP_POST, h_time_post,     nullptr},
    {"/api/time/sync",   HTTP_POST, h_time_sync,     nullptr},
    {"/api/ai",          HTTP_GET,  h_ai_get,        nullptr},
    {"/api/ai",          HTTP_POST, h_ai_post,       nullptr},
    {"/api/kie",         HTTP_GET,  h_kie_get,       nullptr},
    {"/api/kie",         HTTP_POST, h_kie_post,      nullptr},
    {"/api/weather",     HTTP_GET,  h_weather_get,   nullptr},
    {"/api/weather",     HTTP_POST, h_weather_post,  nullptr},
    {"/api/stock",       HTTP_GET,  h_stock_get,     nullptr},
    {"/api/stock",       HTTP_POST, h_stock_post,    nullptr},
    {"/api/personas",    HTTP_GET,  h_personas_get,  nullptr},
    {"/api/personas",    HTTP_POST, h_personas_post, nullptr},
    {"/api/persona",     HTTP_POST, h_persona_active, nullptr},
    {"/api/memory",      HTTP_GET,  h_memory_get,    nullptr},
    {"/api/memory",      HTTP_POST, h_memory_post,   nullptr},
    {"/api/wake",        HTTP_GET,  h_wake_get,      nullptr},
    {"/api/wake",        HTTP_POST, h_wake_post,     nullptr},
    {"/api/skins",       HTTP_GET,  h_skins_get,     nullptr},
    {"/api/skin",        HTTP_POST, h_skin_post,     nullptr},
    {"/api/screensaver", HTTP_GET,  h_screensaver_get,  nullptr},
    {"/api/screensaver", HTTP_POST, h_screensaver_post, nullptr},
    {"/api/reboot",      HTTP_POST, h_reboot,        nullptr},
};

}  // namespace

int api_handler_count()
{
    return sizeof(API_URIS) / sizeof(API_URIS[0]);
}

void register_api_handlers(httpd_handle_t server)
{
    for (const httpd_uri_t& u : API_URIS) {
        esp_err_t err = httpd_register_uri_handler(server, &u);
        if (err != ESP_OK) {
            mclog::tagError(_tag, "failed to register {}: {}", u.uri, esp_err_to_name(err));
        }
    }
}

}  // namespace stackchan::portal
