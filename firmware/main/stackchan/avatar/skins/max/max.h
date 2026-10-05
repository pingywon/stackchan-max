/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 *
 * MAX — a Max Headroom styled character skin.
 *
 * Composition
 * -----------
 * The static art (wireframe backdrop, slicked hair, tapered face, collar and tie) is baked
 * to a single 320x240 RGB565 asset, `max_backplate.bin`, packed into the assets partition
 * via main/assets/assets_bin. Only the parts that must animate are live LVGL objects drawn
 * on top of it:
 *
 *   - two sunglass lenses (the eyes) with a specular glint
 *   - an eyebrow slash above each lens, driven by the eye's rotation
 *   - the toothy grin
 *
 * That keeps per-frame drawing cheap (one blit + a handful of rects) while giving art
 * quality that LVGL primitives alone can't reach. Regenerate the plate with
 * `design/make_backplate.py` then `tools/photo2asset.py --format rgb565`.
 *
 * As with VOLT, every feature honours the stock `Feature` contract, so blink / breath /
 * speaking / idle_motion / head_pet modifiers all drive MAX unchanged.
 */
#pragma once
#include "../skin_base.h"
#include "../../avatar/elements/feature.h"
#include <lvgl.h>
#include <smooth_lvgl.hpp>
#include <memory>

namespace stackchan::avatar {

namespace max_palette {
inline lv_color_t lens()
{
    return lv_color_hex(0x0E0E1A);
}
inline lv_color_t glint()
{
    return lv_color_hex(0x78E6FF);
}
inline lv_color_t brow()
{
    // Deliberately NOT the hair colour (0xD69632). The brows sit just below the hairline
    // and were invisible against it — which silently killed the whole expression channel,
    // because rotation of these bars is how every emotion reads.
    return lv_color_hex(0x6B3F0E);
}
inline lv_color_t lip()
{
    return lv_color_hex(0x963C46);
}
inline lv_color_t mouthDark()
{
    return lv_color_hex(0x280C14);
}
inline lv_color_t teeth()
{
    return lv_color_hex(0xF8F4EC);
}
inline lv_color_t backdrop()
{
    return lv_color_hex(0x080A1E);
}
}  // namespace max_palette

/**
 * @brief Sunglass lens + specular glint + brow slash.
 *
 * `weight` squashes the lens vertically, which reads as a blink behind the shades.
 */
class MaxEyes : public Feature {
public:
    MaxEyes(lv_obj_t* parent, bool isLeftEye);
    ~MaxEyes();

    void setPosition(const uitk::Vector2i& position) override;
    void setWeight(int weight) override;
    void setRotation(int rotation) override;
    void setEmotion(const Emotion& emotion) override;
    void setVisible(bool visible) override;
    void setSize(int size) override;

private:
    void relayout();

    bool _is_left_eye = false;
    int _lens_w = 0;
    int _lens_h = 0;

    std::unique_ptr<uitk::lvgl_cpp::Container> _lens;
    std::unique_ptr<uitk::lvgl_cpp::Container> _glint;
    std::unique_ptr<uitk::lvgl_cpp::Container> _brow;
};

/**
 * @brief The grin: lip frame, dark interior, and a top (plus bottom, when open) tooth row.
 */
class MaxMouth : public Feature {
public:
    MaxMouth(lv_obj_t* parent);
    ~MaxMouth();

    void setPosition(const uitk::Vector2i& position) override;
    void setWeight(int weight) override;
    void setRotation(int rotation) override;
    void setVisible(bool visible) override;

private:
    void relayout();

    std::unique_ptr<uitk::lvgl_cpp::Container> _lip;
    std::unique_ptr<uitk::lvgl_cpp::Container> _cavity;
    std::unique_ptr<uitk::lvgl_cpp::Container> _teeth_top;
    std::unique_ptr<uitk::lvgl_cpp::Container> _teeth_bottom;
};

/**
 * @brief MAX avatar — backplate image plus the live features.
 */
class MaxAvatar : public SkinBase {
public:
    void init(lv_obj_t* parent, const lv_font_t* font = &lv_font_montserrat_16) override;
    uitk::lvgl_cpp::Container* getPanel() const override;

private:
    // Declaration order matters: LVGL keeps a raw pointer to the descriptor, and members are
    // destroyed in reverse declaration order. The descriptor must therefore be declared
    // BEFORE the Image so it is destroyed after it.
    lv_image_dsc_t _backplate_dsc = {};
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Image> _backplate;
};

}  // namespace stackchan::avatar
