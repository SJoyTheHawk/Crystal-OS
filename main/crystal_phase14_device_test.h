/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>

class ESP_Brookesia_Phone;
enum class CrystalPhase14TestStatus { Idle, Running, AwaitingReboot, Passed, Failed };

// Development builds only. Call with the display lock held after shell startup.
// The embedded fixture must use org.crystal.phase14.calculator. All test work
// runs in short LVGL timer steps; restart is requested once, after committing a
// checkpoint. An existing unrelated installation of that ID is never replaced.
bool crystal_phase14_device_test_start(ESP_Brookesia_Phone *phone,
                                      const uint8_t *package, size_t package_size,
                                      const char *apps_root, void (*restart)(),
                                      uint32_t run_id = 1);
CrystalPhase14TestStatus crystal_phase14_device_test_status();
