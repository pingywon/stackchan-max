/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 *
 * StackyChan web portal — rebuilt around chunked uploads.
 *
 * Why the rewrite
 * ---------------
 * The first version pushed the whole 3.85MB image in a single POST. That failed in the
 * field, silently, with nothing useful to report. The PC and the device sat on
 * different subnets, so every byte crossed a router, and a
 * single multi-megabyte request held open across a router is the classic shape of transfer
 * that dies without an error: NAT state expiry, MTU/fragmentation, proxy buffering, or the
 * ESP32's small TCP receive window stalling the sender until something upstream gives up.
 * The socket simply stops and nobody reports a failure.
 *
 * Uploads are therefore now many small independent requests. Each chunk is tens of KB,
 * completes in milliseconds, and can be retried on its own. Nothing is long-lived enough
 * for the network to break, and every failure has a specific place to report itself.
 *
 * Three upload paths, deliberately:
 *
 *   /api/ota/begin + /api/ota/chunk (xN) + /api/ota/end   primary, used by the page
 *   /api/ota                                              single-shot, easy from curl
 *   /api/ota_url                                          the device pulls it itself
 *
 * Plus /api/echo, which consumes a body and reports the byte count without touching flash.
 * That separates "the network cannot move this much data" from "the OTA code is wrong" —
 * a distinction that previously had to be guessed at.
 */
#include "portal.h"
#include "portal_page.h"
#include "portal_api.h"
#include "portal_config.h"

#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <esp_https_ota.h>
#include <esp_http_client.h>
#include <esp_app_desc.h>
#include <cJSON.h>
#include <esp_netif.h>
#include <mdns.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mooncake_log.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <stackchan/stackchan.h>
#include <stackchan/avatar/skins/skins.h>
#include <hal/hal.h>

static const char* _tag = "portal";

namespace stackchan::portal {

static httpd_handle_t _server = nullptr;

bool is_running()
{
    return _server != nullptr;
}

/* ------------------------------------------------------------------ helpers -- */

static std::string current_ip()
{
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) {
        return "0.0.0.0";
    }
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK) {
        return "0.0.0.0";
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip.ip));
    return buf;
}

static bool has_ip()
{
    return current_ip() != "0.0.0.0";
}

