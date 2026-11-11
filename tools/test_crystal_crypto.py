#!/usr/bin/env python3
"""Test the production entropy adapter against the bundled libsodium on host.

Hardware RNG functions are deterministic test doubles; this checks provider
selection, initialization ordering, and Ed25519 compatibility, not RNG quality.
"""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SODIUM = ROOT / 'managed_components/esphome__libsodium'

HARNESS = r'''
#include <cassert>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include "sodium.h"
#include "crystal_crypto.hpp"
static bool entropy;
static unsigned enables, disables, words;
void bootloader_random_enable() { assert(!entropy); entropy = true; ++enables; }
void bootloader_random_disable() { assert(entropy); entropy = false; ++disables; }
uint32_t esp_random() { assert(entropy); return ++words * 2654435761U; }
void esp_fill_random(void *buf, size_t size) {
    auto *p = static_cast<unsigned char *>(buf);
    while (size--) *p++ = static_cast<unsigned char>(esp_random());
}
int main() {
    assert(!crystal_crypto_ready());
    assert(crystal_crypto_init());
    assert(crystal_crypto_ready());
    assert(enables == 1 && disables == 1 && !entropy && words > 0);
    assert(std::strcmp(randombytes_implementation_name(), "crystal-esp32") == 0);
    assert(crystal_crypto_init());
    assert(enables == 1 && disables == 1);
    // Simulate active Wi-Fi entropy for the pairing PIN and session token.
    entropy = true;
    unsigned char token[32];
    const unsigned before = words;
    randombytes_buf(token, sizeof(token));
    assert(words > before);
    assert(randombytes_uniform(1000000) < 1000000);
    entropy = false;
    // RFC 8032 test vector 1: verification also works offline.
    unsigned char key[32], sig[64];
    assert(sodium_hex2bin(key, sizeof(key),
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
        64, nullptr, nullptr, nullptr) == 0);
    assert(sodium_hex2bin(sig, sizeof(sig),
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555f"
        "b8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
        128, nullptr, nullptr, nullptr) == 0);
    assert(crypto_sign_ed25519_verify_detached(sig, reinterpret_cast<const unsigned char *>(""), 0, key) == 0);
    sig[0] ^= 1;
    assert(crypto_sign_ed25519_verify_detached(sig, reinterpret_cast<const unsigned char *>(""), 0, key) != 0);
    std::puts("PASS: ESP32 provider, boot entropy lifecycle, idempotence, pairing randomness, offline Ed25519 and tamper rejection");
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='crystal-crypto-') as directory:
        work = pathlib.Path(directory)
        (work / 'esp_random.h').write_text(
            '#pragma once\n#include <stddef.h>\n#include <stdint.h>\n'
            'uint32_t esp_random();\nvoid esp_fill_random(void *, size_t);\n')
        (work / 'bootloader_random.h').write_text(
            '#pragma once\nvoid bootloader_random_enable();\nvoid bootloader_random_disable();\n')
        (work / 'check.cpp').write_text(HARNESS)
        (work / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.16)
project(crystal_crypto_check LANGUAGES C CXX ASM)
add_subdirectory("{SODIUM}" sodium)
add_executable(check check.cpp "{ROOT}/components/crystal_signature/src/crystal_crypto.cpp")
target_include_directories(check PRIVATE "{work}" "{ROOT}/components/crystal_signature/include")
target_link_libraries(check PRIVATE esphome::sodium)
''')
        subprocess.run(['cmake', '-S', str(work), '-B', str(work / 'build')], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(['cmake', '--build', str(work / 'build'), '-j', '4'], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([str(work / 'build/check')], check=True)

if __name__ == '__main__':
    main()
