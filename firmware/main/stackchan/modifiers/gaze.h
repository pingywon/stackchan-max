/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../modifiable.h"
#include <hal/hal.h>
#include <cstdint>

namespace stackchan {

/**
 * @brief Make the eyes look in the direction the head is moving.
 *
 * Two contributions are summed:
 *
 *  - **lead**  — from the angular velocity of the yaw/pitch servos. When the head starts
 *                turning right, the eyes snap right *ahead* of the body. This is the part
 *                that reads as intent; without it the eyes look dragged along.
 *  - **bias**  — a gentle pull toward the current head angle, so a head parked off-centre
 *                keeps its gaze committed that way instead of drifting back to middle.
 *
 * When motion stops the lead term decays away and the eyes settle onto the bias.
 *
 * Servo units are tenths of a degree (yaw -1280..1280, pitch 30..870 — FINDINGS.md 4b).
 * Eye position is the normalised -100..100 the `Feature` contract expects, so this drives
 * any skin — Default, VOLT or MAX — without modification.
 */
class GazeModifier : public Modifier {
public:
    /**
     * @param leadGain          how strongly velocity throws the gaze ahead (per deg/s)
     * @param biasGain          how strongly a parked head angle pulls the gaze (0..1)
     * @param decayPermil       per-update decay of the lead term, in 1/1000ths
     * @param updateIntervalMs  sampling period
     */
    GazeModifier(float leadGain = 0.22f, float biasGain = 0.55f, int decayPermil = 780,
                 uint32_t updateIntervalMs = 40)
        : _lead_gain(leadGain), _bias_gain(biasGain), _decay_permil(decayPermil),
          _update_interval_ms(updateIntervalMs)
    {
    }

    void _update(Modifiable& stackchan) override
    {
        if (!stackchan.hasAvatar()) {
            return;
        }

        uint32_t now = GetHAL().millis();
        uint32_t dt  = now - _last_update_tick;
        if (dt < _update_interval_ms) {
            return;
        }
        _last_update_tick = now;
        if (dt == 0) {
            return;
        }

        auto& motion = stackchan.motion();
        int yaw      = motion.getCurrentYawAngle();
        int pitch    = motion.getCurrentPitchAngle();

        // First sample only establishes a baseline; velocity needs two.
        if (!_primed) {
            _prev_yaw   = yaw;
            _prev_pitch = pitch;
            _primed     = true;
            return;
        }

        // deg/s = (difference in tenths / 10) / (dt ms / 1000)
        float yaw_vel   = ((float)(yaw - _prev_yaw) / 10.0f) * 1000.0f / (float)dt;
        float pitch_vel = ((float)(pitch - _prev_pitch) / 10.0f) * 1000.0f / (float)dt;
        _prev_yaw       = yaw;
        _prev_pitch     = pitch;

        // Decay then re-inject: a sustained turn holds the lead, a stop releases it.
        _lead_x = _lead_x * (float)_decay_permil / 1000.0f + yaw_vel * _lead_gain;
        _lead_y = _lead_y * (float)_decay_permil / 1000.0f + pitch_vel * _lead_gain;

        // Bias toward where the head is actually pointing.
        float bias_x = ((float)yaw / 1280.0f) * 100.0f * _bias_gain;

        // Pitch: a larger angle means looking further up, so the eyes should rise, which is
        // negative Y in the Feature coordinate system.
        float pitch_mid  = (float)(_pitch_min + _pitch_max) / 2.0f;
        float pitch_half = (float)(_pitch_max - _pitch_min) / 2.0f;
        float bias_y     = -(((float)pitch - pitch_mid) / pitch_half) * 100.0f * _bias_gain;

        int gx = clamp_i((int)(bias_x + _lead_x), -100, 100);
        int gy = clamp_i((int)(bias_y + _lead_y), -100, 100);

        auto& avatar = stackchan.avatar();
        uitk::Vector2i target(gx, gy);
        avatar.leftEye().setPosition(target);
        avatar.rightEye().setPosition(target);
    }

private:
    static int clamp_i(int v, int lo, int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    float _lead_gain;
    float _bias_gain;
    int _decay_permil;
    uint32_t _update_interval_ms;

    // Mirrors hal_servo.cpp pitch_servo_config.angleLimit
    static const int _pitch_min = 30;
    static const int _pitch_max = 870;

    bool _primed          = false;
    int _prev_yaw         = 0;
    int _prev_pitch       = 0;
    float _lead_x         = 0.0f;
    float _lead_y         = 0.0f;
    uint32_t _last_update_tick = 0;
};

}  // namespace stackchan