/** Read a small request body whole. Bounded so a bad client cannot exhaust the heap. */
static bool read_body(httpd_req_t* req, std::string& out, size_t limit = 8192)
{
    int remaining = req->content_len;
    if (remaining < 0 || static_cast<size_t>(remaining) > limit) {
        return false;
    }
    out.clear();
    out.reserve(remaining);

    char chunk[512];
    while (remaining > 0) {
        int want = remaining > (int)sizeof(chunk) ? (int)sizeof(chunk) : remaining;
        int got  = httpd_req_recv(req, chunk, want);
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

static esp_err_t send_json(httpd_req_t* req, const char* json)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

/** Pull an integer field from a tiny JSON body without adding a parser dependency. */
static long json_int(const std::string& body, const char* key, long fallback = 0)
{
    std::string needle = std::string("\"") + key + "\"";
    const char* k      = std::strstr(body.c_str(), needle.c_str());
    if (!k) {
        return fallback;
    }
    const char* colon = std::strchr(k, ':');
    return colon ? std::atol(colon + 1) : fallback;
}

/** Same, for a string field. */
static std::string json_str(const std::string& body, const char* key)
{
    std::string needle = std::string("\"") + key + "\"";
    const char* k      = std::strstr(body.c_str(), needle.c_str());
    if (!k) {
        return "";
    }
    const char* colon = std::strchr(k, ':');
    if (!colon) {
        return "";
    }
    const char* q1 = std::strchr(colon, '"');
    if (!q1) {
        return "";
    }
    const char* q2 = std::strchr(q1 + 1, '"');
    if (!q2) {
        return "";
    }
    return std::string(q1 + 1, q2 - q1 - 1);
}

/* ------------------------------------------------------------ basic handlers -- */

static esp_err_t h_root(httpd_req_t* req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PORTAL_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

/**
 * GET /api/info
 *
 * Built with cJSON rather than snprintf, and that is not a style choice.
 *
 * This handler used to format the uptime with `%llu`. The build enables
 * CONFIG_LIBC_NEWLIB_NANO_FORMAT, whose printf — in ESP-IDF's own words — "doesn't
 * support 64-bit integer formats". The `ll` width was not honoured, so every argument
 * after the uptime was read from the wrong offset and the final `%s` picked up the high
 * half of the 64-bit uptime. That half is zero for any uptime under about 49 days, so
 * printf called strlen(NULL) and the whole HTTP task died: LoadProhibited, EXCVADDR
 * 0x00000000, and a reboot within seconds of any browser having the portal open.
 *
 * A serialiser that takes typed values cannot make that mistake, so this endpoint no
 * longer uses varargs at all.
 */
static esp_err_t h_info(httpd_req_t* req)
{
    const esp_app_desc_t* desc     = esp_app_get_description();
    const esp_partition_t* running = esp_ota_get_running_partition();

    std::string compiled = "?";
    if (desc != nullptr) {
        compiled = std::string(desc->date) + " " + desc->time;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", desc ? desc->version : "?");
    cJSON_AddStringToObject(root, "compiled", compiled.c_str());
    cJSON_AddStringToObject(root, "partition", running ? running->label : "?");
    cJSON_AddStringToObject(root, "ip", current_ip().c_str());
    // cJSON numbers are doubles, which represent this exactly until well past the
    // point the device would have rebooted for other reasons.
    cJSON_AddNumberToObject(root, "uptime_ms",
                            static_cast<double>(esp_timer_get_time() / 1000));
    cJSON_AddNumberToObject(root, "free_heap", static_cast<double>(esp_get_free_heap_size()));
    cJSON_AddStringToObject(root, "skin", avatar::skin_name(avatar::get_current_skin()));

    char* text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, text);
    cJSON_free(text);
    return err;
}

static esp_err_t h_avatar(httpd_req_t* req)
{
    std::string body;
    if (!read_body(req, body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }
    GetStackChan().updateAvatarFromJson(body.c_str());
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t h_motion(httpd_req_t* req)
{
    std::string body;
    if (!read_body(req, body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }
    GetStackChan().updateMotionFromJson(body.c_str());
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t h_rgb(httpd_req_t* req)
{
    std::string body;
    if (!read_body(req, body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }
    GetStackChan().updateNeonLightFromJson(body.c_str());
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t h_emotion(httpd_req_t* req)
{
    std::string body;
    if (!read_body(req, body, 256)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }
    long value = json_int(body, "emotion", -1);
    if (value < 0 || value > 5) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "emotion out of range 0..5");
        return ESP_FAIL;
    }
    if (GetStackChan().hasAvatar()) {
        GetStackChan().avatar().setEmotion(static_cast<avatar::Emotion>(value));
    }
    return send_json(req, "{\"ok\":true}");
}

/* ---------------------------------------------------------------- diagnostic -- */

/** POST /api/echo — consume a body and report the count. Touches no flash. */
static esp_err_t h_echo(httpd_req_t* req)
{
    size_t total  = 0;
    int remaining = req->content_len;
    int64_t t0    = esp_timer_get_time();
    char buf[2048];

    while (remaining > 0) {
        int want = remaining > (int)sizeof(buf) ? (int)sizeof(buf) : remaining;
        int got  = httpd_req_recv(req, buf, want);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (got <= 0) {
            mclog::tagError(_tag, "echo aborted after {} of {} bytes (recv={})", total,
                            (int)req->content_len, got);
            char detail[176];
            std::snprintf(detail, sizeof(detail),
                          "{\"ok\":false,\"received\":%u,\"expected\":%d,\"recv\":%d}",
                          (unsigned)total, (int)req->content_len, got);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, detail);
            return ESP_OK;
        }
        total += got;
        remaining -= got;
    }

    int ms = (int)((esp_timer_get_time() - t0) / 1000);
    mclog::tagInfo(_tag, "echo received {} bytes in {} ms", total, ms);
    char out[176];
    std::snprintf(out, sizeof(out), "{\"ok\":true,\"received\":%u,\"ms\":%d,\"kbps\":%d}",
                  (unsigned)total, ms, ms > 0 ? (int)(total / (size_t)ms) : 0);
    return send_json(req, out);
}

/* ----------------------------------------------------------- chunked upload -- */

namespace {

struct OtaSession {
    esp_ota_handle_t handle    = 0;
    const esp_partition_t* dst = nullptr;
    size_t expected            = 0;
    size_t written             = 0;
    bool active                = false;
    bool magic_checked         = false;
    int64_t started_us         = 0;
};

OtaSession g_ota;

void ota_session_reset()
{
    if (g_ota.active && g_ota.handle != 0) {
        esp_ota_abort(g_ota.handle);
    }
    g_ota = OtaSession{};
}

}  // namespace

/** POST /api/ota/begin — body {"size":N}. Erases the inactive slot. */
static esp_err_t h_ota_begin(httpd_req_t* req)
{
    std::string body;
    if (!read_body(req, body, 256)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }

    long size                  = json_int(body, "size", 0);
    const esp_partition_t* dst = esp_ota_get_next_update_partition(nullptr);

    if (dst == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA slot");
        return ESP_FAIL;
    }
    if (size <= 0 || (size_t)size > dst->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "size out of range for slot");
        return ESP_FAIL;
    }

    ota_session_reset();

    mclog::tagInfo(_tag, "OTA begin: {} bytes -> '{}' (erasing)", size, dst->label);
    int64_t t0    = esp_timer_get_time();
    esp_err_t err = esp_ota_begin(dst, size, &g_ota.handle);
    int erase_ms  = (int)((esp_timer_get_time() - t0) / 1000);

    if (err != ESP_OK) {
        mclog::tagError(_tag, "esp_ota_begin failed: {}", esp_err_to_name(err));
        char detail[96];
        std::snprintf(detail, sizeof(detail), "ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, detail);
        return ESP_FAIL;
    }

    g_ota.dst        = dst;
    g_ota.expected   = (size_t)size;
    g_ota.written    = 0;
    g_ota.active     = true;
    g_ota.started_us = esp_timer_get_time();

    mclog::tagInfo(_tag, "slot erased in {} ms, ready for chunks", erase_ms);

    char out[128];
    std::snprintf(out, sizeof(out), "{\"ok\":true,\"slot\":\"%s\",\"erase_ms\":%d}", dst->label,
                  erase_ms);
    return send_json(req, out);
}

/** POST /api/ota/chunk — raw bytes appended in order. Retryable. */
static esp_err_t h_ota_chunk(httpd_req_t* req)
{
    if (!g_ota.active) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no upload in progress; call begin first");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    if (remaining <= 0 || g_ota.written + (size_t)remaining > g_ota.expected) {
        ota_session_reset();
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "chunk would exceed declared size");
        return ESP_FAIL;
    }

    char buf[4096];
    while (remaining > 0) {
        int want = remaining > (int)sizeof(buf) ? (int)sizeof(buf) : remaining;
        int got  = httpd_req_recv(req, buf, want);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (got <= 0) {
            // Session stays open on purpose: the client just resends this chunk.
            mclog::tagWarn(_tag, "chunk recv failed ({}) at offset {}", got, g_ota.written);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "chunk truncated, retry");
            return ESP_FAIL;
        }

        if (!g_ota.magic_checked) {
            if ((uint8_t)buf[0] != 0xE9) {
                ota_session_reset();
                mclog::tagError(_tag, "rejected: first byte is not 0xE9");
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                    "not an ESP firmware image (missing 0xE9 magic)");
                return ESP_FAIL;
            }
            g_ota.magic_checked = true;
        }

        esp_err_t err = esp_ota_write(g_ota.handle, buf, got);
        if (err != ESP_OK) {
            mclog::tagError(_tag, "esp_ota_write failed at {}: {}", g_ota.written,
                            esp_err_to_name(err));
            ota_session_reset();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "flash write failed");
            return ESP_FAIL;
        }

        g_ota.written += got;
        remaining -= got;
        taskYIELD();
    }

    char out[96];
    std::snprintf(out, sizeof(out), "{\"ok\":true,\"written\":%u,\"expected\":%u}",
                  (unsigned)g_ota.written, (unsigned)g_ota.expected);
    return send_json(req, out);
}

/** POST /api/ota/end — validate, switch boot slot, reboot. */
static esp_err_t h_ota_end(httpd_req_t* req)
{
    if (!g_ota.active) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no upload in progress");
        return ESP_FAIL;
    }
    if (g_ota.written != g_ota.expected) {
        char detail[128];
        std::snprintf(detail, sizeof(detail), "incomplete: %u of %u bytes",
                      (unsigned)g_ota.written, (unsigned)g_ota.expected);
        ota_session_reset();
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, detail);
        return ESP_FAIL;
    }

    const esp_partition_t* dst = g_ota.dst;
    size_t written             = g_ota.written;
    int secs                   = (int)((esp_timer_get_time() - g_ota.started_us) / 1000000);

    esp_err_t err = esp_ota_end(g_ota.handle);
    g_ota.handle  = 0;
    if (err != ESP_OK) {
        mclog::tagError(_tag, "esp_ota_end failed: {}", esp_err_to_name(err));
        g_ota = OtaSession{};
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "image failed validation");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(dst);
    if (err != ESP_OK) {
        mclog::tagError(_tag, "set_boot_partition failed: {}", esp_err_to_name(err));
        g_ota = OtaSession{};
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not set boot slot");
        return ESP_FAIL;
    }

    g_ota = OtaSession{};
    mclog::tagInfo(_tag, "OTA complete ({} bytes in {}s), rebooting into '{}'", written, secs,
                   dst->label);

    send_json(req, "{\"ok\":true,\"rebooting\":true}");
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
    return ESP_OK;
}

