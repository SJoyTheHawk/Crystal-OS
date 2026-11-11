/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_signature.hpp"
#include "crystal_crypto.hpp"

#include <array>
#include <cstdio>
#include <cstring>

#include "crystal_hal.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sodium.h"

static const char *TAG = "crystal_signature";

namespace {
constexpr uint8_t kFirstTrustedKey = 1;
constexpr size_t kPublicKeySize = crypto_sign_PUBLICKEYBYTES;

bool storage_key(uint8_t key_id, char out[8])
{
    return key_id >= kFirstTrustedKey && std::snprintf(out, 8, "sig%03u", key_id) > 0;
}

bool read_key(uint8_t key_id, uint8_t public_key[kPublicKeySize])
{
    char name[8];
    if (!storage_key(key_id, name) || hal().storage == nullptr) return false;
    size_t length = kPublicKeySize;
    return hal().storage->get(name, public_key, &length) && length == kPublicKeySize;
}

int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}

bool crystal_signature_init(void)
{
    static_assert(CONFIG_CRYSTAL_PACKAGE_TRUSTED_KEY_ID >= 1 &&
                  CONFIG_CRYSTAL_PACKAGE_TRUSTED_KEY_ID <= 255,
                  "trusted package key ID must fit the package trailer");
    const char *hex = CONFIG_CRYSTAL_PACKAGE_TRUSTED_PUBLIC_KEY_HEX;
    if (hex[0] == '\0') {
        ESP_LOGW(TAG, "No initial package signing key configured; signed packages will be rejected");
        return true;
    }
    if (std::strlen(hex) != kPublicKeySize * 2) {
        ESP_LOGE(TAG, "Initial package signing public key must contain 64 hex characters");
        return false;
    }
    uint8_t public_key[kPublicKeySize] = {};
    for (size_t i = 0; i < kPublicKeySize; ++i) {
        const int high = hex_digit(hex[2 * i]);
        const int low = hex_digit(hex[2 * i + 1]);
        if (high < 0 || low < 0) {
            sodium_memzero(public_key, sizeof(public_key));
            ESP_LOGE(TAG, "Initial package signing public key contains non-hex characters");
            return false;
        }
        public_key[i] = static_cast<uint8_t>((high << 4) | low);
    }
    const bool ok = crystal_signature_trust_key(
        static_cast<uint8_t>(CONFIG_CRYSTAL_PACKAGE_TRUSTED_KEY_ID), public_key);
    sodium_memzero(public_key, sizeof(public_key));
    if (ok) ESP_LOGI(TAG, "Loaded configured package signing key ID %d",
                     CONFIG_CRYSTAL_PACKAGE_TRUSTED_KEY_ID);
    else ESP_LOGE(TAG, "Could not persist configured package signing key");
    return ok;
}

bool crystal_signature_trust_key(uint8_t key_id, const uint8_t public_key[32])
{
    char name[8];
    return public_key != nullptr && storage_key(key_id, name) && hal().storage != nullptr &&
           hal().storage->set(name, public_key, kPublicKeySize);
}

bool crystal_signature_remove_trusted_key(uint8_t key_id)
{
    char name[8];
    return storage_key(key_id, name) && hal().storage != nullptr && hal().storage->erase(name);
}

bool crystal_signature_has_trusted_key(uint8_t key_id)
{
    uint8_t public_key[kPublicKeySize] = {};
    return read_key(key_id, public_key);
}

bool crystal_signature_verify_package(FILE *stream, size_t signed_length,
                                      const uint8_t signature[64], uint8_t key_id)
{
    if (stream == nullptr || signature == nullptr || signed_length == 0) return false;
    uint8_t public_key[kPublicKeySize] = {};
    if (!crystal_crypto_ready() || !read_key(key_id, public_key)) return false;

    const long original_position = std::ftell(stream);
    if (original_position < 0 || std::fseek(stream, 0, SEEK_SET) != 0) return false;

    // libsodium's pure Ed25519 API takes a contiguous message. Prefer PSRAM so
    // verification does not consume the smaller internal heap; fail closed if
    // this package cannot fit, including when the optional internal fallback
    // is exhausted.
    uint8_t *message = static_cast<uint8_t *>(
        heap_caps_malloc(signed_length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (message == nullptr) message = static_cast<uint8_t *>(
        heap_caps_malloc(signed_length, MALLOC_CAP_8BIT));
    bool valid = false;
    if (message != nullptr) {
        const bool complete = std::fread(message, 1, signed_length, stream) == signed_length &&
                              !std::ferror(stream);
        valid = complete && crypto_sign_ed25519_verify_detached(signature, message,
                    static_cast<unsigned long long>(signed_length), public_key) == 0;
        sodium_memzero(message, signed_length);
        heap_caps_free(message);
    }
    const bool restored = std::fseek(stream, original_position, SEEK_SET) == 0;
    sodium_memzero(public_key, sizeof(public_key));
    return valid && restored;
}
