/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_character.h"
#include <hal/hal.h>
#include <hal/board/hal_bridge.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <stackchan/avatar/skins/skins.h>
#include <portal/portal_config.h>
#include <apps/common/common.h>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace stackchan::avatar;

AppCharacter::AppCharacter()
{
    setAppInfo().name = "CHARACTER";
    static auto icon  = assets::get_image("icon_sentinel.bin");
    setAppInfo().icon = (void*)&icon;
    static uint32_t theme_color = 0x9966FF;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppCharacter::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppCharacter::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    LvglLockGuard lock;

    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setBgColor(lv_color_hex(0x0F1115));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _title = std::make_unique<Label>(*_panel);
    _title->setText("Pick a character");
    _title->setTextFont(&lv_font_montserrat_20);
    _title->setTextColor(lv_color_hex(0xE6E8EB));
    _title->align(LV_ALIGN_TOP_MID, 0, 30);

    _status = std::make_unique<Label>(*_panel);
    _status->setText("");
    _status->setTextFont(&lv_font_montserrat_14);
    _status->setTextColor(lv_color_hex(0x8A92A3));
    _status->align(LV_ALIGN_BOTTOM_MID, 0, -34);

    rebuildList();

    view::create_home_indicator([&]() { close(); }, 0xB19CFF, 0x2A1B4D);
    view::create_status_bar(0xB19CFF, 0x2A1B4D);
}

void AppCharacter::rebuildList()
{
    _buttons.clear();

    const SkinId current = get_current_skin();
    const int count       = static_cast<int>(SkinId::_Count);
    const int btn_h        = 44;
    const int gap          = 10;
    const int start_y      = 64;

    for (int i = 0; i < count; i++) {
        auto skin = static_cast<SkinId>(i);
        bool is_current = (skin == current);

        auto btn = std::make_unique<Button>(*_panel);
        btn->align(LV_ALIGN_TOP_MID, 0, start_y + i * (btn_h + gap));
        btn->setSize(240, btn_h);
        btn->setRadius(8);
        if (is_current) {
            btn->setBgColor(lv_color_hex(0x5B9DFF));
            btn->label().setTextColor(lv_color_hex(0x04101F));
        } else {
            btn->setBgColor(lv_color_hex(0x161A21));
            btn->label().setTextColor(lv_color_hex(0xE6E8EB));
        }
        btn->label().setText(is_current ? (std::string(skin_name(skin)) + "  (current)").c_str()
                                         : skin_name(skin));
        btn->onClick().connect([this, skin]() {
            using namespace stackchan::portal::config;
            skin_set(static_cast<int>(skin));
            bool applied_live = hal_bridge::display_swap_skin(skin);
            _status->setText(applied_live
                                  ? "Applied live."
                                  : "Saved -- open AI Agent chat to see it (or reboot).");
            rebuildList();
        });

        _buttons.push_back(std::move(btn));
    }
}

void AppCharacter::onRunning()
{
    LvglLockGuard lock;
    view::update_home_indicator();
    view::update_status_bar();
}

void AppCharacter::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;
    _buttons.clear();
    _status.reset();
    _title.reset();
    _panel.reset();
}