/** POST /api/ota/abort — drop a half-finished session. */
static esp_err_t h_ota_abort(httpd_req_t* req)
{
    ota_session_reset();
    mclog::tagInfo(_tag, "OTA session aborted by client");
    return send_json(req, "{\"ok\":true}");
}

/* ------------------------------------------------------- music (SD card) -- */
//
// Same chunked-session shape as the OTA upload above, writing to a file on the SD card
// via stdio instead of esp_ota_write() to a flash partition. No 0xE9 magic-byte check
// (irrelevant for MP3) — extension and a size cap take its place. Every handler bails
// immediately if Hal::sdCardAvailable() is false: this feature is either fully working
// or fully absent, no degraded/fallback mode.

namespace {

constexpr const char* kMusicDir      = "/sdcard/stackchan/music";
constexpr size_t kMusicMaxBytes      = 30 * 1024 * 1024;  // generous for a single MP3

struct MusicUploadSession {
    FILE* fp        = nullptr;
    std::string path;
    size_t expected = 0;
    size_t written  = 0;
    bool active     = false;
};

MusicUploadSession g_music;

void music_session_reset()
{
    if (g_music.fp != nullptr) {
        std::fclose(g_music.fp);
        // A partial file left behind after an aborted/failed upload would look like a
        // real (but corrupt) song in the list — remove it rather than leave that trap.
        std::remove(g_music.path.c_str());
    }
    g_music = MusicUploadSession{};
}

/** Letters, digits, spaces, `-_.` only, must end in .mp3 — enough to build a safe path
 * under kMusicDir without pulling in a real path-sanitising library for one use. */
bool music_filename_is_safe(const std::string& name)
{
    if (name.size() < 5 || name.size() > 128) {
        return false;
    }
    if (name.find(".mp3") != name.size() - 4) {
        return false;
    }
    for (char c : name) {
        bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' ||
                  c == '_' || c == '.';
        if (!ok) {
            return false;
        }
    }
    return true;
}

}  // namespace

