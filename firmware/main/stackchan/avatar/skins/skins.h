/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 *
 * Runtime skin selection.
 *
 * Call sites should build avatars through `create_avatar()` instead of naming a concrete
 * skin, so the active character can be switched from the setup UI or the web portal
 * without touching every app.
 */
#pragma once
#include "skin_base.h"
#include <memory>

namespace stackchan::avatar {

enum class SkinId : int {
    Default = 0,
    Volt    = 1,
    Max     = 2,
    _Count
};

/** Human-readable name, safe for UI and logs. */
const char* skin_name(SkinId id);

/** Currently selected skin (process-wide). Defaults to Max. */
SkinId get_current_skin();
void setCurrentSkin(SkinId id);

/**
 * @brief Construct and initialise the currently selected skin.
 *
 * @param parent LVGL parent object
 * @param font   font handed to the speech bubble
 * @return an initialised skin, never null
 */
std::unique_ptr<SkinBase> create_avatar(lv_obj_t* parent, const lv_font_t* font = &lv_font_montserrat_16);

/** Construct a specific skin regardless of the current selection. */
std::unique_ptr<SkinBase> create_avatar_of(SkinId id, lv_obj_t* parent,
                                           const lv_font_t* font = &lv_font_montserrat_16);

}  // namespace stackchan::avatar
