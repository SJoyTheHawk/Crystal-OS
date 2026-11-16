#!/usr/bin/env python3
"""Run production Weather UI, request session and location service through Brookesia."""
import pathlib
import subprocess
import sys
from test_weather_requests import CHECK
ROOT=pathlib.Path(__file__).resolve().parents[1]
SCENARIOS=r'''
    assert(jobs.empty()); // Registration and another restored app are network-free.
    route=CrystalNetworkType::None; ++route_revision;
    start(weather); assert(jobs.empty()); // Open offline.
    route=CrystalNetworkType::WiFi; ++route_revision;
    weather.refresh(); assert(jobs.size()==1 && jobs.back().url.find("ipwho.is")!=std::string::npos);
    weather.refresh(); assert(jobs.size()==1);
    finish(jobs.back(),R"({"success":true,"city":"Hong Kong","latitude":22.3193,"longitude":114.1694})");
    weather.refresh(); assert(jobs.size()==2);
    finish(jobs.back(),forecast); weather.refresh();
    assert(has_label(lv_scr_act(),"24.6"));
    auto count=jobs.size();
    weather.refresh(); assert(jobs.size()==count);
    lv_tick_inc(1800000); weather.refresh(); assert(jobs.size()==count+1);
    const Job late=jobs.back();
    assert(phone.sendNavigateEvent(ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME)); settle();
    assert(weather.resources().report().timers==0);
    finish(late,forecast); ++route_revision;
    lv_tick_inc(1800000); weather.refresh(); assert(jobs.size()==count+1);
    start(weather); assert(jobs.size()==count+2);
    finish(jobs.back(),forecast); weather.refresh();
    count=jobs.size();
    weather.requestManual(); assert(jobs.size()==count+1);
    weather.requestManual(); assert(jobs.size()==count+1);
    finish(jobs.back(),nullptr,503); weather.refresh();
    lv_tick_inc(59000); weather.refresh(); assert(jobs.size()==count+1);
    lv_tick_inc(1000); weather.refresh(); assert(jobs.size()==count+2);
    finish(jobs.back(),nullptr,503); weather.refresh();
    lv_tick_inc(179000); weather.refresh(); assert(jobs.size()==count+2);
    lv_tick_inc(1000); weather.refresh(); assert(jobs.size()==count+3);
    const Job switched=jobs.back();
    route=CrystalNetworkType::Ethernet; ++route_revision;
    weather.refresh(); assert(jobs.size()==count+4);
    finish(switched,forecast); weather.refresh(); assert(jobs.size()==count+4);
    finish(jobs.back(),forecast); weather.refresh();
    start(calculator); assert(weather.resources().report().clean());
    count=jobs.size();
    assert(crystal_weather_set_location(25,121,"Taipei"));
    ++route_revision; lv_tick_inc(3600000); weather.refresh();
    assert(jobs.size()==count);
    start(weather); assert(jobs.size()==count+1 && jobs.back().url.find("25.0000")!=std::string::npos);
    const Job old_settings=jobs.back();
    assert(crystal_weather_set_location(35,139,"Tokyo"));
    weather.refresh(); assert(jobs.size()==count+2);
    finish(old_settings,forecast); weather.refresh();
    finish(jobs.back(),forecast); weather.refresh();
    assert(has_label(lv_scr_act(),"Tokyo"));
    weather.requestManual(); const Job closed=jobs.back();
    start(calculator); assert(weather.resources().report().clean());
    assert(weather.clearData());
    finish(closed,forecast);
    int16_t value; size_t bytes=sizeof(value);
    assert(!weather.state().get("tc10",&value,&bytes));
    count=jobs.size(); ++route_revision; weather.refresh(); assert(jobs.size()==count);
    start(weather); assert(jobs.size()==count+1);
    finish(jobs.back(),forecast); weather.refresh();
    assert(phone.sendNavigateEvent(ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME)); settle();
    count=jobs.size(); start(weather); assert(jobs.size()==count); // Fresh resume.
    start(calculator); assert(weather.resources().report().clean() && bodies==0);
    puts("PASS: restored app isolation, offline/reconnect, 30-minute refresh, retries, pause/resume, close, settings and clear-data isolation");
'''
def main():
    # Reuse the production LVGL/Brookesia harness and its maintained dependency list.
    subprocess.run([sys.executable,str(ROOT/'tools/capture_readme.py'),'--output-dir','/tmp/crystal-weather-test-captures'],check=True,stdout=subprocess.DEVNULL)
    work=ROOT/'.cache/weather-lifecycle'; work.mkdir(parents=True,exist_ok=True)
    (work/'nvs.h').write_text(r'''#pragma once
#include "crystal_hal.hpp"
#include <cstring>
using esp_err_t=int;
using nvs_handle_t=unsigned;
using nvs_iterator_t=void *;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NVS_NOT_FOUND=1;
constexpr int NVS_READWRITE=1,NVS_TYPE_ANY=255;
struct nvs_entry_info_t {char key[16];};
inline const char *esp_err_to_name(int) {return "fixture";}
inline const char *keys[]={"aeab633.tc10","aeab633.humid","aeab633.wmo","aeab633.wind","aeab633.fetched","aeab633.city","aeab633.lat","aeab633.lon","aeab633.auto"};
inline int nvs_open(const char *,int,nvs_handle_t *h) {*h=1;return ESP_OK;}
inline void nvs_close(nvs_handle_t) {}
inline int nvs_entry_find_in_handle(nvs_handle_t,int,nvs_iterator_t *i) {*i=reinterpret_cast<void *>(1);return ESP_OK;}
inline int nvs_entry_info(nvs_iterator_t i,nvs_entry_info_t *out) {strcpy(out->key,keys[reinterpret_cast<uintptr_t>(i)-1]);return ESP_OK;}
inline int nvs_entry_next(nvs_iterator_t *i) {auto n=reinterpret_cast<uintptr_t>(*i)+1;if(n>9){*i=nullptr;return ESP_ERR_NVS_NOT_FOUND;}*i=reinterpret_cast<void *>(n);return ESP_OK;}
inline void nvs_release_iterator(nvs_iterator_t) {}
inline int nvs_erase_key(nvs_handle_t,const char *key) {return hal().storage->erase(key)?ESP_OK:ESP_ERR_NVS_NOT_FOUND;}
inline int nvs_commit(nvs_handle_t) {return ESP_OK;}
''')
    original=(ROOT/'sim/screenshots/capture.cpp').read_text()
    a=original.index('    start(weather);'); b=original.index('    start(calculator);',a)
    jobs=CHECK[CHECK.index('struct Job'):CHECK.index('int main()')]
    jobs=jobs.replace('static int64_t now_us = 1000000;','').replace('int64_t esp_timer_get_time() { return now_us; }','')
    source=original[:a]+SCENARIOS+original[b:]
    source=source.replace('int main()', '#include "crystal_http.h"\n#include <string>\n#include <vector>\nextern CrystalNetworkType route;\nextern uint32_t route_revision;\n'+jobs+'\nint main()').replace('#include "crystal_hal.hpp"','#include "crystal_hal.hpp"\n#include "crystal_network_policy.hpp"')
    (work/'check.cpp').write_text(source)
    services=(ROOT/'sim/screenshots/services.cpp').read_text()
    a=services.index('uint32_t crystal_network_revision()'); b=services.index('void crystal_stopwatch_set_running',a)
    services=services[:a]+services[b:]
    services=services.replace('CrystalNetworkType crystal_network_active_type() { return CrystalNetworkType::Ethernet; }','CrystalNetworkType route=CrystalNetworkType::Ethernet;\nuint32_t route_revision=0;\nuint32_t crystal_network_revision() { return route_revision; }\nCrystalNetworkType crystal_network_active_type() { return route; }')
    services=services.replace('const time_t value = 1794276540;', 'const time_t value = 1794276540 + lv_tick_get()/1000;').replace('#include <arpa/inet.h>','#include <arpa/inet.h>\n#include "lvgl.h"')
    (work/'services.cpp').write_text(services)
    cmake=(ROOT/'.cache/readme-captures/CMakeLists.txt').read_text().replace(str(ROOT/'sim/screenshots/capture.cpp'),str(work/'check.cpp')).replace(str(ROOT/'sim/screenshots/services.cpp'),str(work/'services.cpp'))
    cmake+=f'''
target_include_directories(capture BEFORE PRIVATE "{work}")
target_sources(capture PRIVATE "{ROOT}/components/weather_app/src/weather_request.cpp" "{ROOT}/components/crystal_core/src/crystal_location.cpp" "{ROOT}/managed_components/espressif__cjson/cJSON/cJSON.c")
target_include_directories(capture PRIVATE "{ROOT}/components/crystal_http/include" "{ROOT}/managed_components/espressif__cjson/cJSON")
set_source_files_properties("{ROOT}/components/weather_app/src/weather_request.cpp" PROPERTIES COMPILE_DEFINITIONS "time=crystal_capture_time")
target_compile_options(capture PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(capture PRIVATE -fsanitize=address,undefined)
'''
    (work/'CMakeLists.txt').write_text(cmake)
    subprocess.run(['cmake','-S',str(work),'-B',str(work/'build')],check=True,stdout=subprocess.DEVNULL)
    subprocess.run(['cmake','--build',str(work/'build'),'-j','8'],check=True,stdout=subprocess.DEVNULL)
    subprocess.run([str(work/'build/capture')],check=True,cwd=work)
if __name__=='__main__': main()