/** POST /api/music/begin — body {"filename":"song.mp3","size":N}. */
static esp_err_t h_music_begin(httpd_req_t* req)
{
    if (!GetHAL().sdCardAvailable()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no formatted SD card is present");
        return ESP_FAIL;
    }
    std::string body;
    if (!read_body(req, body, 256)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }

    std::string filename = json_str(body, "filename");
    long size             = json_int(body, "size", 0);

    if (!music_filename_is_safe(filename)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "filename must end in .mp3 and use only letters/digits/space/-_.");
        return ESP_FAIL;
    }
    if (size <= 0 || (size_t)size > kMusicMaxBytes) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "size out of range (max 30 MB)");
        return ESP_FAIL;
    }

    music_session_reset();

    mkdir(kMusicDir, 0755);  // ignore EEXIST; only failure that matters shows up at fopen

    g_music.path = std::string(kMusicDir) + "/" + filename;
    g_music.fp   = std::fopen(g_music.path.c_str(), "wb");
    if (g_music.fp == nullptr) {
        mclog::tagError(_tag, "could not open '{}' for writing", g_music.path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not open file on SD card");
        return ESP_FAIL;
    }

    g_music.expected = (size_t)size;
    g_music.written  = 0;
    g_music.active   = true;

    mclog::tagInfo(_tag, "music upload begin: '{}', {} bytes", filename, size);
    return send_json(req, "{\"ok\":true}");
}

