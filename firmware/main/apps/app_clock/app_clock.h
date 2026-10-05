/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <smooth_lvgl.hpp>
#include <memory>
#include <cstdint>

/**
 * @brief Full-screen clock face. No network needed -- reads the system clock, which NTP
 * already keeps correct in the background.
 */
class AppClock : public mooncake::AppAbility {
public:
    AppClock();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void updateClock();

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _time_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _date_label;

    uint32_t _last_update = 0;
};
