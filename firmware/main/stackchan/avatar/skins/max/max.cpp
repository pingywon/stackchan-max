/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 */
#include "max.h"
#include "../default/default.h"
#include <assets/assets.h>
#include <mooncake_log.h>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace stackchan::avatar;

static const char* _tag = "skin-max";

/* ------------------------------------------------------------------- eyes -- */
/* Lens home positions match the baked backplate art. If you regenerate the plate with
 * different head geometry, these must move with it. */
static const Vector2i _lens_pos        = Vector2i(-30, -2);
static const Vector2i _lens_min_offset = Vector2i(-9, -8);
static const Vector2i _lens_max_offset = Vector2i(9, 8);
static const int _lens_w_min = 34;
static const int _lens_w_max = 46;
static const int _lens_h_min = 7;   // fully "closed"
static const int _lens_h_max = 26;  // wide open
static const int _brow_gap   = 6;

MaxEyes::MaxEyes(lv_obj_t* parent, bool isLeftEye)
{
    _is_left_eye = isLeftEye;

    _lens = std::make_unique<Container>(parent);
    _lens->setAlign(LV_ALIGN_CENTER);
    _lens->setBorderWidth(0);
    _lens->setBgColor(max_palette::lens());
    _lens->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _lens->setPadding(0, 0, 0, 0);
    _lens->setShadowColor(max_palette::lens());
    _lens->setShadowWidth(10);
    _lens->setShadowSpread(1);
    _lens->setShadowOpa(LV_OPA_40);

    _glint = std::make_unique<Container>(_lens->get());
    _glint->setBorderWidth(0);
    _glint->setBgColor(max_palette::glint());
    _glint->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _glint->setPadding(0, 0, 0, 0);

    _brow = std::make_unique<Container>(parent);
    _brow->setAlign(LV_ALIGN_CENTER);
    _brow->setBorderWidth(0);
    _brow->setBgColor(max_palette::brow());
    _brow->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _brow->setPadding(0, 0, 0, 0);

    setSize(0);
    setWeight(100);
    setPosition(_position);
    setRotation(0);
}

MaxEyes::~MaxEyes()
{
    _brow.reset();
    _glint.reset();
    _lens.reset();
}

void MaxEyes::relayout()
{
    _lens_w = map_range(_size, -100, 100, _lens_w_min, _lens_w_max);
    _lens_h = map_range(_weight, 0, 100, _lens_h_min, _lens_h_max);
    if (_lens_h < 2) {
        _lens_h = 2;
    }

    _lens->setSize(_lens_w, _lens_h);
    _lens->setRadius(_lens_h / 2 < 7 ? _lens_h / 2 : 7);

    // Specular glint sits high and outboard, and vanishes once the lens is squeezed shut.
    int gw = _lens_w * 42 / 100;
    int gh = _lens_h * 26 / 100;
    if (gh >= 2 && _lens_h > 10) {
        _glint->setHidden(false);
        _glint->setSize(gw, gh);
        _glint->setRadius(gh / 2);
        _glint->align(LV_ALIGN_CENTER, -_lens_w * 30 / 100, -_lens_h * 22 / 100);
    } else {
        _glint->setHidden(true);
    }

    int bw = _lens_w * 102 / 100;
    int bh = 5;
    _brow->setSize(bw, bh);
    _brow->setRadius(2);
    _brow->setTransformPivot(bw / 2, bh / 2);
}

void MaxEyes::setPosition(const Vector2i& position)
{
    Element::setPosition(position);

    auto pos_x = _is_left_eye ? _lens_pos.x : -_lens_pos.x;
    pos_x += map_range(_position.x, -100, 100, _lens_min_offset.x, _lens_max_offset.x);
    auto pos_y = _lens_pos.y + map_range(_position.y, -100, 100, _lens_min_offset.y, _lens_max_offset.y);

    _lens->setPos(pos_x, pos_y);
    _brow->setPos(pos_x, pos_y - _lens_h_max / 2 - _brow_gap);
}

void MaxEyes::setWeight(int weight)
{
    Feature::setWeight(weight);
    relayout();
}

void MaxEyes::setSize(int size)
{
    Feature::setSize(size);
    relayout();
    setPosition(_position);
}

void MaxEyes::setRotation(int rotation)
{
    Element::setRotation(rotation);
    // Raw value on purpose — see FINDINGS.md 4a. Negative rotations render but getRotation()
    // reports 0, and the emotion presets below depend on negatives.
    _brow->setRotation(rotation);
}

void MaxEyes::setEmotion(const Emotion& emotion)
{
    if (getIgnoreEmotion()) {
        return;
    }

    auto apply_style = [this](int weight, int leftRotation, int rightRotation) {
        setWeight(weight);
        setRotation(_is_left_eye ? leftRotation : rightRotation);
    };

    switch (emotion) {
        case Emotion::Neutral:
            apply_style(100, 0, 0);
            break;
        case Emotion::Happy:
            apply_style(78, -170, 170);
            break;
        case Emotion::Angry:
            apply_style(58, 280, -280);
            break;
        case Emotion::Sad:
            apply_style(72, -260, 260);
            break;
        case Emotion::Doubt:
            apply_style(88, -330, 90);  // one brow cocked — very Max
            break;
        case Emotion::Sleepy:
            apply_style(24, -90, 90);
            break;
        default:
            break;
    }
}

void MaxEyes::setVisible(bool visible)
{
    Element::setVisible(visible);
    _lens->setHidden(!visible);
    _brow->setHidden(!visible);
    if (visible) {
        relayout();
    }
}