/** POST /api/music/chunk — raw bytes appended in order. Retryable. */
static esp_err_t h_music_chunk(httpd_req_t* req)
{
    if (!g_music.active) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no upload in progress; call begin first");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    if (remaining <= 0 || g_music.written + (size_t)remaining > g_music.expected) {
        music_session_reset();
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "chunk would exceed declared size");
        return ESP_FAIL;
    }

    // The client always retries a failed chunk by resending the SAME full blob from its
    // own byte 0 (portal_page.h's musicSendChunk), not a resumed remainder — so on any
    // retryable failure partway through this chunk, the file position and g_music.written
    // both have to roll back to where THIS chunk started, not stay at wherever the partial
    // write left off. Without this, a retry appends its full bytes past the partial write
    // already on disk instead of overwriting it, silently shifting/duplicating everything
    // from that point on (MP3 decodes as garbage, or a later chunk's size check catches
    // the drift and wipes the whole upload).
    const size_t chunk_start = g_music.written;
    char buf[4096];
    while (remaining > 0) {
        int want = remaining > (int)sizeof(buf) ? (int)sizeof(buf) : remaining;
        int got  = httpd_req_recv(req, buf, want);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (got <= 0) {
            mclog::tagWarn(_tag, "music chunk recv failed ({}) at offset {}, rolling back to {}",
                           got, g_music.written, chunk_start);
            std::fflush(g_music.fp);
            if (ftruncate(fileno(g_music.fp), chunk_start) == 0) {
                std::fseek(g_music.fp, chunk_start, SEEK_SET);
                g_music.written = chunk_start;
            } else {
                // Couldn't roll back the file itself -- state would disagree with what's
                // actually on disk either way, so abort the whole upload rather than let a
                // retry silently corrupt it further.
                mclog::tagError(_tag, "rollback truncate failed, aborting upload");
                music_session_reset();
            }
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "chunk truncated, retry");
            return ESP_FAIL;
        }

        if (std::fwrite(buf, 1, got, g_music.fp) != (size_t)got) {
            mclog::tagError(_tag, "SD write failed at offset {}", g_music.written);
            music_session_reset();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card write failed");
            return ESP_FAIL;
        }

        g_music.written += got;
        remaining -= got;
        taskYIELD();
    }

    char out[96];
    std::snprintf(out, sizeof(out), "{\"ok\":true,\"written\":%u,\"expected\":%u}",
                  (unsigned)g_music.written, (unsigned)g_music.expected);
    return send_json(req, out);
}

