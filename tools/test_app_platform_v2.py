#!/usr/bin/env python3
"""Run Phase 14 with production package/registry/Calculator/LVGL under ASan/UBSan."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib
import shutil
import crystal
import prepare_phase14_device_test

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
#define LV_USE_LOG 0
'''


def fixture(manifest, entries):
    text = json.dumps(manifest, separators=(',', ':')).encode()
    value = bytearray(b'CAPP' + struct.pack('>HI', 1, len(text)) + text)
    for path, data in entries:
        name = path.encode()
        value += struct.pack('>H', len(name)) + name + struct.pack('>II', len(data), zlib.crc32(data)) + data
    return value + bytes(68)


def main():
    with tempfile.TemporaryDirectory(prefix='crystal-phase14-') as directory:
        temp = Path(directory)
        package = ROOT / 'examples/calculator_package'
        m = crystal.load_manifest(package / 'app.yaml')
        crystal.pack(package, temp / 'calculator.capp')
        crystal.pack(ROOT / 'examples/declarative_dashboard', temp / 'declarative.capp')
        for example in ('clock', 'weather', 'calculator'):
            visual_package = temp / f'declarative-{example}-source'
            shutil.copytree(ROOT / 'examples/declarative_dashboard', visual_package)
            visual_manifest = json.loads((visual_package / 'app.yaml').read_text())
            visual_manifest['id'] = f'org.crystal.visual-{example}'
            visual_manifest['name'] = f'{example.title()} Visual Reference'
            visual_manifest['entry'] = f'visual/{example}.json'
            visual_manifest['storage_kb'] = 64
            (visual_package / 'app.yaml').write_text(json.dumps(visual_manifest))
            crystal.pack(visual_package, temp / f'declarative-{example}.capp')
        denied_package = temp / 'declarative-denied-source'
        shutil.copytree(ROOT / 'examples/declarative_dashboard', denied_package)
        denied_manifest = json.loads((denied_package / 'app.yaml').read_text())
        denied_manifest['id'] = 'org.crystal.declarative-denied'
        denied_manifest['name'] = 'Denied Dashboard'
        denied_manifest['permissions'] = {}
        (denied_package / 'app.yaml').write_text(json.dumps(denied_manifest))
        crystal.pack(denied_package, temp / 'declarative-denied.capp')
        prepare_phase14_device_test.prepare(temp / 'device-calculator.capp')
        assert crystal.inspect(temp / 'calculator.capp')['manifest']['id'] == m['id']
        data = (temp / 'calculator.capp').read_bytes()
        crc = bytearray(data); crc[-69] ^= 1
        (temp / 'crc.capp').write_bytes(crc)
        (temp / 'truncated.capp').write_bytes(data[:-10])
        (temp / 'trailing.capp').write_bytes(data + b'x')
        signature = bytearray(data); signature[-66] = 1
        (temp / 'signature.capp').write_bytes(signature)
        entries = [('calculator.native', b'crystal-native:calculator-v1\n')]
        for name, manifest in [
            ('sibling', dict(m, id='org.crystal.sentinel')),
            ('large', dict(m, id='org.crystal.large', memory_kb=900)),
            ('worker', dict(m, id='org.crystal.worker', worker_threads=1)),
            ('bus-leak', dict(m, worker_threads=1)),
        ]:
            (temp / f'{name}.capp').write_bytes(fixture(manifest, entries))
        for name, manifest, files in [
            ('traversal', m, entries + [('../outside', b'bad')]),
            ('duplicate', m, entries * 2), ('reserved', m, entries + [('data/state.json', b'bad')]),
            ('abi', dict(m, abi=2), entries),
            ('network', dict(m, permissions={'network_hosts': ['example.com']}), entries),
            ('missing', dict(m, entry='missing.native'), entries),
            ('depth', dict(m, description=[[[[[[[[[[[[[[[[[0]]]]]]]]]]]]]]]]]), entries),
        ]:
            (temp / f'{name}.capp').write_bytes(fixture(manifest, files))
        (temp / 'app.json').write_text(json.dumps(m))
        # CLI rejection checks use the actual packer, including source symlinks.
        folder = temp / 'source'; folder.mkdir()
        (folder / 'app.yaml').write_text(json.dumps(m))
        (folder / 'calculator.native').symlink_to(package / 'calculator.native')
        try:
            crystal.pack(folder, temp / 'bad.capp')
            raise AssertionError('Packer accepted a symlink')
        except ValueError:
            pass
        for payload in (data[:-1], bytes(crc), data + b'extra'):
            (temp / 'bad.capp').write_bytes(payload)
            try:
                crystal.inspect(temp / 'bad.capp')
                raise AssertionError('Inspector accepted corrupt package')
            except ValueError:
                pass
        work = ROOT / '.cache/phase14-host'
        work.mkdir(parents=True, exist_ok=True)
        (work / 'lv_conf.h').write_text(CONF)
        cjson = ROOT / 'managed_components/espressif__cjson/cJSON'
        if not (cjson / 'cJSON.c').exists():
            raise SystemExit('Resolve ESP-IDF dependencies first: idf.py reconfigure')
        brookesia = ROOT / 'components/esp-brookesia/src'
        sources = [cjson / 'cJSON.c']
        for component in ('crystal_package', 'crystal_app', 'crystal_registry', 'calculator_app', 'crystal_declarative'):
            sources.extend(sorted((ROOT / f'components/{component}/src').glob('*.cpp')))
            sources.extend(sorted((ROOT / f'components/{component}/src').glob('*.c')))
        include_dirs = [brookesia, ROOT / 'sim/screenshots/stubs', work, cjson, ROOT / 'main']
        include_dirs.extend(ROOT / f'components/{component}/include' for component in
                            ('crystal_package', 'crystal_app', 'crystal_hal', 'crystal_registry', 'calculator_app',
                             'crystal_declarative', 'crystal_core'))
        quoted_sources = '\n'.join(f'"{path}"' for path in sources)
        quoted_includes = '\n'.join(f'"{path}"' for path in include_dirs)
        (work / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(phase14_host LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
add_link_options(-fsanitize=address,undefined)
set(LV_CONF_PATH "{work}/lv_conf.h" CACHE STRING "" FORCE)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
file(GLOB_RECURSE BROOKESIA_SOURCES
    "{brookesia}/core/*.cpp" "{brookesia}/core/*.c"
    "{brookesia}/widgets/*.cpp" "{brookesia}/systems/phone/*.cpp"
    "{brookesia}/assets/*.c")
add_library(platform STATIC {quoted_sources} ${{BROOKESIA_SOURCES}})
target_include_directories(platform PUBLIC {quoted_includes})
target_compile_definitions(platform PUBLIC ESP_BROOKESIA_KCONFIG_IGNORE ESP_BROOKESIA_CONF_SKIP
    ESP_BROOKESIA_SQUARELINE_USE_INTERNAL_UI_HELPERS=1 ESP_BROOKESIA_SQ1_4_1_LV8_3_11)
target_link_libraries(platform PUBLIC lvgl)
add_executable(check "{ROOT}/tools/phase14_check.cpp" "{ROOT}/components/crystal_shell/src/crystal_keyboard.cpp")
target_include_directories(check PRIVATE "{ROOT}/components/crystal_shell/include")
target_link_libraries(check PRIVATE platform)
add_executable(device_check "{ROOT}/tools/phase14_device_check.cpp"
    "{ROOT}/main/crystal_phase14_device_test.cpp")
target_link_libraries(device_check PRIVATE platform)
''')
        subprocess.run(['cmake', '-S', str(work), '-B', str(work / 'build')], check=True)
        subprocess.run(['cmake', '--build', str(work / 'build'), '-j', '8'], check=True)
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
        subprocess.run([str(work / 'build/check'), str(temp), str(temp / 'apps')], env=env, check=True)
        subprocess.run([str(work / 'build/device_check'), str(temp / 'device-calculator.capp'),
                        str(temp / 'device-apps')], env=env, check=True)


if __name__ == '__main__':
    main()
