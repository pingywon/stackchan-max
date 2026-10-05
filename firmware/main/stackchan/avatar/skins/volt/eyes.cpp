/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 */
#include "volt.h"

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace stackchan::avatar;

/* Home position and travel limits. Mirrors the stock skin's structure so the same
 * -100..100 position range feels equivalent between skins. */
static const Vector2i _eye_pos        = Vector2i(-66, -18);
static const Vector2i _eye_min_offset = Vector2i(-16, -16);
static const Vector2i _eye_max_offset = Vector2i(16, 16);

/* Eye body scales with `size`; height then scales with `weight` to blink. */
static const int _eye_size_min = 26;
static const int _eye_size_max = 52;
static const int _lid_closed_pct = 10;   // height at weight 0, percent of full
static const int _brow_h_pct     = 17;   // brow thickness, percent of eye width
static const int _brow_w_pct     = 115;  // brow length, percent of eye width
static const int _brow_gap_pct   = 42;   // brow lift above eye top, percent of eye width

/* Container is square and sized to the largest possible eye so rotation never clips. */
static const int _box = _eye_size_max + 8;

VoltEyes::VoltEyes(lv_obj_t* parent, bool isLeftEye)
{
    _is_left_eye = isLeftEye;

    _container = std::make_unique<Container>(parent);
    _container->setAlign(LV_ALIGN_CENTER);
    _container->setRadius(0);
    _container->setBorderWidth(0);
    _container->setBgOpa(0);
    _container->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _container->setPadding(0, 0, 0, 0);
    _container->setSize(_box, _box);
    _container->setTransformPivot(_box / 2, _box / 2);

    _eye = std::make_unique<Container>(_container->get());
    _eye->align(LV_ALIGN_CENTER, 0, 0);
    _eye->setBorderWidth(0);
    _eye->setBgColor(volt_palette::eyeNeon());
    _eye->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _eye->setPadding(0, 0, 0, 0);
    // neon bloom
    _eye->setShadowColor(volt_palette::eyeNeon());
    _eye->setShadowWidth(18);
    _eye->setShadowSpread(2);
    _eye->setShadowOpa(LV_OPA_60);

    _core = std::make_unique<Container>(_container->get());
    _core->align(LV_ALIGN_CENTER, 0, 0);
    _core->setBorderWidth(0);
    _core->setBgColor(volt_palette::eyeCore());
    _core->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _core->setPadding(0, 0, 0, 0);

    // Brow lives outside the rotating container so its own rotation is independent of
    // the eye body, and so a blink (which squashes the eye) never moves it.
    _brow = std::make_unique<Container>(parent);
    _brow->setAlign(LV_ALIGN_CENTER);
    _brow->setBorderWidth(0);
    _brow->setBgColor(volt_palette::brow());
    _brow->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _brow->setPadding(0, 0, 0, 0);
    _brow->setShadowColor(volt_palette::brow());
    _brow->setShadowWidth(12);
    _brow->setShadowSpread(1);
    _brow->setShadowOpa(LV_OPA_50);

    setSize(0);
    setWeight(100);
    setPosition(_position);
    setRotation(0);
}

VoltEyes::~VoltEyes()
{
    _brow.reset();
    _core.reset();
    _eye.reset();
    _container.reset();
}

/* Recompute eye body + core + brow geometry from the current size/weight. */
void VoltEyes::relayout()
{
    int full = map_range(_size, -100, 100, _eye_size_min, _eye_size_max);
    _eye_w   = full;
    _eye_h   = map_range(_weight, 0, 100, full * _lid_closed_pct / 100, full);
    if (_eye_h < 2) {
        _eye_h = 2;
    }

    int radius = (_eye_w < _eye_h ? _eye_w : _eye_h) * 42 / 100;

    _eye->setSize(_eye_w, _eye_h);
    _eye->setRadius(radius);

    // Inner core inset; hide it once the eye is squeezed too thin to show a rim.
    int inset_x = _eye_w * 20 / 100;
    int inset_y = _eye_h * 22 / 100;
    int core_w  = _eye_w - inset_x * 2;
    int core_h  = _eye_h - inset_y * 2;
    if (core_w > 1 && core_h > 1 && _eye_h > 8) {
        _core->setHidden(!_visible ? true : false);
        _core->setSize(core_w, core_h);
        _core->setRadius(radius / 2);
    } else {
        _core->setHidden(true);
    }

    int brow_w = _eye_w * _brow_w_pct / 100;
    int brow_h = _eye_w * _brow_h_pct / 100;
    if (brow_h < 3) {
        brow_h = 3;
    }
    _brow->setSize(brow_w, brow_h);
    _brow->setRadius(brow_h / 2);
    _brow->setTransformPivot(brow_w / 2, brow_h / 2);
}