/** POST /api/music/end — close and validate the file. */
static esp_err_t h_music_end(httpd_req_t* req)
{
    if (!g_music.active) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no upload in progress");
        return ESP_FAIL;
    }
    if (g_music.written != g_music.expected) {
        music_session_reset();
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "short upload — sizes do not match");
        return ESP_FAIL;
    }

    std::fclose(g_music.fp);
    g_music.fp = nullptr;
    mclog::tagInfo(_tag, "music upload complete: '{}' ({} bytes)", g_music.path, g_music.written);
    g_music = MusicUploadSession{};
    return send_json(req, "{\"ok\":true}");
}

/** POST /api/music/abort — drop a half-finished upload and remove the partial file. */
static esp_err_t h_music_abort(httpd_req_t* req)
{
    music_session_reset();
    mclog::tagInfo(_tag, "music upload aborted by client");
    return send_json(req, "{\"ok\":true}");
}

/** GET /api/music/list — every .mp3 on the card, or a clear "no card" state. */
static esp_err_t h_music_list(httpd_req_t* req)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "sd_card_available", GetHAL().sdCardAvailable());
    cJSON* items = cJSON_CreateArray();

    if (GetHAL().sdCardAvailable()) {
        DIR* dir = opendir(kMusicDir);
        if (dir != nullptr) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name = entry->d_name;
                if (!music_filename_is_safe(name)) {
                    continue;
                }
                std::string full = std::string(kMusicDir) + "/" + name;
                struct stat st;
                cJSON* o = cJSON_CreateObject();
                cJSON_AddStringToObject(o, "filename", name.c_str());
                cJSON_AddNumberToObject(o, "bytes", stat(full.c_str(), &st) == 0
                                                        ? static_cast<double>(st.st_size) : 0.0);
                cJSON_AddItemToArray(items, o);
            }
            closedir(dir);
        }
    }

    cJSON_AddItemToObject(root, "songs", items);

    char* text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, text);
    cJSON_free(text);
    return err;
}

/** POST /api/music/delete — body {"filename":"song.mp3"}. */
static esp_err_t h_music_delete(httpd_req_t* req)
{
    if (!GetHAL().sdCardAvailable()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no formatted SD card is present");
        return ESP_FAIL;
    }
    std::string body;
    if (!read_body(req, body, 256)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }
    std::string filename = json_str(body, "filename");
    if (!music_filename_is_safe(filename)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad filename");
        return ESP_FAIL;
    }
    std::string path = std::string(kMusicDir) + "/" + filename;
    if (std::remove(path.c_str()) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "file not found");
        return ESP_FAIL;
    }
    mclog::tagInfo(_tag, "deleted '{}'", path);
    return send_json(req, "{\"ok\":true}");
}

/* ------------------------------------------------------------- single-shot -- */