/* ------------------------------------------------------------------ mouth -- */
static const Vector2i _mouth_pos        = Vector2i(0, 30);
static const Vector2i _mouth_min_offset = Vector2i(-8, -8);
static const Vector2i _mouth_max_offset = Vector2i(8, 8);
static const Vector2i _mouth_min_size   = Vector2i(74, 10);
static const Vector2i _mouth_max_size   = Vector2i(84, 34);

MaxMouth::MaxMouth(lv_obj_t* parent)
{
    _lip = std::make_unique<Container>(parent);
    _lip->setAlign(LV_ALIGN_CENTER);
    _lip->setBorderWidth(0);
    _lip->setBgColor(max_palette::lip());
    _lip->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _lip->setPadding(0, 0, 0, 0);

    _cavity = std::make_unique<Container>(_lip->get());
    _cavity->setBorderWidth(0);
    _cavity->setBgColor(max_palette::mouthDark());
    _cavity->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _cavity->setPadding(0, 0, 0, 0);
    _cavity->align(LV_ALIGN_CENTER, 0, 0);

    _teeth_top = std::make_unique<Container>(_cavity->get());
    _teeth_top->setBorderWidth(0);
    _teeth_top->setBgColor(max_palette::teeth());
    _teeth_top->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _teeth_top->setPadding(0, 0, 0, 0);

    _teeth_bottom = std::make_unique<Container>(_cavity->get());
    _teeth_bottom->setBorderWidth(0);
    _teeth_bottom->setBgColor(max_palette::teeth());
    _teeth_bottom->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _teeth_bottom->setPadding(0, 0, 0, 0);

    setPosition(_position);
    setWeight(0);
    setRotation(0);
}

MaxMouth::~MaxMouth()
{
    _teeth_bottom.reset();
    _teeth_top.reset();
    _cavity.reset();
    _lip.reset();
}

void MaxMouth::relayout()
{
    int w = map_range(_weight, 0, 100, _mouth_min_size.x, _mouth_max_size.x);
    int h = map_range(_weight, 0, 100, _mouth_min_size.y, _mouth_max_size.y);

    _lip->setSize(w, h);
    _lip->setRadius(6);
    _lip->setTransformPivot(w / 2, h / 2);

    int iw = w - 6;
    int ih = h - 6;
    if (iw < 2 || ih < 2) {
        _cavity->setHidden(true);
        return;
    }
    _cavity->setHidden(false);
    _cavity->setSize(iw, ih);
    _cavity->setRadius(4);

    int th = ih * 52 / 100;
    if (th > 11) {
        th = 11;
    }
    if (th < 2) {
        th = 2;
    }
    _teeth_top->setSize(iw, th);
    _teeth_top->setRadius(2);
    _teeth_top->align(LV_ALIGN_TOP_MID, 0, 0);

    if (ih > 16) {
        _teeth_bottom->setHidden(false);
        _teeth_bottom->setSize(iw, th * 8 / 10);
        _teeth_bottom->setRadius(2);
        _teeth_bottom->align(LV_ALIGN_BOTTOM_MID, 0, 0);
    } else {
        _teeth_bottom->setHidden(true);
    }
}

void MaxMouth::setPosition(const Vector2i& position)
{
    Element::setPosition(position);

    auto pos_x = _mouth_pos.x + map_range(_position.x, -100, 100, _mouth_min_offset.x, _mouth_max_offset.x);
    auto pos_y = _mouth_pos.y + map_range(_position.y, -100, 100, _mouth_min_offset.y, _mouth_max_offset.y);

    _lip->setPos(pos_x, pos_y);
}

void MaxMouth::setWeight(int weight)
{
    Feature::setWeight(weight);
    relayout();
}

void MaxMouth::setRotation(int rotation)
{
    Element::setRotation(rotation);
    _lip->setTransformPivot(_lip->getWidth() / 2, _lip->getHeight() / 2);
    _lip->setRotation(rotation);
}

void MaxMouth::setVisible(bool visible)
{
    Element::setVisible(visible);
    _lip->setHidden(!visible);
}

/* ----------------------------------------------------------------- avatar -- */
void MaxAvatar::init(lv_obj_t* parent, const lv_font_t* font)
{
    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setBgColor(max_palette::backdrop());
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _panel->setPadding(0, 0, 0, 0);

    // Baked art. If the asset is missing the panel just stays dark and the live features
    // still render, so a bad/absent plate degrades to a usable face rather than a crash.
    _backplate_dsc = assets::get_image("max_backplate.bin");
    if (_backplate_dsc.data != nullptr && _backplate_dsc.data_size > 0) {
        _backplate = std::make_unique<Image>(_panel->get());
        _backplate->setSrc(&_backplate_dsc);
        _backplate->align(LV_ALIGN_CENTER, 0, 0);
    } else {
        mclog::tagWarn(_tag, "max_backplate.bin not found in assets partition");
    }

    _key_elements.leftEye  = std::make_unique<MaxEyes>(_panel->get(), true);
    _key_elements.rightEye = std::make_unique<MaxEyes>(_panel->get(), false);
    _key_elements.mouth    = std::make_unique<MaxMouth>(_panel->get());
    _key_elements.speechBubble =
        std::make_unique<DefaultSpeechBubble>(_panel->get(), max_palette::teeth(),
                                              max_palette::backdrop(), font);
}

Container* MaxAvatar::getPanel() const
{
    if (_panel) {
        return _panel.get();
    }
    return NULL;
}
