/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 *
 * StackyChan web portal.
 *
 * A small HTTP server on port 80 that gives the device a face on the network:
 *
 *   GET  /              control page (firmware info, OTA upload, live controls)
 *   GET  /api/info      JSON: version, uptime, running slot, IP, free heap
 *   POST /api/ota       raw firmware .bin body -> written to the inactive OTA slot
 *   POST /api/avatar    JSON -> StackChan::updateAvatarFromJson()
 *   POST /api/motion    JSON -> StackChan::updateMotionFromJson()
 *   POST /api/rgb       JSON -> StackChan::updateNeonLightFromJson()
 *
 * The three control endpoints are deliberately thin: the firmware already has a JSON
 * control plane (main/stackchan/json/json_helper.cpp) that the Dance app drives over BLE.
 * This is the same three calls with an HTTP source, so the schema is identical and the
 * browser keyframe editor in workshop/portal speaks it natively.
 *
 * OTA safety: uploads go to the *inactive* slot, never the running one. With
 * CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE (already on) the new image boots on trial and the
 * bootloader reverts automatically if it fails to stay up — see hal.cpp's
 * _confirm_ota_image_if_stable(). So a bad upload costs a reboot, not a cable.
 */
#pragma once

namespace stackchan::portal {

/**
 * @brief Spawn the portal task.
 *
 * Returns immediately. The task waits for the network to come up, then starts the HTTP
 * server. Safe to call before WiFi is connected, and safe to call in both boot modes
 * (mooncake launcher or straight-to-AI-agent).
 */
void start();

/** @brief True once the HTTP server is listening. */
bool is_running();

}  // namespace stackchan::portal