/** POST /api/ota — whole image in one request. Convenient from curl. */
static esp_err_t h_ota(httpd_req_t* req)
{
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (target == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA slot available");
        return ESP_FAIL;
    }

    mclog::tagInfo(_tag, "single-shot OTA: {} bytes -> '{}'", (int)req->content_len,
                   target->label);

    if (req->content_len == 0 || req->content_len > target->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "image size invalid for slot");
        return ESP_FAIL;
    }

    int64_t t_erase         = esp_timer_get_time();
    esp_ota_handle_t handle = 0;
    esp_err_t err           = esp_ota_begin(target, req->content_len, &handle);
    mclog::tagInfo(_tag, "erase done in {} ms", (int)((esp_timer_get_time() - t_erase) / 1000));
    if (err != ESP_OK) {
        mclog::tagError(_tag, "esp_ota_begin failed: {}", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin failed");
        return ESP_FAIL;
    }

    std::vector<char> buf(4096);
    int remaining   = req->content_len;
    bool magic_ok   = false;
    int last_logged = -1;

    while (remaining > 0) {
        int want = remaining > (int)buf.size() ? (int)buf.size() : remaining;
        int got  = httpd_req_recv(req, buf.data(), want);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (got <= 0) {
            esp_ota_abort(handle);
            mclog::tagError(_tag, "recv failed ({}) with {} of {} bytes left", got, remaining,
                            (int)req->content_len);
            char detail[144];
            std::snprintf(detail, sizeof(detail), "upload interrupted: recv=%d, %d of %d left",
                          got, remaining, (int)req->content_len);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, detail);
            return ESP_FAIL;
        }

        if (!magic_ok) {
            if ((uint8_t)buf[0] != 0xE9) {
                esp_ota_abort(handle);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                    "not an ESP firmware image (missing 0xE9 magic)");
                return ESP_FAIL;
            }
            magic_ok = true;
        }

        err = esp_ota_write(handle, buf.data(), got);
        if (err != ESP_OK) {
            esp_ota_abort(handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "flash write failed");
            return ESP_FAIL;
        }

        remaining -= got;
        taskYIELD();

        int pct = 100 - (remaining * 100 / (int)req->content_len);
        if (pct / 10 != last_logged / 10) {
            last_logged = pct;
            mclog::tagInfo(_tag, "OTA {}%", pct);
        }
    }

    if (esp_ota_end(handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "image failed validation");
        return ESP_FAIL;
    }
    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not set boot slot");
        return ESP_FAIL;
    }

    mclog::tagInfo(_tag, "OTA complete, rebooting into '{}'", target->label);
    send_json(req, "{\"ok\":true,\"rebooting\":true}");
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
    return ESP_OK;
}

/* ------------------------------------------------------------------ pull OTA -- */

/**
 * POST /api/ota_url — body {"url":"http://host:port/stack-chan.bin"}.
 *
 * The device fetches the image itself, so browser behaviour and upload buffering stop
 * mattering. The URL must be reachable from the DEVICE's network, which is not
 * necessarily the same network the browser is on.
 */
static esp_err_t h_ota_url(httpd_req_t* req)
{
    std::string body;
    if (!read_body(req, body, 1024)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }

    std::string url = json_str(body, "url");
    if (url.empty()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no url in body");
        return ESP_FAIL;
    }

    mclog::tagInfo(_tag, "pull OTA from {}", url);
    send_json(req, "{\"ok\":true,\"started\":true}");

    esp_http_client_config_t http_cfg    = {};
    http_cfg.url                         = url.c_str();
    http_cfg.timeout_ms                  = 20000;
    http_cfg.keep_alive_enable           = true;
    http_cfg.skip_cert_common_name_check = true;
    http_cfg.crt_bundle_attach           = nullptr;

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config            = &http_cfg;

    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        mclog::tagInfo(_tag, "pull OTA complete, rebooting");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else {
        mclog::tagError(_tag, "pull OTA failed: {}", esp_err_to_name(err));
    }
    return ESP_OK;
}

/* -------------------------------------------------------------------- server -- */

