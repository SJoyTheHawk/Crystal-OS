/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
// Host fixtures for tests/captures that do not install package apps. The Phase 14
// integration suite links the production package store instead of these stubs.
#include "crystal_package.hpp"
const std::vector<CrystalPackageRecord> &crystal_packages() {
    static const std::vector<CrystalPackageRecord> empty;
    return empty;
}
bool crystal_package_valid_id(const std::string &) { return false; }
bool crystal_package_set_enabled(const std::string &, bool) { return false; }
bool crystal_package_set_slot(const std::string &, uint16_t) { return false; }
bool crystal_package_launch_result(const std::string &, bool) { return false; }
bool crystal_package_remove(const std::string &, bool, std::string *) { return false; }
bool crystal_package_state_get(const std::string &, const char *, void *, size_t *) { return false; }
bool crystal_package_state_set(const std::string &, const char *, const void *, size_t) { return false; }
bool crystal_package_state_erase(const std::string &, const char *) { return false; }
bool crystal_package_state_clear(const std::string &) { return false; }
