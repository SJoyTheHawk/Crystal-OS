#!/usr/bin/env python3
"""Capture production launcher, Quick Settings and app UI on a headless LVGL host.

Fixed sample time, weather, battery and connectivity; no device/network access.
Requires CMake, a C++17 compiler and the checked-out managed LVGL dependency.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]
CONF = '''#pragma once
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_USE_SNAPSHOT 1
#define LV_USE_LOG 0
#define LV_THEME_DEFAULT_DARK 0
'''

def png_from_ppm(source, destination):
    with source.open('rb') as stream:
        assert stream.readline() == b'P6\n'
        width, height = map(int, stream.readline().split())
        assert (width, height) == (480, 480)
        assert stream.readline() == b'255\n'
        pixels = stream.read()
        assert len(pixels) == width * height * 3
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    destination.write_bytes(b'\x89PNG\r\n\x1a\n' +
        chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
        chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'docs/images')
    args = parser.parse_args()
    output = args.output_dir.resolve()
    work = ROOT / '.cache/readme-captures'
    work.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    (work / 'lv_conf.h').write_text(CONF)
    brookesia = ROOT / 'components/esp-brookesia/src'
    capture = ROOT / 'sim/screenshots'
    sources = [ROOT / 'components/crystal_app/src/crystal_app.cpp', ROOT / 'sim/crystal_hal_mock.cpp',
               ROOT / 'components/crystal_package/src/crystal_resources.cpp',
               capture / 'package_stubs.cpp', capture / 'capture.cpp', capture / 'capture_shell.cpp', capture / 'services.cpp']
    for app in ('clock_app', 'weather_app', 'calculator_app'):
        sources.extend(sorted((ROOT / f'components/{app}/src').glob('*.cpp')))
        sources.extend(sorted((ROOT / f'components/{app}/src').glob('*.c')))
    include_dirs = [brookesia, capture / 'stubs', work,
                    ROOT / 'components/crystal_shell/src', ROOT / 'components/crystal_shell/include',
                    ROOT / 'components/manage_apps_page/include', ROOT / 'components/crystal_registry/include',
                    ROOT / 'components/crystal_update_view/include']
    include_dirs.extend(ROOT / f'components/{name}/include' for name in
                        ('crystal_package', 'crystal_app', 'crystal_core', 'crystal_hal', 'clock_app', 'weather_app', 'calculator_app'))
    quoted_sources = '\n'.join(f'"{path}"' for path in sources)
    quoted_includes = '\n'.join(f'"{path}"' for path in include_dirs)
    (work / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(readme_capture LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(LV_CONF_PATH "{work}/lv_conf.h" CACHE STRING "" FORCE)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
file(GLOB_RECURSE BROOKESIA_SOURCES
    "{brookesia}/core/*.cpp" "{brookesia}/core/*.c"
    "{brookesia}/widgets/*.cpp" "{brookesia}/systems/phone/*.cpp"
    "{brookesia}/assets/*.c")
add_executable(capture {quoted_sources} {chr(36)}{{BROOKESIA_SOURCES}})
target_include_directories(capture PRIVATE {quoted_includes})
target_compile_definitions(capture PRIVATE ESP_BROOKESIA_KCONFIG_IGNORE ESP_BROOKESIA_CONF_SKIP
    ESP_BROOKESIA_SQUARELINE_USE_INTERNAL_UI_HELPERS=1 ESP_BROOKESIA_SQ1_4_1_LV8_3_11)
target_compile_options(capture PRIVATE -ffunction-sections -fdata-sections)
target_link_options(capture PRIVATE -Wl,-dead_strip -Wl,-undefined,dynamic_lookup)
set_source_files_properties(
    "{ROOT}/components/clock_app/src/clock_app.cpp"
    "{ROOT}/components/weather_app/src/weather_app.cpp"
    PROPERTIES COMPILE_DEFINITIONS "time=crystal_capture_time")
target_link_libraries(capture PRIVATE lvgl)
''')
    subprocess.run(['cmake', '-S', str(work), '-B', str(work / 'build')], check=True)
    subprocess.run(['cmake', '--build', str(work / 'build'), '--target', 'capture', '-j', '8'], check=True)
    subprocess.run([str(work / 'build/capture')], check=True, cwd=work)
    for name in ('clock', 'weather', 'calculator', 'launcher', 'quick_settings'):
        png_from_ppm(work / f'{name}.ppm', output / f'{name}.png')
    print(f'Captured screenshots: {output}')

if __name__ == '__main__':
    main()