static void register_handlers()
{
    static const httpd_uri_t uris[] = {
        {"/",              HTTP_GET,  h_root,      nullptr},
        {"/api/info",      HTTP_GET,  h_info,      nullptr},
        {"/api/avatar",    HTTP_POST, h_avatar,    nullptr},
        {"/api/motion",    HTTP_POST, h_motion,    nullptr},
        {"/api/rgb",       HTTP_POST, h_rgb,       nullptr},
        {"/api/emotion",   HTTP_POST, h_emotion,   nullptr},
        {"/api/echo",      HTTP_POST, h_echo,      nullptr},
        {"/api/ota",       HTTP_POST, h_ota,       nullptr},
        {"/api/ota_url",   HTTP_POST, h_ota_url,   nullptr},
        {"/api/ota/begin", HTTP_POST, h_ota_begin, nullptr},
        {"/api/ota/chunk", HTTP_POST, h_ota_chunk, nullptr},
        {"/api/ota/end",   HTTP_POST, h_ota_end,   nullptr},
        {"/api/ota/abort", HTTP_POST, h_ota_abort, nullptr},
        {"/api/music/begin",  HTTP_POST, h_music_begin,  nullptr},
        {"/api/music/chunk",  HTTP_POST, h_music_chunk,  nullptr},
        {"/api/music/end",    HTTP_POST, h_music_end,    nullptr},
        {"/api/music/abort",  HTTP_POST, h_music_abort,  nullptr},
        {"/api/music/list",   HTTP_GET,  h_music_list,   nullptr},
        {"/api/music/delete", HTTP_POST, h_music_delete, nullptr},
    };
    for (const auto& u : uris) {
        esp_err_t err = httpd_register_uri_handler(_server, &u);
        if (err != ESP_OK) {
            mclog::tagError(_tag, "failed to register {}: {}", u.uri, esp_err_to_name(err));
        }
    }

    // Settings endpoints live in portal_api.cpp — see the note there on why they are
    // kept out of this file.
    register_api_handlers(_server);
}

static void portal_task(void*)
{
    // The device can sit on the setup screen for a long time before it has an address.
    while (!has_ip()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Runs once, right when the IP first becomes known, on every boot path - the
    // launcher-menu boot and the skip-straight-to-AI-agent boot both reach here.
    {
        LvglLockGuard lock;
        GetStackChan().addModifier(
            std::make_unique<stackchan::TimedSpeechModifier>("IP: " + current_ip(), 5000));
    }

    // Static addressing is applied here rather than at connect time: the interface has
    // to exist and be up first, and a DHCP lease is a harmless thing to replace.
    config::network_apply();
    config::time_apply();

    httpd_config_t cfg    = HTTPD_DEFAULT_CONFIG();
    cfg.server_port       = 80;
    cfg.stack_size        = 12288;
    // Base handlers (19 as of the music-upload endpoints, see register_handlers()'s
    // local `uris[]`) plus every settings endpoint, with room to add more without this
    // silently starting to drop registrations.
    cfg.max_uri_handlers  = 24 + api_handler_count();
    cfg.max_open_sockets  = 4;
    cfg.lru_purge_enable  = true;
    cfg.recv_wait_timeout = 60;
    cfg.send_wait_timeout = 60;

    esp_err_t err = httpd_start(&_server, &cfg);
    if (err != ESP_OK) {
        mclog::tagError(_tag, "httpd_start failed: {}", esp_err_to_name(err));
        _server = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    register_handlers();

    // Stable name, so a DHCP change never sends anyone hunting for the address again.
    esp_err_t merr = mdns_init();
    if (merr == ESP_OK) {
        std::string host = config::network_get().hostname;
        if (host.empty()) {
            host = "stackchan";
        }
        mdns_hostname_set(host.c_str());
        mdns_instance_name_set("StackyChan");
        mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
        mclog::tagInfo(_tag, "mDNS up: http://{}.local/", host);
    } else {
        mclog::tagWarn(_tag, "mDNS init failed ({}), use the IP directly", esp_err_to_name(merr));
    }

    mclog::tagInfo(_tag, "portal up at http://{}/  (or http://stackchan.local/)", current_ip());

    vTaskDelete(nullptr);
}

void start()
{
    if (_server != nullptr) {
        return;
    }

    // Both of these run before the network comes up, on purpose. The recovery check has
    // to happen before any static address is applied, and the stored skin has to be in
    // place before the first app builds an avatar.
    config::network_recover_at_boot();
    config::skin_restore();
    config::wake_word_init_default();
    config::custom_backend_migrate_at_boot();

    xTaskCreate(portal_task, "portal", 4096, nullptr, 4, nullptr);
}

}  // namespace stackchan::portal
