/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 */
#include "skins.h"
#include "default/default.h"
#include "volt/volt.h"
#include "max/max.h"
#include <mooncake_log.h>

static const char* _tag = "skins";

namespace stackchan::avatar {

// MAX is the default character for this build.
static SkinId _current_skin = SkinId::Max;

const char* skin_name(SkinId id)
{
    switch (id) {
        case SkinId::Default:
            return "Default";
        case SkinId::Volt:
            return "VOLT";
        case SkinId::Max:
            return "MAX";
        default:
            return "Unknown";
    }
}

SkinId get_current_skin()
{
    return _current_skin;
}

void setCurrentSkin(SkinId id)
{
    if (id < SkinId::Default || id >= SkinId::_Count) {
        mclog::tagWarn(_tag, "ignoring out-of-range skin id {}", static_cast<int>(id));
        return;
    }
    _current_skin = id;
    mclog::tagInfo(_tag, "skin set to {}", skin_name(id));
}

std::unique_ptr<SkinBase> create_avatar_of(SkinId id, lv_obj_t* parent, const lv_font_t* font)
{
    std::unique_ptr<SkinBase> avatar;

    switch (id) {
        case SkinId::Max:
            avatar = std::make_unique<MaxAvatar>();
            break;
        case SkinId::Volt:
            avatar = std::make_unique<VoltAvatar>();
            break;
        case SkinId::Default:
        default:
            avatar = std::make_unique<DefaultAvatar>();
            break;
    }

    avatar->init(parent, font);
    return avatar;
}

std::unique_ptr<SkinBase> create_avatar(lv_obj_t* parent, const lv_font_t* font)
{
    return create_avatar_of(_current_skin, parent, font);
}

}  // namespace stackchan::avatar
