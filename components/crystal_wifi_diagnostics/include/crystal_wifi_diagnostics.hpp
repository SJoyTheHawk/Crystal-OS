/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>

// Session-only capture, off at boot. Enabling records a local snapshot; no
// recurring network probes. Driver/DNS/TLS work always runs on a worker.
bool crystal_wifi_diagnostics_enabled();
bool crystal_wifi_diagnostics_set_enabled(bool enabled);
enum class CrystalWifiDiagnostic { Snapshot, NetworkChecks };
bool crystal_wifi_diagnostics_request(CrystalWifiDiagnostic operation);
void crystal_wifi_diagnostics_clear();
// Copies the newest complete lines into a bounded caller-owned buffer.
// revision lets the UI avoid replacing an unchanged label.
size_t crystal_wifi_diagnostics_copy_logs(char *out, size_t capacity, uint32_t *revision);
// Optional physical UART command interface; not needed by Developer Tools.
void crystal_wifi_diagnostics_start_uart();
