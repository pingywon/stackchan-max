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
 * @brief Full-screen single-ticker price display. Reads the symbol saved in the portal's
 * Stock ticker card (stackchan::portal::config::stock_get()) and polls Yahoo Finance's
 * public chart endpoint directly from the device -- no API key, but also undocumented and
 * not guaranteed stable; a fetch failure just keeps the last good reading on screen.
 */
class AppStock : public mooncake::AppAbility {
public:
    AppStock();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void refresh();
    void render();

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _symbol_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _price_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _change_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _status_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _refresh_button;

    std::string _symbol;
    bool _have_data       = false;
    bool _last_fetch_ok   = true;
    bool _refresh_pending = false;
    uint32_t _last_fetch  = 0;

    double _price     = 0;
    double _prev_close = 0;
    std::string _currency;
};
