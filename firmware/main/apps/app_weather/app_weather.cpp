/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_weather.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <apps/common/common.h>
#include <portal/portal_config.h>
#include <board.h>
#include <cJSON.h>
#include <fmt/format.h>

#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <utility>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;

namespace {
constexpr uint32_t kThemeColor         = 0xFFB74D;
constexpr uint32_t kBg                 = 0x201408;
constexpr uint32_t kFg                 = 0xFFE8CC;
constexpr uint32_t kFgDim              = 0xD9A968;
constexpr uint32_t kErr                = 0xFF6B6B;
constexpr uint32_t kRefreshIntervalMs  = 10 * 60 * 1000;  // 10 minutes -- a place doesn't change that fast

const char* _tag = "AppWeather";

std::string url_encode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t");
    size_t b = s.find_last_not_of(" \t");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

const char* condition_text(int code)
{
    switch (code) {
        case 0: return "clear";
        case 1: return "mostly clear";
        case 2: return "partly cloudy";
        case 3: return "overcast";
        case 45: case 48: return "foggy";
        case 51: return "light drizzle";
        case 53: return "drizzle";
        case 55: return "heavy drizzle";
        case 61: return "light rain";
        case 63: return "rain";
        case 65: return "heavy rain";
        case 71: return "light snow";
        case 73: return "snow";
        case 75: return "heavy snow";
        case 80: case 81: return "rain showers";
        case 82: return "heavy rain showers";
        case 95: case 96: case 99: return "thunderstorms";
        default: return "unclear skies";
    }
}

// Fetches a URL and parses the body as JSON. Caller owns and must cJSON_Delete() *out.
bool fetch_json(NetworkInterface* network, const std::string& url, cJSON** out)
{
    *out = nullptr;
    auto http = network->CreateHttp(0);
    http->SetTimeout(8000);
    if (!http->Open("GET", url)) {
        mclog::tagError(_tag, "failed to open http connection to {}", url);
        return false;
    }
    if (http->GetStatusCode() != 200) {
        mclog::tagError(_tag, "http {} -> status {}", url, http->GetStatusCode());
        http->Close();
        return false;
    }
    std::string body = http->ReadAll();
    http->Close();

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        mclog::tagError(_tag, "failed to parse json from {}", url);
        return false;
    }
    *out = root;
    return true;
}

