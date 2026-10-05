/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 *
 * VOLT — a neon HUD character skin for StackChan.
 *
 * Design notes
 * ------------
 * VOLT keeps the exact `Feature` contract the stock skin uses, so every existing modifier
 * (blink, breath, speaking lip-sync, idle_motion, head_pet, imu ...) drives it unchanged:
 *
 *   position  -100..100  -> pixel offset from the feature's home position
 *   weight       0..100  -> eyes: open amount (100 = wide open, 0 = shut)
 *                           mouth: open amount
 *   size      -100..100  -> eye scale
 *   rotation  tenths of a degree, CLOCKWISE (LVGL convention)
 *
 * The eyebrow is owned by VoltEyes rather than being a separate element, because
 * `KeyElements_t` only carries {leftEye, rightEye, mouth, speechBubble}. Driving the brow
 * from the eye's own rotation means the six stock `Emotion` presets animate the brows for
 * free — Angry (+450/-450) scowls inward, Sad (-400/+400) tilts outward, and so on.
 *
 * Glow is real LVGL shadow (setShadowWidth/Color/Spread/Opa), not a bitmap, so it costs no
 * flash and scales with the eye.
 */
#pragma once
#include "../skin_base.h"
#include "../../avatar/elements/feature.h"
#include <lvgl.h>
#include <smooth_lvgl.hpp>
#include <memory>

namespace stackchan::avatar {

/* ------------------------------------------------------------------ palette -- */
namespace volt_palette {
inline lv_color_t background()
{
    return lv_color_hex(0x06080E);
}
inline lv_color_t eyeNeon()
{
    return lv_color_hex(0x00E1FF);
}
inline lv_color_t eyeCore()
{
    return lv_color_hex(0xBEFFFF);
}
inline lv_color_t brow()
{
    return lv_color_hex(0xFF3CAA);
}
inline lv_color_t mouth()
{
    return lv_color_hex(0x00FFBE);
}
}  // namespace volt_palette

/**
 * @brief Glowing rounded-rect visor eye with an angular neon brow above it.
 */
class VoltEyes : public Feature {
public:
    VoltEyes(lv_obj_t* parent, bool isLeftEye);
    ~VoltEyes();

    void setPosition(const uitk::Vector2i& position) override;
    void setWeight(int weight) override;
    void setRotation(int rotation) override;
    void setEmotion(const Emotion& emotion) override;
    void setVisible(bool visible) override;
    void setSize(int size) override;

private:
    void relayout();

    bool _is_left_eye = false;
    int _eye_w = 0;
    int _eye_h = 0;

    std::unique_ptr<uitk::lvgl_cpp::Container> _container;  // holds eye + core, rotates
    std::unique_ptr<uitk::lvgl_cpp::Container> _eye;        // outer neon body
    std::unique_ptr<uitk::lvgl_cpp::Container> _core;       // inner bright fill
    std::unique_ptr<uitk::lvgl_cpp::Container> _brow;       // neon slash above the eye
};

/**
 * @brief Neon bar mouth. Widens-and-flattens when shut, narrows-and-opens when speaking.
 */
class VoltMouth : public Feature {
public:
    VoltMouth(lv_obj_t* parent);
    ~VoltMouth();

    void setPosition(const uitk::Vector2i& position) override;
    void setWeight(int weight) override;
    void setRotation(int rotation) override;
    void setVisible(bool visible) override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _mouth;
};

/**
 * @brief VOLT avatar — assembles the two eyes, the mouth and a reused speech bubble.
 */
class VoltAvatar : public SkinBase {
public:
    void init(lv_obj_t* parent, const lv_font_t* font = &lv_font_montserrat_16) override;
    uitk::lvgl_cpp::Container* getPanel() const override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
};

}  // namespace stackchan::avatar
