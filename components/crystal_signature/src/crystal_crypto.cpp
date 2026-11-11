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
    // Wi-Fi is not running during early boot. Temporarily supply hardware
    // entropy for libsodium's initialization, then release ADC/RF resources.
    bootloader_random_enable();
    const int result = sodium_init();
    bootloader_random_disable();
    s_ready = result >= 0;
    return s_ready;
}

bool crystal_crypto_ready(void)
{
    return s_ready;
}
