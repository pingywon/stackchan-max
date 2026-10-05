/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 */
#include "volt.h"
#include "../default/default.h"

using namespace uitk::lvgl_cpp;
using namespace stackchan::avatar;

void VoltAvatar::init(lv_obj_t* parent, const lv_font_t* font)
{
    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setBgColor(volt_palette::background());
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _panel->setPadding(0, 0, 0, 0);

    _key_elements.leftEye  = std::make_unique<VoltEyes>(_panel->get(), true);
    _key_elements.rightEye = std::make_unique<VoltEyes>(_panel->get(), false);
    _key_elements.mouth    = std::make_unique<VoltMouth>(_panel->get());

    // Reuse the stock speech bubble rather than duplicating it; recoloured to match VOLT.
    _key_elements.speechBubble = std::make_unique<DefaultSpeechBubble>(
        _panel->get(), volt_palette::eyeCore(), volt_palette::background(), font);
}

Container* VoltAvatar::getPanel() const
{
    if (_panel) {
        return _panel.get();
    }
    return NULL;
}
