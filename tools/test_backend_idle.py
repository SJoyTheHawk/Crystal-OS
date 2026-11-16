#!/usr/bin/env python3
"""Execute the production OTA worker/control functions with deterministic task notifications."""
import pathlib
import subprocess
import tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
def function(source, signature):
    start=source.index(signature)
    body=source.index('{',start)
    depth=1; end=body+1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}'); end+=1
    return source[start:end]
MOCK=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <deque>
#include <stdexcept>
#include "crystal_update.h"
#include "crystal_update_manifest.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(v) (v)
#define MANIFEST_PRIMARY "primary"
#define MANIFEST_MIRROR "mirror"
static crystal_update_snapshot_t s_snapshot{};
static void *s_ota_worker_task=(void *)1;
static bool s_check_requested,s_download_requested,s_cancel_requested,s_all_download_urls_failed;
static unsigned notifications,waits,fetches,downloads;
static std::deque<std::function<void()>> actions;
struct Idle {};
struct Reboot {};
static void xTaskNotifyGive(void *task) {assert(task); ++notifications;}
static unsigned ulTaskNotifyTake(int clear,unsigned timeout) {
    assert(clear==pdTRUE && timeout==portMAX_DELAY); ++waits;
    if(!notifications) {
        if(actions.empty()) throw Idle{};
        auto action=actions.front();actions.pop_front();action();
    }
    assert(notifications); notifications=0;return 1;
}
static int esp_restart() {throw Reboot{};}
static void vTaskDelay(unsigned ms) {assert(ms==2000);}
static void notify_ota_ui() {}
static int fetch_manifest(const char *,update_manifest_t *out) {
    ++fetches;strcpy(out->version,"1.0.0");out->size=123;return ESP_OK;
}
static const char *crystal_version_string() {return "0.0.0";}
static int install_image(update_manifest_t *) {++downloads;return ESP_OK;}
static int persist_attempted_version(const char *) {return ESP_OK;}
static void set_error(const char *) {s_snapshot.state=UPDATE_STATE_FAILED;}
'''
CHECK=r'''
static void reset() {
    s_snapshot={}; s_snapshot.state=UPDATE_STATE_IDLE;
    s_check_requested=s_download_requested=s_cancel_requested=false;
    notifications=waits=fetches=downloads=0;actions.clear();
}
static void idle() {try {ota_worker_task(nullptr);assert(false);}catch(const Idle &) {}}
int main() {
    reset(); idle(); assert(waits==1 && fetches==0 && downloads==0);
    reset();
    actions.push_back([] {assert(crystal_update_check()==ESP_OK);});
    actions.push_back([] {assert(s_snapshot.state==UPDATE_STATE_AVAILABLE);crystal_update_cancel_check();});
    idle();assert(fetches==1 && downloads==0 && s_snapshot.state==UPDATE_STATE_IDLE);
    reset();
    assert(crystal_update_check()==ESP_OK); // Work queued before worker sleeps.
    actions.push_back([] {assert(s_snapshot.state==UPDATE_STATE_AVAILABLE);assert(crystal_update_start_download()==ESP_OK);});
    try {ota_worker_task(nullptr);assert(false);}catch(const Reboot &) {}
    assert(fetches==1 && downloads==1 && waits==2);
    reset();assert(crystal_update_start_download()==ESP_ERR_INVALID_STATE && notifications==0);
    s_ota_worker_task=nullptr;assert(crystal_update_check()==ESP_ERR_INVALID_STATE);
    crystal_update_cancel_check();assert(notifications==0);
    puts("PASS: OTA sleeps indefinitely when idle, wakes for check/download/cancel, handles prequeued work and rejects invalid requests");
}
'''
def main():
    source=(ROOT/'main/crystal_update.c').read_text()
    selected='\n'.join(function(source,s) for s in [
        'static void ota_worker_task(void *arg)',
        'esp_err_t crystal_update_check(void)',
        'void crystal_update_cancel_check(void)',
        'esp_err_t crystal_update_start_download(void)'])
    with tempfile.TemporaryDirectory(prefix='backend-idle-') as d:
        p=pathlib.Path(d)
        (p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\nenum {ESP_OK=0,ESP_ERR_INVALID_STATE=1,ESP_ERR_NOT_FOUND=2,ESP_ERR_INVALID_CRC=3,ESP_ERR_OTA_VALIDATE_FAILED=4,ESP_ERR_OTA_ROLLBACK_INVALID_STATE=5};\n')
        (p/'check.cpp').write_text(MOCK+selected+CHECK)
        subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I',str(p),'-I',str(ROOT/'main'),'-I',str(ROOT/'components/crystal_core/include'),str(p/'check.cpp'),'-o',str(p/'check')],check=True)
        subprocess.run([str(p/'check')],check=True)
    # Architectural boundaries: core may initialize transport but cannot fetch.
    core=(ROOT/'components/crystal_core/src/crystal_core.cpp').read_text()
    service=function(core,'void service_task(void *)')
    assert 'weather' not in service and 'crystal_http_get' not in service and 'http_get_json' not in service
    assert 'queue_https_smoke_test' not in core and 'example.com' not in core
    boot=(ROOT/'main/main.cpp').read_text()
    assert 'if (idle) lv_timer_pause(timer)' in boot and 'crystal_package_set_cleanup_ready_hook' in boot
    print('PASS: system service has no app HTTP waits or startup diagnostics; cleanup timer pauses when idle')
if __name__=='__main__':main()
