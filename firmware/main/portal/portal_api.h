/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 *
 * The configuration half of the portal's HTTP surface.
 *
 * portal.cpp owns the server, the control plane passthrough and firmware upload — the
 * things that must keep working even if everything else is broken. This file owns the
 * settings endpoints, which are chattier and change more often. Splitting them keeps the
 * recovery-critical code short enough to audit in one sitting.
 */
#pragma once

#include <esp_http_server.h>

namespace stackchan::portal {

/** @brief Register the settings endpoints on an already-started server. */
void register_api_handlers(httpd_handle_t server);

/** @brief How many URI handler slots register_api_handlers() needs. */
int api_handler_count();

}  // namespace stackchan::portal
