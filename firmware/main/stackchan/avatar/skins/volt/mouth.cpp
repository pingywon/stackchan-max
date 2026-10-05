/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 */
#include "volt.h"

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace stackchan::avatar;

static const Vector2i _mouth_pos        = Vector2i(0, 40);
static const Vector2i _mouth_min_offset = Vector2i(-16, -16);
static const Vector2i _mouth_max_offset = Vector2i(16, 16);

/* Shut = a wide thin neon bar; open = narrower and taller. Same directional idea as the
 * stock mouth (which goes 90x6 -> 60x50), so lip-sync driven by `speaking` reads correctly. */
static const Vector2i _mouth_min_size = Vector2i(86, 7);
static const Vector2i _mouth_max_size = Vector2i(62, 44);
static const int _mouth_min_radius    = 3;
static const int _mouth_max_radius    = 18;

VoltMouth::VoltMouth(lv_obj_t* parent)
{
    _mouth = std::make_unique<Container>(parent);
    _mouth->setAlign(LV_ALIGN_CENTER);
    _mouth->setBorderWidth(0);
    _mouth->setBgColor(volt_palette::mouth());
    _mouth->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _mouth->setPadding(0, 0, 0, 0);
    _mouth->setShadowColor(volt_palette::mouth());
    _mouth->setShadowWidth(16);
    _mouth->setShadowSpread(1);
    _mouth->setShadowOpa(LV_OPA_60);

    setPosition(_position);
    setWeight(0);
    setRotation(0);
}

VoltMouth::~VoltMouth()
{
    _mouth.reset();
}

void VoltMouth::setPosition(const Vector2i& position)
{
    Element::setPosition(position);

    auto pos_x = _mouth_pos.x + map_range(_position.x, -100, 100, _mouth_min_offset.x, _mouth_max_offset.x);
    auto pos_y = _mouth_pos.y + map_range(_position.y, -100, 100, _mouth_min_offset.y, _mouth_max_offset.y);

    _mouth->setPos(pos_x, pos_y);
}

void VoltMouth::setWeight(int weight)
{
    Feature::setWeight(weight);

    auto size_x = map_range(_weight, 0, 100, _mouth_min_size.x, _mouth_max_size.x);
    auto size_y = map_range(_weight, 0, 100, _mouth_min_size.y, _mouth_max_size.y);
    auto radius = map_range(_weight, 0, 100, _mouth_min_radius, _mouth_max_radius);

    _mouth->setSize(size_x, size_y);
    _mouth->setRadius(radius);
    _mouth->setTransformPivot(size_x / 2, size_y / 2);
}

void VoltMouth::setRotation(int rotation)
{
    Element::setRotation(rotation);

    _mouth->setTransformPivot(_mouth->getWidth() / 2, _mouth->getHeight() / 2);
    _mouth->setRotation(rotation);
}

void VoltMouth::setVisible(bool visible)
{
    Element::setVisible(visible);

    _mouth->setHidden(!visible);
}
