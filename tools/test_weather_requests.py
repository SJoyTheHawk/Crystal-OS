#!/usr/bin/env python3
"""Production request/location code under ASan/UBSan with scripted HTTP completion."""
import pathlib
import subprocess
import tempfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
CHECK = r'''
#include "weather_request.hpp"
#include "crystal_http.h"
#include "crystal_hal.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
struct Job { uint32_t id; crystal_http_callback_t cb; void *context; std::string url; };
static std::vector<Job> jobs;
static uint32_t next_id, cancels, bodies;
static int64_t now_us = 1000000;
static bool reject, immediate, finish_on_cancel;
int64_t esp_timer_get_time() { return now_us; }
static const char *forecast = R"({"current":{"temperature_2m":24.6,"relative_humidity_2m":68,"weather_code":2,"wind_speed_10m":12.3}})";
static void finish(Job job, const char *body, int status=200) {
    crystal_http_response_t r{};
    r.request_id=job.id; r.status_code=status; r.transport_error=ESP_OK;
    if(body) {r.body=(uint8_t*)strdup(body);r.body_len=strlen(body);++bodies;}
    job.cb(&r, job.context);
}
extern "C" uint32_t crystal_http_get(const crystal_http_options_t *o, crystal_http_callback_t cb, void *ctx) {
    assert(o->max_attempts==1 && o->max_body_bytes==1023 && o->owner_id);
    assert(o->timeout_ms==(CONFIG_IDF_TARGET_ESP32P4?15000:8000));
    if(reject) return 0;
    Job job{++next_id,cb,ctx,o->url}; jobs.push_back(job);
    if(immediate) finish(job,forecast);
    return job.id;
}
extern "C" bool crystal_http_cancel(uint32_t id) {
    ++cancels;
    if(finish_on_cancel) {finish_on_cancel=false; for(auto &j:jobs) if(j.id==id) finish(j,forecast);}
    return true;
}
extern "C" void crystal_http_response_release(const crystal_http_response_t *r) {if(r->body){free(r->body);--bodies;}}
int main() {
    crystal_hal_init();
    // Cached getters and manual settings never submit network requests.
    double lat,lon; char city[24];
    assert(crystal_location_get(&lat,&lon,city,sizeof(city)) && jobs.empty());
    assert(!crystal_location_snapshot().resolved);
    WeatherRequest request;
    CrystalWeatherReading reading{};
    assert(request.start() && jobs.back().url.find("ipwho.is")!=std::string::npos);
    finish(jobs.back(), R"({"success":true,"city":"Tokyo","latitude":35.68,"longitude":139.69})");
    assert(!request.poll(reading));
    assert(crystal_location_snapshot().resolved && jobs.back().url.find("35.6800")!=std::string::npos);
    finish(jobs.back(),forecast);
    assert(request.poll(reading) && reading.success && reading.temperature_c10==246 && !strcmp(reading.city,"Tokyo"));
    assert(!request.pending());
    const auto count=jobs.size();
    assert(crystal_weather_set_location(22,114,"Manual") && jobs.size()==count);
    const auto manual=crystal_location_snapshot();
    assert(!crystal_location_accept(manual.revision,10,20,"late auto"));
    assert(!crystal_weather_set_location(100,114,"bad"));
    assert(request.start()); const auto late=jobs.back();
    request.cancel(); assert(!request.pending());
    assert(request.start()); const auto current=jobs.back();
    finish(late,forecast); assert(!request.poll(reading));
    finish(current,forecast); assert(request.poll(reading)&&reading.success);
    assert(request.start());
    crystal_weather_set_location(23,115,"New");
    finish(jobs.back(),forecast); assert(request.poll(reading)&&!reading.success);
    assert(request.start()); const auto timeout=jobs.back();
    now_us+=30000001;
    assert(request.poll(reading)&&!reading.success&&!request.pending());
    finish(timeout,forecast);
    assert(request.start()); finish_on_cancel=true; request.cancel();
    immediate=true; assert(request.start()); assert(request.poll(reading)&&reading.success); immediate=false;
    reject=true; assert(!request.start()&&!request.pending()); reject=false;
    for(const char *body : {"garbage", "{}", "{\"current\":{\"temperature_2m\":1e300}}"}) {
        assert(request.start());finish(jobs.back(),body);assert(request.poll(reading)&&!reading.success);
    }
    assert(request.start());finish(jobs.back(),forecast,503);assert(request.poll(reading)&&!reading.success);
    // Automatic mode invalidates the cached location without any HTTP work.
    const auto before=jobs.size(); crystal_weather_set_automatic(true); assert(jobs.size()==before);
    assert(request.start()); const auto locate=jobs.back();
    crystal_weather_set_location(25,121,"Taipei");
    finish(locate,R"({"success":true,"city":"stale","latitude":1,"longitude":2})");
    assert(request.poll(reading)&&!reading.success);
    assert(!strcmp(crystal_location_snapshot().city,"Taipei"));
    crystal_weather_set_automatic(true);
    assert(request.start()); finish(jobs.back(),"{}",503);
    assert(!request.poll(reading)); // forecast from labelled fallback
    assert(!crystal_location_snapshot().resolved);
    finish(jobs.back(),forecast); assert(request.poll(reading)&&reading.success);
    assert(!strcmp(reading.city,"Hong Kong (fallback)"));
    // Destroying an owner cannot leave a callback pointing into that owner.
    Job detached;
    { WeatherRequest temporary; assert(temporary.start()); detached=jobs.back(); }
    finish(detached,"{}");
    assert(bodies==0 && cancels>=4);
    puts("PASS: async completion, late/inline/cancel races, timeout, rejection, parsing, settings generations, cache-only getters and fallback");
}
'''
def main():
    with tempfile.TemporaryDirectory(prefix='weather-request-') as d:
        p=pathlib.Path(d)
        (p/'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time();\n')
        (p/'check.cpp').write_text(CHECK)
        cjson=ROOT/'managed_components/espressif__cjson/cJSON'
        subprocess.run(['cc','-fsanitize=address,undefined','-c',str(cjson/'cJSON.c'),'-o',str(p/'json.o')],check=True)
        for target in (0,1):
            cmd=['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',f'-DCONFIG_IDF_TARGET_ESP32P4={target}']
            for include in [p,ROOT/'sim/screenshots/stubs',cjson]+[ROOT/f'components/{name}/include' for name in ['weather_app','crystal_core','crystal_hal','crystal_http']]:
                cmd+=['-I',str(include)]
            cmd += [str(p/'check.cpp'),str(ROOT/'components/weather_app/src/weather_request.cpp'),str(ROOT/'components/crystal_core/src/crystal_location.cpp'),str(ROOT/'sim/crystal_hal_mock.cpp'),str(p/'json.o'),'-o',str(p/'check')]
            subprocess.run(cmd,check=True); subprocess.run([str(p/'check')],check=True)
if __name__=='__main__': main()
