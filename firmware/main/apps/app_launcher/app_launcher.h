/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "view/view.h"
#include <apps/app_setup/workers/workers.h>
#include <mooncake.h>
#include <mooncake_templates.h>
#include <portal/portal_config.h>
#include <cstdint>
#include <memory>

class AppLauncher : public mooncake::templates::AppLauncherBase {
public:
    void onLauncherCreate() override;
    void onLauncherOpen() override;
    void onLauncherRunning() override;
    void onLauncherClose() override;
    void onLauncherDestroy() override;

private:
    std::unique_ptr<view::LauncherView> _view;
    std::unique_ptr<view::Screensaver> _screensaver;
    std::unique_ptr<setup_workers::StartupWorker> _startup_worker;
    uint32_t _screensaver_timecount = 0;
    bool _startup_checked           = false;

    // Cached rather than read from NVS on every tick — screensaver_update() runs on
    // every launcher frame, and a config a portal edit can change takes effect within a
    // second either way, so re-reading it that often bought nothing.
    stackchan::portal::config::ScreensaverConfig _screensaver_cfg;
    uint32_t _screensaver_cfg_timecount = 0;

    void create_launcher_view();
    void screensaver_update();
};
