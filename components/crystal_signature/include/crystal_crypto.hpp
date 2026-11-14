/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

// Call once from app_main before starting RF, ADC, or application tasks.
// Installs the ESP32 entropy provider and initializes libsodium. On P4, reserves
// ADC1 for continuous local entropy; the external C6 does not feed the P4 RNG.
bool crystal_crypto_init(void);
bool crystal_crypto_ready(void);
