/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 *
 * On-device character/skin picker. The portal has always been able to change the skin;
 * this is the same choice, reachable without a phone or laptop -- tap a name, it's saved
 * and applied live if the AI-agent screen already has an avatar built, or takes effect
 * the next time that screen opens if it doesn't yet.
 */
#pragma once
#include <mooncake.h>
#include <smooth_lvgl.hpp>
#include <vector>

class AppCharacter : public mooncake::AppAbility {
public:
    AppCharacter();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void rebuildList();

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _status;
    std::vector<std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>> _buttons;
};
