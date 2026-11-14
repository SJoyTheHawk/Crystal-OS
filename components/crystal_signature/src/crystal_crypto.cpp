/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_crypto.hpp"

#include "bootloader_random.h"
#include "esp_random.h"
#include "sodium.h"

namespace {
bool s_ready = false;
const char *implementation_name() { return "crystal-esp32"; }
const randombytes_implementation kRandom = {
    implementation_name, esp_random, nullptr, nullptr, esp_fill_random, nullptr
};
}

bool crystal_crypto_init(void)
{
    if (s_ready) return true;
    // The bundled libsodium defaults to a Unix random device, which ESP-IDF
    // does not provide. Register before sodium_init (which requests randomness).
    if (randombytes_set_implementation(&kRandom) != 0) return false;
    // Supply local hardware entropy before libsodium requests randomness.
    // S3 releases ADC/RF resources after initialization; P4 retains its source.
    bootloader_random_enable();
    const int result = sodium_init();
#if !CONFIG_IDF_TARGET_ESP32P4
    bootloader_random_disable();
#endif
    // P4 has no local Wi-Fi RF entropy: the C6 radio cannot feed its RNG.
    // Keep the SDK's ADC entropy source enabled for TLS/session randomness,
    // including Ethernet-only operation. ADC1 is reserved for this purpose.
    s_ready = result >= 0;
    return s_ready;
}

bool crystal_crypto_ready(void)
{
    return s_ready;
}
