/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_network_policy.hpp"
#include "crystal_network.h"
#include "crystal_ethernet.hpp"
#include "crystal_hal.hpp"
#include "esp_netif.h"
#include "esp_log.h"
#include <atomic>
#include <mutex>

namespace {
std::atomic<CrystalNetworkType> s_active{CrystalNetworkType::None};
std::mutex s_refresh_lock;
std::mutex s_policy_lock;
crystal_network_callback_t s_callback;
void *s_context;
}

void crystal_network_set_callback(crystal_network_callback_t callback, void *context)
{
    std::lock_guard<std::mutex> lock(s_policy_lock);
    s_callback = callback;
    s_context = context;
}

void crystal_network_policy_refresh()
{
    // Keep notifications ordered when a service tick races a link/IP event.
    // The callback can read the atomic snapshot without taking this lock.
    std::lock_guard<std::mutex> refresh_lock(s_refresh_lock);
    CrystalNetworkType previous, active;
    crystal_network_callback_t callback;
    void *context;
    {
        std::lock_guard<std::mutex> lock(s_policy_lock);
        const bool wifi_ip = hal().wifi && hal().wifi->has_ip();
        active = crystal_network_select(wifi_ip, crystal_ethernet_is_connected());
        previous = s_active.load();
        if (active == previous) return;
        if (active != CrystalNetworkType::None) {
            auto *netif = esp_netif_get_handle_from_ifkey(active == CrystalNetworkType::Ethernet ? "ETH_DEF" : "WIFI_STA_DEF");
            if (!netif || esp_netif_set_default_netif(netif) != ESP_OK) {
                ESP_LOGW("crystal_network", "Cannot select %s route", crystal_network_type_name(active));
                return; // Retry on the service tick; never announce a false route.
            }
        }
        s_active.store(active);
        callback = s_callback;
        context = s_context;
    }
    ESP_LOGI("crystal_network", "Active route: %s -> %s", crystal_network_type_name(previous), crystal_network_type_name(active));
    if (callback) callback(previous, active, context);
}
CrystalNetworkType crystal_network_active_type() { return s_active.load(); }
const char *crystal_network_type_name(CrystalNetworkType type)
{
    return type == CrystalNetworkType::Ethernet ? "Ethernet" : type == CrystalNetworkType::WiFi ? "WiFi" : "Offline";
}
