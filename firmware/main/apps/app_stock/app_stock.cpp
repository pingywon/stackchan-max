/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_stock.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <apps/common/common.h>
#include <portal/portal_config.h>
#include <board.h>
#include <cJSON.h>
#include <fmt/format.h>

#include <cstdio>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;

namespace {
constexpr uint32_t kThemeColor        = 0x66BB6A;
constexpr uint32_t kBg                = 0x0B1A0E;
constexpr uint32_t kFg                = 0xE3FFE8;
constexpr uint32_t kFgDim             = 0x8FCB98;
constexpr uint32_t kUp                = 0x69F0AE;
constexpr uint32_t kDown              = 0xFF6B6B;
constexpr uint32_t kErr               = 0xFF6B6B;
constexpr uint32_t kRefreshIntervalMs = 60 * 1000;  // a price is worth checking more often than weather

const char* _tag = "AppStock";

// Undocumented but widely-used no-key quote endpoint. Yahoo blocks requests with no
// browser-like User-Agent, so one is set below; if this endpoint ever goes away, this is
// the one thing in this app that would need replacing.
std::string chart_url(const std::string& symbol)
{
    return "https://query1.finance.yahoo.com/v8/finance/chart/" + symbol + "?interval=1d&range=1d";
}

bool fetch_quote(NetworkInterface* network, const std::string& symbol, double& price, double& prev_close,
                  std::string& currency)
{
    auto http = network->CreateHttp(0);
    http->SetTimeout(8000);
    http->SetHeader("User-Agent",
                     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                     "(KHTML, like Gecko) Chrome/120.0 Safari/537.36");

    if (!http->Open("GET", chart_url(symbol))) {
        mclog::tagError(_tag, "failed to open http connection for {}", symbol);
        return false;
    }
    if (http->GetStatusCode() != 200) {
        mclog::tagError(_tag, "quote fetch for {} -> status {}", symbol, http->GetStatusCode());
        http->Close();
        return false;
    }
    std::string body = http->ReadAll();
    http->Close();

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        mclog::tagError(_tag, "failed to parse quote json for {}", symbol);
        return false;
    }

    bool ok        = false;
    cJSON* chart   = cJSON_GetObjectItemCaseSensitive(root, "chart");
    cJSON* results = cJSON_GetObjectItemCaseSensitive(chart, "result");
    if (cJSON_IsArray(results) && cJSON_GetArraySize(results) > 0) {
        cJSON* meta = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(results, 0), "meta");
        cJSON* jprice = cJSON_GetObjectItemCaseSensitive(meta, "regularMarketPrice");
        cJSON* jprev  = cJSON_GetObjectItemCaseSensitive(meta, "previousClose");
        if (!cJSON_IsNumber(jprev)) {
            jprev = cJSON_GetObjectItemCaseSensitive(meta, "chartPreviousClose");
        }
        cJSON* jcur = cJSON_GetObjectItemCaseSensitive(meta, "currency");
        if (cJSON_IsNumber(jprice)) {
            price      = jprice->valuedouble;
            prev_close = cJSON_IsNumber(jprev) ? jprev->valuedouble : price;
            currency   = (cJSON_IsString(jcur) && jcur->valuestring != nullptr) ? jcur->valuestring : "USD";
            ok         = true;
        }
    }

    cJSON_Delete(root);
    return ok;
}

}  // namespace

