/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include <stdbool.h>

// The API is available only for the current boot session. Enabling it starts a
// LAN HTTP server and creates a one-use, ten-minute pairing PIN. The PIN must
// be displayed by the caller on the device. A paired token expires after 24h.
bool crystal_control_enable(char pairing_pin[7]);
void crystal_control_disable(void);
bool crystal_control_enabled(void);