// "lat,lon" if the location parses that way (whole-string match, both halves numeric);
// otherwise geocodes it as a place name via Open-Meteo's free geocoding API, same rule
// server/bridge/weather.py uses. Results are cached in-process -- a place doesn't move.
bool resolve_location(NetworkInterface* network, const std::string& loc, double& lat, double& lon)
{
    static std::map<std::string, std::pair<double, double>> cache;
    auto cached = cache.find(loc);
    if (cached != cache.end()) {
        lat = cached->second.first;
        lon = cached->second.second;
        return true;
    }

    size_t comma = loc.find(',');
    if (comma != std::string::npos) {
        std::string a = trim(loc.substr(0, comma));
        std::string b = trim(loc.substr(comma + 1));
        if (!a.empty() && !b.empty()) {
            char* end_a = nullptr;
            char* end_b = nullptr;
            double la = std::strtod(a.c_str(), &end_a);
            double lo = std::strtod(b.c_str(), &end_b);
            if (end_a != nullptr && *end_a == '\0' && end_b != nullptr && *end_b == '\0') {
                lat = la;
                lon = lo;
                cache[loc] = {lat, lon};
                return true;
            }
        }
    }

    std::string url = "https://geocoding-api.open-meteo.com/v1/search?name=" + url_encode(loc) + "&count=1";
    cJSON* root      = nullptr;
    if (!fetch_json(network, url, &root)) {
        return false;
    }

    bool ok               = false;
    cJSON* results = cJSON_GetObjectItemCaseSensitive(root, "results");
    if (cJSON_IsArray(results) && cJSON_GetArraySize(results) > 0) {
        cJSON* first = cJSON_GetArrayItem(results, 0);
        cJSON* jlat  = cJSON_GetObjectItemCaseSensitive(first, "latitude");
        cJSON* jlon  = cJSON_GetObjectItemCaseSensitive(first, "longitude");
        if (cJSON_IsNumber(jlat) && cJSON_IsNumber(jlon)) {
            lat        = jlat->valuedouble;
            lon        = jlon->valuedouble;
            cache[loc] = {lat, lon};
            ok         = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}

}  // namespace

AppWeather::AppWeather()
{
    setAppInfo().name = "WEATHER";
    static uint32_t theme_color = kThemeColor;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppWeather::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppWeather::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    {
        LvglLockGuard lock;

        _panel = std::make_unique<Container>(lv_screen_active());
        _panel->setBgColor(lv_color_hex(kBg));
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setBorderWidth(0);
        _panel->setSize(320, 240);
        _panel->setRadius(0);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _location_label = std::make_unique<Label>(*_panel);
        _location_label->setTextFont(&lv_font_montserrat_16);
        _location_label->setTextColor(lv_color_hex(kFgDim));
        _location_label->align(LV_ALIGN_TOP_MID, 0, 32);

        _temp_label = std::make_unique<Label>(*_panel);
        _temp_label->setTextFont(&lv_font_montserrat_48);
        _temp_label->setTextColor(lv_color_hex(kFg));
        _temp_label->align(LV_ALIGN_CENTER, 0, -8);
        _temp_label->setText("--");

        _condition_label = std::make_unique<Label>(*_panel);
        _condition_label->setTextFont(&lv_font_montserrat_20);
        _condition_label->setTextColor(lv_color_hex(kFg));
        _condition_label->align(LV_ALIGN_CENTER, 0, 44);

        _status_label = std::make_unique<Label>(*_panel);
        _status_label->setTextFont(&lv_font_montserrat_14);
        _status_label->setTextColor(lv_color_hex(kFgDim));
        _status_label->align(LV_ALIGN_BOTTOM_MID, 0, -46);

        _refresh_button = std::make_unique<Button>(*_panel);
        _refresh_button->align(LV_ALIGN_BOTTOM_MID, 0, -8);
        _refresh_button->setSize(120, 34);
        _refresh_button->setRadius(8);
        _refresh_button->setBgColor(lv_color_hex(0x3A2A14));
        _refresh_button->label().setTextColor(lv_color_hex(kFg));
        _refresh_button->label().setText("Refresh");
        _refresh_button->onClick().connect([this]() { _refresh_pending = true; });

        view::create_home_indicator([&]() { close(); }, kThemeColor, 0x3A2A14);
        view::create_status_bar(kThemeColor, 0x3A2A14);
    }

    _location = stackchan::portal::config::weather_get().location;

    if (_location.empty()) {
        LvglLockGuard lock;
        _status_label->setText("No location set -- add one in the web portal's Weather card");
        return;
    }

    {
        LvglLockGuard lock;
        _location_label->setText(_location.c_str());
        _status_label->setText("Loading...");
    }

    GetHAL().startNetwork([&](std::string_view msg) {
        LvglLockGuard lock;
        _status_label->setText(std::string(msg).c_str());
    });

    refresh();
    _last_fetch = GetHAL().millis();
}

void AppWeather::refresh()
{
    if (_location.empty()) {
        return;
    }

    auto network = Board::GetInstance().GetNetwork();
    double lat = 0, lon = 0;
    bool ok = resolve_location(network, _location, lat, lon);

    if (ok) {
        char url[256];
        std::snprintf(url, sizeof(url),
                       "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                       "&current=temperature_2m,weather_code,wind_speed_10m&temperature_unit=fahrenheit",
                       lat, lon);
        cJSON* root = nullptr;
        if (fetch_json(network, url, &root)) {
            cJSON* current = cJSON_GetObjectItemCaseSensitive(root, "current");
            cJSON* temp    = cJSON_GetObjectItemCaseSensitive(current, "temperature_2m");
            cJSON* wind    = cJSON_GetObjectItemCaseSensitive(current, "wind_speed_10m");
            cJSON* code    = cJSON_GetObjectItemCaseSensitive(current, "weather_code");
            if (cJSON_IsNumber(temp)) {
                _temp_f        = temp->valuedouble;
                _wind_mph      = cJSON_IsNumber(wind) ? wind->valuedouble : 0;
                _weather_code  = cJSON_IsNumber(code) ? code->valueint : -1;
                _have_data     = true;
                _last_fetch_ok = true;
            } else {
                ok = false;
            }
            cJSON_Delete(root);
        } else {
            ok = false;
        }
    }

    _last_fetch_ok = ok;
    render();
}

void AppWeather::render()
{
    LvglLockGuard lock;

    _status_label->setTextColor(lv_color_hex(_last_fetch_ok ? kFgDim : kErr));

    if (_have_data) {
        _temp_label->setText(fmt::format("{}°F", static_cast<int>(_temp_f + (_temp_f >= 0 ? 0.5 : -0.5))));
        _condition_label->setText(condition_text(_weather_code));
        _status_label->setText(_last_fetch_ok
                                    ? fmt::format("wind {} mph", static_cast<int>(_wind_mph + 0.5))
                                    : "showing last known reading -- refresh failed");
    } else {
        _status_label->setText(_last_fetch_ok ? "Loading..." : "Could not reach the weather service");
    }
}

void AppWeather::onRunning()
{
    if (!_location.empty() && (_refresh_pending || GetHAL().millis() - _last_fetch >= kRefreshIntervalMs)) {
        _refresh_pending = false;
        refresh();
        _last_fetch = GetHAL().millis();
    }

    LvglLockGuard lock;
    view::update_home_indicator();
    view::update_status_bar();
}

void AppWeather::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;
    _refresh_button.reset();
    _status_label.reset();
    _condition_label.reset();
    _temp_label.reset();
    _location_label.reset();
    _panel.reset();
}
