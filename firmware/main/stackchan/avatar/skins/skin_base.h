/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 *
 * Common base for selectable avatar skins.
 *
 * The stock firmware instantiated `DefaultAvatar` directly in five places. To make the
 * skin swappable at runtime (and later, from the web portal), every skin now derives from
 * SkinBase and is built through `create_avatar()` in skins.h.
 */
#pragma once
#include "../avatar/avatar.h"
#include <lvgl.h>
#include <smooth_lvgl.hpp>

namespace stackchan::avatar {

class SkinBase : public Avatar {
public:
    /**
     * @brief Build the skin's LVGL object tree under `parent`.
     */
    virtual void init(lv_obj_t* parent, const lv_font_t* font = &lv_font_montserrat_16) = 0;

    /**
     * @brief Root panel — used for hit-testing (app_avatar connects onClick to it).
     */
    virtual uitk::lvgl_cpp::Container* getPanel() const = 0;
};

}  // namespace stackchan::avatar