AppStock::AppStock()
{
    setAppInfo().name = "STOCKS";
    static uint32_t theme_color = kThemeColor;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppStock::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppStock::onOpen()
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

        _symbol_label = std::make_unique<Label>(*_panel);
        _symbol_label->setTextFont(&lv_font_montserrat_20);
        _symbol_label->setTextColor(lv_color_hex(kFgDim));
        _symbol_label->align(LV_ALIGN_TOP_MID, 0, 32);

        _price_label = std::make_unique<Label>(*_panel);
        _price_label->setTextFont(&lv_font_montserrat_48);
        _price_label->setTextColor(lv_color_hex(kFg));
        _price_label->align(LV_ALIGN_CENTER, 0, -8);
        _price_label->setText("--");

        _change_label = std::make_unique<Label>(*_panel);
        _change_label->setTextFont(&lv_font_montserrat_20);
        _change_label->setTextColor(lv_color_hex(kFgDim));
        _change_label->align(LV_ALIGN_CENTER, 0, 44);

        _status_label = std::make_unique<Label>(*_panel);
        _status_label->setTextFont(&lv_font_montserrat_14);
        _status_label->setTextColor(lv_color_hex(kFgDim));
        _status_label->align(LV_ALIGN_BOTTOM_MID, 0, -46);

        _refresh_button = std::make_unique<Button>(*_panel);
        _refresh_button->align(LV_ALIGN_BOTTOM_MID, 0, -8);
        _refresh_button->setSize(120, 34);
        _refresh_button->setRadius(8);
        _refresh_button->setBgColor(lv_color_hex(0x123018));
        _refresh_button->label().setTextColor(lv_color_hex(kFg));
        _refresh_button->label().setText("Refresh");
        _refresh_button->onClick().connect([this]() { _refresh_pending = true; });

        view::create_home_indicator([&]() { close(); }, kThemeColor, 0x123018);
        view::create_status_bar(kThemeColor, 0x123018);
    }

    _symbol = stackchan::portal::config::stock_get().symbol;

    if (_symbol.empty()) {
        LvglLockGuard lock;
        _status_label->setText("No ticker set -- add one in the web portal's Stock ticker card");
        return;
    }

    {
        LvglLockGuard lock;
        _symbol_label->setText(_symbol.c_str());
        _status_label->setText("Loading...");
    }

    GetHAL().startNetwork([&](std::string_view msg) {
        LvglLockGuard lock;
        _status_label->setText(std::string(msg).c_str());
    });

    refresh();
    _last_fetch = GetHAL().millis();
}

void AppStock::refresh()
{
    if (_symbol.empty()) {
        return;
    }

    auto network = Board::GetInstance().GetNetwork();
    double price = 0, prev_close = 0;
    std::string currency;
    bool ok = fetch_quote(network, _symbol, price, prev_close, currency);

    if (ok) {
        _price       = price;
        _prev_close  = prev_close;
        _currency    = currency;
        _have_data   = true;
    }

    _last_fetch_ok = ok;
    render();
}

void AppStock::render()
{
    LvglLockGuard lock;

    _status_label->setTextColor(lv_color_hex(_last_fetch_ok ? kFgDim : kErr));

    if (_have_data) {
        const char* sym = _currency == "USD" ? "$" : "";
        _price_label->setText(fmt::format("{}{:.2f}", sym, _price));

        double change     = _price - _prev_close;
        double change_pct = (_prev_close != 0) ? (change / _prev_close) * 100.0 : 0.0;
        _change_label->setTextColor(lv_color_hex(change >= 0 ? kUp : kDown));
        _change_label->setText(fmt::format("{}{:.2f} ({}{:.2f}%)", change >= 0 ? "+" : "", change,
                                            change_pct >= 0 ? "+" : "", change_pct));

        _status_label->setText(_last_fetch_ok ? "" : "showing last known price -- refresh failed");
    } else {
        _status_label->setText(_last_fetch_ok ? "Loading..." : "Could not reach the quote service");
    }
}

void AppStock::onRunning()
{
    if (!_symbol.empty() && (_refresh_pending || GetHAL().millis() - _last_fetch >= kRefreshIntervalMs)) {
        _refresh_pending = false;
        refresh();
        _last_fetch = GetHAL().millis();
    }

    LvglLockGuard lock;
    view::update_home_indicator();
    view::update_status_bar();
}

void AppStock::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;
    _refresh_button.reset();
    _status_label.reset();
    _change_label.reset();
    _price_label.reset();
    _symbol_label.reset();
    _panel.reset();
}
