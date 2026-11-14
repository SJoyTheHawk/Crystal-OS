#!/usr/bin/env python3
"""Production route policy with deterministic netif/lease failures (not radio tests)."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include <cassert>
#include <cstdio>
#include <vector>
#include "crystal_hal.hpp"
#include "crystal_ethernet.hpp"
#include "crystal_network_policy.hpp"
#include "esp_netif.h"
static bool wifi_ip, ethernet_ip, wifi_handle=true, ethernet_handle=true, route_fails;
static int writes;
static esp_netif_t wifi{1},ethernet{2},*selected;
static IWifi wifi_adapter;
static CrystalHal services{&wifi_adapter};
CrystalHal &hal(){return services;}
bool IWifi::has_ip()const{return wifi_ip;}
bool crystal_ethernet_is_connected(){return ethernet_ip;}
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key){return key[0]=='E'?(ethernet_handle?&ethernet:nullptr):(wifi_handle?&wifi:nullptr);}
int esp_netif_set_default_netif(esp_netif_t *netif){++writes;if(route_fails)return -1;selected=netif;return 0;}
struct Change{CrystalNetworkType before,after;};
static std::vector<Change> changes;
static void changed(CrystalNetworkType before,CrystalNetworkType after,void *context){assert(context==&changes);assert(crystal_network_active_type()==after);changes.push_back({before,after});}
int main(){
 using T=CrystalNetworkType;
 static_assert(crystal_network_select(false,false)==T::None);
 static_assert(crystal_network_select(true,false)==T::WiFi);
 static_assert(crystal_network_select(false,true)==T::Ethernet);
 static_assert(crystal_network_select(true,true)==T::Ethernet);
 crystal_network_set_callback(changed,&changes);
 crystal_network_policy_refresh();assert(changes.empty()&&writes==0);
 wifi_ip=true;crystal_network_policy_refresh();assert(selected==&wifi&&changes.size()==1);
 ethernet_ip=true;ethernet_handle=false;crystal_network_policy_refresh();assert(crystal_network_active_type()==T::WiFi&&changes.size()==1);
 ethernet_handle=true;route_fails=true;crystal_network_policy_refresh();assert(crystal_network_active_type()==T::WiFi&&changes.size()==1);
 route_fails=false;crystal_network_policy_refresh();assert(selected==&ethernet&&changes.size()==2);
 wifi_ip=false;crystal_network_policy_refresh();assert(crystal_network_active_type()==T::Ethernet&&changes.size()==2);
 wifi_ip=true;crystal_network_policy_refresh();assert(changes.size()==2);
 ethernet_ip=false;crystal_network_policy_refresh();assert(selected==&wifi&&changes.size()==3);
 wifi_ip=false;crystal_network_policy_refresh();assert(crystal_network_active_type()==T::None&&changes.size()==4);
 ethernet_ip=true;crystal_network_policy_refresh();assert(selected==&ethernet&&changes.size()==5);
 for(int i=0;i<30;++i){wifi_ip=true;ethernet_ip=false;crystal_network_policy_refresh();assert(selected==&wifi);ethernet_ip=true;crystal_network_policy_refresh();assert(selected==&ethernet);}
 const auto count=changes.size();for(int i=0;i<100;++i)crystal_network_policy_refresh();assert(changes.size()==count);
 crystal_network_set_callback(nullptr,nullptr);wifi_ip=ethernet_ip=false;crystal_network_policy_refresh();assert(changes.size()==count);
 std::puts("PASS: lease-gated Ethernet preference, WiFi fallback, offline recovery, missing netif/route failure retry, repeated switching and callback deduplication");
}
'''
def main():
 with tempfile.TemporaryDirectory(prefix='crystal-network-') as directory:
  p=Path(directory)
  (p/'crystal_hal.hpp').write_text('#pragma once\nstruct IWifi {bool has_ip() const;};struct CrystalHal{IWifi *wifi;};CrystalHal &hal();\n')
  (p/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;constexpr int ESP_OK=0;\n')
  (p/'esp_netif.h').write_text('#pragma once\n#include "esp_err.h"\nstruct esp_netif_t{int id;};esp_netif_t *esp_netif_get_handle_from_ifkey(const char*);int esp_netif_set_default_netif(esp_netif_t*);\n')
  (p/'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n')
  (p/'check.cpp').write_text(HARNESS)
  subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-pthread','-I',str(p),'-I',str(ROOT/'components/crystal_hal/include'),str(p/'check.cpp'),str(ROOT/'components/crystal_hal/src/crystal_network_policy.cpp'),'-o',str(p/'check')],check=True)
  subprocess.run([str(p/'check')],check=True)
if __name__=='__main__':main()
