/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <smooth_lvgl.hpp>
#include <memory>
#include <string>
#include <cstdint>

/**
 * @brief Full-screen current-conditions display. Reads the location saved in the portal's
 * Weather card (stackchan::portal::config::weather_get()) and polls Open-Meteo directly
 * from the device -- same free, no-key API the bridge's voice weather tool already uses
 * (server/bridge/weather.py), just fetched here instead of asked for out loud.
 */
class AppWeather : public mooncake::AppAbility {
public:
    AppWeather();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void refresh();
    void render();

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _location_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _temp_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _condition_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _status_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _refresh_button;

    std::string _location;
    bool _have_data       = false;
    bool _last_fetch_ok   = true;
    bool _refresh_pending = false;
    uint32_t _last_fetch  = 0;

    double _temp_f   = 0;
    double _wind_mph = 0;
    int _weather_code = -1;
};
