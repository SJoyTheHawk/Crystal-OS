/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include <stdbool.h>

#include <stddef.h>

struct CrystalControlAction;

// The API is available only for the current boot session. Enabling it starts a
// LAN HTTP server and creates a one-use, ten-minute pairing PIN. The PIN must
// be displayed by the caller on the device. A paired token expires after 24h.
bool crystal_control_enable(char pairing_pin[7]);
void crystal_control_disable(void);
bool crystal_control_enabled(void);

// Start the physical USB developer transport. It accepts a framed package
// upload on USB Serial/JTAG and installs it in explicit developer mode.
bool crystal_control_usb_start(void);

// Append a bounded developer log line for the authenticated SSE endpoint.
// This is intentionally a small in-memory ring and is not persistent.
#ifdef __cplusplus
extern "C" {
#endif
void crystal_control_log_line(const char *app_id, const char *line);
#ifdef __cplusplus
}
#endif
