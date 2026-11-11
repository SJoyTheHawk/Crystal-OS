/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <vector>

struct CrystalPackageManifest {
    std::string id, name, version, min_os, runtime, entry, icon, bar_style;
    uint32_t memory_kb = 0, storage_kb = 0, worker_threads = 0;
    bool notify = false, audio = false, system_timer = false, location = false;
    std::vector<std::string> network_hosts;
};
struct CrystalPackageRecord {
    CrystalPackageManifest manifest;
    uint16_t slot = 0;
    bool enabled = true;
    uint16_t error_count = 0;
    uint32_t installed_at = 0;
    std::string details;
};
struct CrystalPackageLimits {
    size_t memory_bytes = 0, storage_bytes = 0;
    unsigned workers = 4;
};
// Verifier consumes the original file (including end marker, excluding the
// 66-byte trailer). On firmware, a null verifier selects the configured
// crystal_signature trust store. Host builds without that component reject
// signed packages unless a test verifier is supplied.
using crystal_package_verify_t = bool (*)(FILE *, size_t, const uint8_t[64], uint8_t);

bool crystal_package_valid_id(const std::string &id);
bool crystal_package_parse_manifest(const std::string &json, CrystalPackageManifest *out, std::string *error);
bool crystal_packages_mount();
// LVGL/management task only. root contains app-ID directories. On storage or
// reconciliation failure, returns false without publishing an incomplete list.
bool crystal_packages_init(const char *root, CrystalPackageLimits limits, std::string *error);
const std::vector<CrystalPackageRecord> &crystal_packages();
bool crystal_package_install(FILE *stream, bool developer_mode, std::string *error,
                             crystal_package_verify_t verifier = nullptr);
bool crystal_package_set_enabled(const std::string &id, bool enabled);
bool crystal_package_set_slot(const std::string &id, uint16_t slot);
bool crystal_package_launch_result(const std::string &id, bool success);
// Caller stops and verifies resources before authorizing removal. Leaks disable
// the app and retain its files, state and registry entry for recovery.
bool crystal_package_remove(const std::string &id, bool cleanup_verified, std::string *error);
bool crystal_package_state_get(const std::string &id, const char *key, void *out, size_t *length);
bool crystal_package_state_set(const std::string &id, const char *key, const void *value, size_t length);
bool crystal_package_state_erase(const std::string &id, const char *key);
bool crystal_package_state_clear(const std::string &id);
