/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_clock.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <apps/common/common.h>
#include <fmt/chrono.h>
#include <chrono>
#include <ctime>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;

namespace {
constexpr uint32_t kThemeColor = 0x40C4FF;
constexpr uint32_t kBg         = 0x0B1620;
constexpr uint32_t kFg         = 0xE6F4FF;
constexpr uint32_t kFgDim      = 0x7FB8DB;

const char* weekday_name(int wday)
{
    static const char* names[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                   "Thursday", "Friday", "Saturday"};
    return (wday >= 0 && wday < 7) ? names[wday] : "";
}

const char* month_name(int mon)
{
    static const char* names[] = {"January", "February", "March", "April", "May", "June",
                                   "July", "August", "September", "October", "November", "December"};
    return (mon >= 0 && mon < 12) ? names[mon] : "";
}
}  // namespace

AppClock::AppClock()
{
    setAppInfo().name = "CLOCK";
    static uint32_t theme_color = kThemeColor;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppClock::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppClock::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    LvglLockGuard lock;

    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setBgColor(lv_color_hex(kBg));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _time_label = std::make_unique<Label>(*_panel);
    _time_label->setTextFont(&lv_font_montserrat_48);
    _time_label->setTextColor(lv_color_hex(kFg));
    _time_label->align(LV_ALIGN_CENTER, 0, -18);

    _date_label = std::make_unique<Label>(*_panel);
    _date_label->setTextFont(&lv_font_montserrat_16);
    _date_label->setTextColor(lv_color_hex(kFgDim));
    _date_label->align(LV_ALIGN_CENTER, 0, 40);

    updateClock();
    _last_update = GetHAL().millis();

    view::create_home_indicator([&]() { close(); }, kThemeColor, 0x0A2733);
    view::create_status_bar(kThemeColor, 0x0A2733);
}

void AppClock::updateClock()
{
    auto now   = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);

    struct tm local_tm;
    localtime_r(&now_t, &local_tm);

    int hour12 = local_tm.tm_hour % 12;
    if (hour12 == 0) {
        hour12 = 12;
    }

    _time_label->setText(fmt::format("{}:{:02d}:{:02d} {}", hour12, local_tm.tm_min, local_tm.tm_sec,
                                      local_tm.tm_hour >= 12 ? "PM" : "AM"));
    _date_label->setText(fmt::format("{}, {} {}, {}", weekday_name(local_tm.tm_wday),
                                      month_name(local_tm.tm_mon), local_tm.tm_mday,
                                      1900 + local_tm.tm_year));
}

void AppClock::onRunning()
{
    LvglLockGuard lock;

    if (GetHAL().millis() - _last_update >= 1000) {
        updateClock();
        _last_update = GetHAL().millis();
    }

    view::update_home_indicator();
    view::update_status_bar();
}

void AppClock::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;
    _date_label.reset();
    _time_label.reset();
    _panel.reset();
}
