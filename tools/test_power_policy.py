#!/usr/bin/env python3
"""Host checks of dim profiles and brightness without CPU frequency changes."""
from pathlib import Path
import subprocess
import tempfile
from test_backend_idle import function

ROOT = Path(__file__).resolve().parents[1]
STUBS = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <map>
#include <string>
#include "crystal_brightness_policy.hpp"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
constexpr uint8_t kFullBrightness = 100;
std::atomic<int32_t> s_cache_dim_seconds{-1}, s_cache_saving_dim_seconds{-1}, s_cache_off_seconds{-1};
std::map<std::string, int> storage;
bool saving = false;
uint8_t preferred = 100;
template<class T> T cached_value(const char *key, T fallback, std::atomic<int32_t> &cache) {
    if (cache < 0) cache = storage.count(key) ? storage[key] : fallback;
    return static_cast<T>(cache.load());
}
template<class T> void store_cached(const char *key, T value, std::atomic<int32_t> &cache) { storage[key] = value; cache = value; }
bool energy_saving_enabled() { return saving; }
bool crystal_power_saving_enabled() { return saving; }
template<class T> void store_value(const char *, T value) { preferred = value; }
uint8_t saved_brightness() { return preferred; }
uint16_t crystal_power_off_seconds();
struct Wifi { void set_power_save(bool) {} } wifi;
uint8_t brightness = 100;
struct Brightness { void set(uint8_t value) { brightness = value; } } brightness_device;
struct Hal { Wifi *wifi; Brightness *brightness; } services{&wifi, &brightness_device};
Hal &hal() { return services; }
void ramp_brightness(uint8_t value) { brightness = value; }
'''
CHECKS = r'''
int main() {
    storage["power.dim_s"] = 30; storage["power.off_s"] = 60;
    assert(crystal_power_dim_seconds() == 120);
    assert(crystal_power_off_seconds() == 300);
    saving = true;
    assert(crystal_power_dim_seconds() == 30);
    assert(crystal_power_off_seconds() == 60);
    crystal_power_set_off_seconds(0); assert(crystal_power_off_seconds() == 0);
    crystal_power_set_dim_seconds(60); assert(crystal_power_dim_seconds() == 60);
    saving = false; assert(crystal_power_dim_seconds() == 120);
    saving = true; crystal_power_set_dim_seconds(30);
    crystal_power_set_dim_seconds(17); assert(crystal_power_dim_seconds() == 30);
    power_saving_apply(true); assert(brightness == 60);
    preferred = 30; power_saving_apply(true); assert(brightness == 18 && preferred == 30);
    preferred = 40; power_saving_apply(true); assert(brightness == 24);
    preferred = 90; power_saving_apply(true); assert(brightness == 54 && preferred == 90);
    power_saving_apply(true); assert(brightness == 54); // Never scale an already-scaled value.
    saving = false; power_saving_apply(false);
    assert(brightness == 90);
    saving = true; crystal_brightness_set(30); assert(brightness == 18 && preferred == 30);
    crystal_brightness_set(100); assert(brightness == 60 && preferred == 100);
    crystal_brightness_set(1); assert(brightness == 1 && preferred == 1);
    crystal_brightness_set(0); assert(brightness == 0);
    saving = false; crystal_brightness_set(30); assert(brightness == 30);
    for (unsigned value = 0; value <= 100; ++value) {
        assert(crystal_effective_brightness(value, false) == value);
        assert(crystal_effective_brightness(value, true) == (value * 3 + 2) / 5);
    }
}
'''

def main():
    source = (ROOT / 'components/crystal_core/src/crystal_core.cpp').read_text()
    signatures = ['bool one_of(', 'void power_saving_apply(', 'uint16_t crystal_power_dim_seconds(',
                  'uint16_t crystal_power_off_seconds(', 'void crystal_power_set_dim_seconds(',
                  'void crystal_power_set_off_seconds(', 'void crystal_brightness_set(']
    boot = (ROOT / 'main/main.cpp').read_text()
    assert 'crystal_power_prepare_display' not in boot
    assert 'esp_pm_configure' not in source
    shell = (ROOT / 'components/crystal_shell/src/crystal_shell.cpp').read_text()
    assert 'Apply CPU Speed' not in shell and 'Restart to apply CPU speed' not in shell
    assert 'dim_ms /= 2' not in function(source, 'void check_power_state(')
    assert 'ramp_brightness(crystal_effective_brightness(saved, energy_saving_enabled()))' in source
    hal = (ROOT / 'components/crystal_hal/src/crystal_hal.cpp').read_text()
    assert 'restore_cached(crystal_effective_brightness(s_brightness.get(), true))' in hal
    with tempfile.TemporaryDirectory(prefix='crystal-power-') as directory:
        path = Path(directory)
        (path / 'check.cpp').write_text(STUBS + chr(10).join(function(source, s) for s in signatures) + CHECKS)
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-DCONFIG_IDF_TARGET_ESP32P4=1', '-fsanitize=address,undefined',
                        '-I', str(ROOT / 'components/crystal_hal/include'),
                        str(path / 'check.cpp'), '-o', str(path / 'check')], check=True)
        subprocess.run([str(path / 'check')], check=True)
    print('PASS: 30s/120s profiles, 60% brightness scaling, slider/persistence/restoration, boot/wake paths, no CPU changes')

if __name__ == '__main__':
    main()