void VoltEyes::setPosition(const Vector2i& position)
{
    Element::setPosition(position);

    auto pos_x = _is_left_eye ? _eye_pos.x : -_eye_pos.x;
    pos_x += map_range(_position.x, -100, 100, _eye_min_offset.x, _eye_max_offset.x);
    auto pos_y = _eye_pos.y + map_range(_position.y, -100, 100, _eye_min_offset.y, _eye_max_offset.y);

    _container->setPos(pos_x, pos_y);

    // Brow tracks the eye, floating a fixed gap above the *full* eye box so it stays put
    // while the eye blinks underneath it.
    int gap = _eye_w * _brow_gap_pct / 100;
    _brow->setPos(pos_x, pos_y - gap);
}

void VoltEyes::setWeight(int weight)
{
    Feature::setWeight(weight);
    relayout();
}

void VoltEyes::setSize(int size)
{
    Feature::setSize(size);
    relayout();
    // geometry changed -> reposition so the brow gap tracks the new eye width
    setPosition(_position);
}

void VoltEyes::setRotation(int rotation)
{
    Element::setRotation(rotation);

    // NOTE: Element::setRotation() clamps _rotation to 0..3600, but the stock skins pass the
    // raw argument straight to LVGL — and the built-in emotion presets rely on negative
    // rotations (Sad = -400, and every right eye negates). We do the same here deliberately.
    // Never read this back via getRotation(); it will report 0 for negatives.
    _brow->setRotation(rotation);
}

void VoltEyes::setEmotion(const Emotion& emotion)
{
    if (getIgnoreEmotion()) {
        return;
    }

    /* Rotation here drives the BROW, not the eye body, so the angles are small — a brow
     * reads as a scowl at ~25 degrees, not 155. (The stock skin's large values rotate the
     * whole eye+eyelid container to flip the lid, which is a different mechanism entirely.)
     *
     * LVGL rotates clockwise for positive values. On the LEFT brow the inner end is the
     * right-hand end, so positive => inner end DOWN => scowl. Negative => inner end up.
     *
     * `mirror` lets an emotion break symmetry — Doubt cocks one brow. */
    auto apply_style = [this](int weight, int leftRotation, int rightRotation) {
        setWeight(weight);
        setRotation(_is_left_eye ? leftRotation : rightRotation);
    };

    // Same six-emotion vocabulary as the stock skin, so agent-driven emotions map 1:1.
    switch (emotion) {
        case Emotion::Neutral:
            apply_style(100, 0, 0);
            break;
        case Emotion::Happy:
            apply_style(72, -170, 170);  // gentle outward arch
            break;
        case Emotion::Angry:
            apply_style(62, 280, -280);  // inner ends driven down
            break;
        case Emotion::Sad:
            apply_style(74, -260, 260);  // inner ends lifted
            break;
        case Emotion::Doubt:
            apply_style(84, -330, 90);  // asymmetric: one brow cocked
            break;
        case Emotion::Sleepy:
            apply_style(26, -90, 90);  // heavy lids, brows barely tilted
            break;
        default:
            break;
    }
}

void VoltEyes::setVisible(bool visible)
{
    Element::setVisible(visible);

    _container->setHidden(!visible);
    _brow->setHidden(!visible);
    if (!visible) {
        _core->setHidden(true);
    } else {
        relayout();
    }
}
