/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Trust configuration is an administrative operation. Key ID zero is reserved
// for unsigned development packages and cannot be added to the trust store.
// Keys persist in the device settings NVS namespace.
// Loads the optional public key configured through menuconfig.
bool crystal_signature_init(void);
bool crystal_signature_trust_key(uint8_t key_id, const uint8_t public_key[32]);
bool crystal_signature_remove_trusted_key(uint8_t key_id);
bool crystal_signature_has_trusted_key(uint8_t key_id);

// Ed25519 verifier matching crystal_package_verify_t. The signed prefix is
// read from the original seekable package stream; the caller's position is
// restored whether verification succeeds or fails.
bool crystal_signature_verify_package(FILE *stream, size_t signed_length,
                                      const uint8_t signature[64], uint8_t key_id);
