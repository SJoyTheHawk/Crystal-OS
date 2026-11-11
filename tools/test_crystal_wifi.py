#!/usr/bin/env python3
"""Exercise the production DeviceWifi with a scripted host Wi-Fi driver.

Run: python3 tools/test_crystal_wifi.py
Checks scan/reconnect arbitration and completion paths, not ESP32 radio timing.
"""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
MOCK = r'''
#include "crystal_hal.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <vector>
using esp_err_t = int;
using esp_event_base_t = int;
using nvs_handle_t = int;
struct esp_netif_t {};
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_INVALID_STATE=1, ESP_ERR_WIFI_STATE=2;
constexpr int ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED=3;
constexpr int WIFI_EVENT=1, IP_EVENT=2, ESP_EVENT_ANY_ID=-1;
constexpr int WIFI_EVENT_STA_START=1, WIFI_EVENT_STA_DISCONNECTED=2, WIFI_EVENT_SCAN_DONE=3;
constexpr int IP_EVENT_STA_GOT_IP=1, IP_EVENT_STA_LOST_IP=2;
constexpr int WIFI_MODE_STA=1, WIFI_STORAGE_FLASH=1, WIFI_IF_STA=0;
constexpr int WIFI_AUTH_OPEN=0, WIFI_AUTH_WPA_PSK=2, ESP_LOG_ERROR=1;
constexpr int ESP_NETIF_DNS_MAIN=0, ESP_NETIF_DNS_BACKUP=1, ESP_IPADDR_TYPE_V4=0;
constexpr int WIFI_PS_MAX_MODEM=1, WIFI_PS_NONE=0, ESP_TIMER_TASK=0;
constexpr int NVS_READONLY=0, NVS_READWRITE=1;
constexpr size_t kWifiMaxNetworks=20;
const char *TAG="test", *kStorageNamespace="crystal", *kWifiEnabledKey="wifi_enabled", *kDhcpKey="net.dhcp";
static void log_mock(const char *, const char *, ...) {}
#define ESP_LOGE log_mock
#define ESP_LOGW log_mock
#define ESP_LOGI log_mock
const char *esp_err_to_name(int) { return "mock"; }
void esp_log_level_set(const char *, int) {}
struct wifi_ap_record_t { uint8_t ssid[33]{}; int8_t rssi=0; int authmode=0; };
wifi_ap_record_t s_wifi_records[kWifiMaxNetworks]{};
struct wifi_config_t { struct {
    uint8_t ssid[33]{}, password[65]{}; bool bssid_set=false; int channel=0;
    struct { int authmode=0; } threshold;
    struct { bool capable=false, required=false; } pmf_cfg;
} sta; };
struct wifi_init_config_t {};
#define WIFI_INIT_CONFIG_DEFAULT() {}
struct wifi_event_sta_disconnected_t { uint8_t reason=201; };
struct wifi_event_sta_scan_done_t { uint32_t status=0; };
struct address { uint32_t addr=0; };
struct esp_netif_ip_info_t { address ip, netmask, gw; };
struct esp_netif_dns_info_t { struct { int type=0; struct { address ip4; } u_addr; } ip; };
struct esp_timer_create_args_t { void (*callback)(void *); void *arg; int dispatch_method; const char *name; bool skip_unhandled_events; };
struct Timer { void (*callback)(void *)=nullptr; void *arg=nullptr; bool armed=false; uint64_t delay=0; };
using esp_timer_handle_t = Timer *;
static Timer timer;
static bool associated, connecting, scanning;
static int connect_calls, disconnect_calls, scan_calls, clear_calls, scan_error, count_error, records_error;
static wifi_config_t saved;
static std::vector<wifi_ap_record_t> aps;
static void (*handler)(void *, esp_event_base_t, int32_t, void *);
static void *handler_arg;
static std::vector<IWifi::Event> notifications;
static void emit(int base, int id, void *data=nullptr) { handler(handler_arg, base, id, data); }
int esp_event_handler_register(int, int, decltype(handler) fn, void *arg) { handler=fn; handler_arg=arg; return 0; }
int esp_event_loop_create_default() { return 0; }
int esp_netif_init() { return 0; }
esp_netif_t *esp_netif_create_default_wifi_sta() { static esp_netif_t n; return &n; }
int esp_netif_dhcpc_start(esp_netif_t *) { return 0; }
int esp_netif_dhcpc_stop(esp_netif_t *) { return 0; }
int esp_netif_set_hostname(esp_netif_t *, const char *) { return 0; }
int esp_netif_set_ip_info(esp_netif_t *, const esp_netif_ip_info_t *) { return 0; }
int esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *) { return 0; }
int esp_netif_set_dns_info(esp_netif_t *, int, const esp_netif_dns_info_t *) { return 0; }
int esp_netif_get_dns_info(esp_netif_t *, int, esp_netif_dns_info_t *) { return 0; }
int esp_wifi_init(const wifi_init_config_t *) { return 0; }
int esp_wifi_set_mode(int) { return 0; }
int esp_wifi_set_storage(int) { return 0; }
int esp_wifi_get_config(int, wifi_config_t *out) { *out=saved; return 0; }
int esp_wifi_set_config(int, const wifi_config_t *in) { saved=*in; return 0; }
int esp_wifi_start() { return 0; }
int esp_wifi_stop() { associated=connecting=scanning=false; return 0; }
int esp_wifi_disconnect() { ++disconnect_calls; associated=connecting=false; return 0; }
int esp_wifi_connect() { ++connect_calls; if (scanning) return ESP_ERR_WIFI_STATE; connecting=true; return 0; }
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *out) {
    if (!associated) return ESP_FAIL;
    std::memcpy(out->ssid,saved.sta.ssid,sizeof(out->ssid)); return 0;
}
int esp_wifi_get_mac(int, uint8_t *) { return 0; }
int esp_wifi_set_ps(int) { return 0; }
int esp_wifi_scan_start(const void *, bool block) {
    assert(!block); ++scan_calls;
    if (connecting) return ESP_ERR_WIFI_STATE;
    if (scan_error) return scan_error;
    assert(!scanning); scanning=true; return 0;
}
int esp_wifi_scan_stop() { scanning=false; return 0; }
int esp_wifi_clear_ap_list() { ++clear_calls; aps.clear(); return 0; }
int esp_wifi_scan_get_ap_num(uint16_t *n) { *n=aps.size(); return count_error; }
int esp_wifi_scan_get_ap_records(uint16_t *n, wifi_ap_record_t *out) {
    if (records_error) return records_error;
    *n=std::min<size_t>(*n,aps.size());
    std::copy_n(aps.begin(),*n,out); aps.clear(); return 0;
}
int esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out) {
    timer={args->callback,args->arg,false,0}; *out=&timer; return 0;
}
int esp_timer_stop(Timer *t) { t->armed=false; return 0; }
int esp_timer_start_once(Timer *t, uint64_t us) { assert(us>0); t->armed=true; t->delay=us; return 0; }
int nvs_open(const char *, int, nvs_handle_t *) { return ESP_FAIL; }
int nvs_get_u8(nvs_handle_t, const char *, uint8_t *) { return ESP_FAIL; }
int nvs_set_u8(nvs_handle_t, const char *, uint8_t) { return 0; }
int nvs_commit(nvs_handle_t) { return 0; }
void nvs_close(nvs_handle_t) {}
struct Storage {
    bool get(const char *, void *, size_t *) { return false; }
    bool set(const char *, const void *, size_t) { return true; }
} s_storage;
#ifndef __APPLE__
size_t strlcpy(char *dst, const char *src, size_t size) {
    const size_t n=std::strlen(src); if(size) { const size_t c=std::min(n,size-1); std::memcpy(dst,src,c); dst[c]=0; } return n;
}
#endif
'''
CHECK = r'''
static void on_event(IWifi::Event e, void *) { notifications.push_back(e); }
static size_t scans_done() { return std::count(notifications.begin(),notifications.end(),IWifi::ScanDone); }
static void boot(DeviceWifi &wifi, const char *ssid="missing") {
    associated=connecting=scanning=false;
    connect_calls=disconnect_calls=scan_calls=clear_calls=scan_error=count_error=records_error=0;
    saved={}; timer={}; aps.clear(); notifications.clear();
    strlcpy(reinterpret_cast<char *>(saved.sta.ssid),ssid,sizeof(saved.sta.ssid));
    wifi.set_event_callback(on_event,nullptr); wifi.start(); emit(WIFI_EVENT,WIFI_EVENT_STA_START);
}
static void drop() {
    associated=connecting=false; wifi_event_sta_disconnected_t d; emit(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&d);
}
static void complete(unsigned status=0) {
    scanning=false; wifi_event_sta_scan_done_t done{status}; emit(WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&done);
}
static void add_ap(const char *ssid, int8_t rssi) {
    wifi_ap_record_t ap; strlcpy(reinterpret_cast<char *>(ap.ssid),ssid,sizeof(ap.ssid)); ap.rssi=rssi; aps.push_back(ap);
}
int main() {
    { // Out-of-range saved SSID, including a timer callback already dispatched.
        DeviceWifi w; boot(w); drop(); assert(timer.armed);
        timer.armed=false; timer.callback(timer.arg); assert(connecting);
        w.scan(); assert(scanning && !connecting && !timer.armed && disconnect_calls==1);
        const int connects=connect_calls; timer.callback(timer.arg); assert(connect_calls==connects);
        drop(); assert(!timer.armed);
        w.scan(); assert(scan_calls==1);
        add_ap("weak",-80); add_ap("nearby",-30); add_ap("nearby",-45); add_ap("",-20);
        complete(); IWifi::Network rows[20]{};
        assert(w.scan_results(rows,20)==2 && std::strcmp(rows[0].ssid,"nearby")==0);
        assert(scans_done()==1 && timer.armed && timer.delay>=1000000);
        timer.armed=false; timer.callback(timer.arg); assert(connect_calls==connects+1);
    }
    { // First startup attempt: no retry timer exists yet.
        DeviceWifi w; boot(w); w.scan(); assert(scanning && !connecting); complete();
        assert(timer.armed && timer.delay==1000000 && scans_done()==1);
    }
    { // Long reconnect backoff also yields to scanning.
        DeviceWifi w; boot(w); for(int i=0;i<11;++i) drop();
        assert(timer.delay==60000000); w.scan(); assert(!timer.armed); complete(); assert(timer.armed);
    }
    { // Associated scans preserve the connection and pin its network first.
        DeviceWifi w; boot(w); connecting=false; associated=true; emit(IP_EVENT,IP_EVENT_STA_GOT_IP);
        w.scan(); assert(disconnect_calls==0 && associated);
        add_ap("nearby",-30); add_ap("missing",-80); complete();
        IWifi::Network rows[2]{}; assert(w.scan_results(rows,2)==2);
        assert(std::strcmp(rows[0].ssid,"missing")==0 && !timer.armed);
        w.scan(); drop(); assert(!timer.armed); complete(); assert(timer.armed);
    }
    for(int failure=0;failure<4;++failure) {
        DeviceWifi w; boot(w);
        if(failure==0) scan_error=ESP_FAIL;
        w.scan();
        if(failure==1) count_error=ESP_FAIL;
        if(failure==2) records_error=ESP_FAIL;
        if(failure!=0) { add_ap("nearby",-20); complete(failure==3 ? 1 : 0); }
        assert(scans_done()==1 && timer.armed && w.scan_results(nullptr,0)==0);
        assert(failure==0 || aps.empty());
    }
    { // Forgetting during a scan must not reconnect when it completes.
        DeviceWifi w; boot(w); drop(); w.scan(); w.forget(); complete();
        assert(!timer.armed && w.last_ssid()[0]==0);
        int connects=connect_calls; timer.callback(timer.arg); assert(connect_calls==connects);
    }
    { // Turning off the radio drops late results and completes the UI request.
        DeviceWifi w; boot(w); drop(); w.scan(); w.set_enabled(false);
        add_ap("stale",-20); complete();
        assert(scans_done()==1 && !timer.armed && w.scan_results(nullptr,0)==0 && aps.empty());
        int connects=connect_calls; timer.callback(timer.arg); assert(connect_calls==connects);
        w.scan(); assert(scans_done()==2);
    }
    { // A cancelled scan's queued completion cannot finish a newer scan.
        DeviceWifi w; boot(w); w.scan(); w.connect("replacement","password");
        assert(std::strcmp(w.last_ssid(),"replacement")==0 && connecting && scans_done()==1);
        w.scan(); wifi_event_sta_scan_done_t old{}; emit(WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&old);
        assert(scanning && scans_done()==1 && !timer.armed);
        add_ap("fresh",-20); complete(); assert(scans_done()==2 && timer.armed);
        IWifi::Network row; assert(w.scan_results(&row,1)==1 && std::strcmp(row.ssid,"fresh")==0);
    }
    { // Successful association invalidates previously scheduled retry work.
        DeviceWifi w; boot(w); drop(); assert(timer.armed);
        associated=true; emit(IP_EVENT,IP_EVENT_STA_GOT_IP); assert(!timer.armed);
        int connects=connect_calls; timer.callback(timer.arg); assert(connect_calls==connects);
    }
    { DeviceWifi w; boot(w,""); w.scan(); complete(); assert(scans_done()==1 && !timer.armed); }
    { DeviceWifi w; notifications.clear(); w.set_event_callback(on_event,nullptr); w.scan(); assert(scans_done()==1); }
    puts("Wi-Fi scan/reconnect regression checks passed");
}
'''


def main():
    source = (ROOT / "components/crystal_hal/src/crystal_hal.cpp").read_text()
    wifi = source.split("class DeviceWifi final", 1)[1].split("class Axp2101Power final", 1)[0]
    with tempfile.TemporaryDirectory(prefix="crystal-wifi-") as tmp:
        cpp = pathlib.Path(tmp) / "test.cpp"
        binary = pathlib.Path(tmp) / "test"
        cpp.write_text(MOCK + "\nclass DeviceWifi final" + wifi + CHECK)
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-pthread", "-I", str(ROOT / "components/crystal_hal/include"),
                        str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
