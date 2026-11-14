/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_ethernet.hpp"
#include "crystal_network_policy.hpp"
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32P4
#include <mutex>
#include "esp_eth.h"
#include "esp_eth_phy_ip101.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "crystal_eth";
std::mutex s_lock;
CrystalEthernetInfo s_info;
esp_eth_handle_t s_handle;
esp_eth_mac_t *s_mac;
esp_eth_phy_t *s_phy;
esp_netif_t *s_netif;
esp_eth_netif_glue_handle_t s_glue;
esp_event_handler_instance_t s_eth_event, s_ip_event;

void clear_address()
{
    s_info.has_ip = false;
    s_info.ip = s_info.mask = s_info.gateway = s_info.dns = 0;
}

void event_handler(void *, esp_event_base_t base, int32_t id, void *data)
{
    {
        std::lock_guard<std::mutex> lock(s_lock);
        if (base == ETH_EVENT) {
            if (!data || *static_cast<esp_eth_handle_t *>(data) != s_handle) return;
            if (id == ETHERNET_EVENT_START) s_info.started = true;
            else if (id == ETHERNET_EVENT_CONNECTED) {
                s_info.link = true;
                eth_speed_t speed = ETH_SPEED_10M;
                eth_duplex_t duplex = ETH_DUPLEX_HALF;
                if (esp_eth_ioctl(s_handle, ETH_CMD_G_SPEED, &speed) == ESP_OK)
                    s_info.speed_mbps = speed == ETH_SPEED_100M ? 100 : 10;
                if (esp_eth_ioctl(s_handle, ETH_CMD_G_DUPLEX_MODE, &duplex) == ESP_OK)
                    s_info.full_duplex = duplex == ETH_DUPLEX_FULL;
                ESP_LOGI(TAG, "Ethernet link up: %u Mbps", s_info.speed_mbps);
            } else if (id == ETHERNET_EVENT_DISCONNECTED || id == ETHERNET_EVENT_STOP) {
                s_info.link = false;
                s_info.speed_mbps = 0;
                s_info.full_duplex = false;
                clear_address();
                if (id == ETHERNET_EVENT_STOP) s_info.started = false;
                ESP_LOGI(TAG, "Ethernet link down");
            }
        } else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP) {
            auto *event = static_cast<ip_event_got_ip_t *>(data);
            if (!event || event->esp_netif != s_netif || !s_info.link) return;
            s_info.ip = event->ip_info.ip.addr;
            s_info.mask = event->ip_info.netmask.addr;
            s_info.gateway = event->ip_info.gw.addr;
            s_info.has_ip = s_info.ip != 0;
            esp_netif_dns_info_t dns = {};
            if (esp_netif_get_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK)
                s_info.dns = dns.ip.u_addr.ip4.addr;
            ESP_LOGI(TAG, "Ethernet IPv4: " IPSTR, IP2STR(&event->ip_info.ip));
        } else if (base == IP_EVENT && id == IP_EVENT_ETH_LOST_IP) {
            if (!data || static_cast<ip_event_got_ip_t *>(data)->esp_netif != s_netif) return;
            clear_address();
        } else return;
    }
    crystal_network_policy_refresh();
}

void release_resources()
{
    if (s_ip_event) esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, s_ip_event);
    if (s_eth_event) esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, s_eth_event);
    s_ip_event = s_eth_event = nullptr;
    if (s_glue) esp_eth_del_netif_glue(s_glue);
    s_glue = nullptr;
    if (s_netif) esp_netif_destroy(s_netif);
    s_netif = nullptr;
    if (s_handle) esp_eth_driver_uninstall(s_handle);
    s_handle = nullptr;
    if (s_phy) s_phy->del(s_phy);
    if (s_mac) s_mac->del(s_mac);
    s_phy = nullptr;
    s_mac = nullptr;
}
}

bool crystal_ethernet_init()
{
    // Single service-task owner; do not hold the status lock over driver calls.
    if (crystal_ethernet_info().initialized) return true;
    esp_err_t error = ESP_OK;
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp_eth_config_t driver = {};
    esp_netif_inherent_config_t inherent = ESP_NETIF_INHERENT_DEFAULT_ETH();
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    // Board-specific factory BSP: IP101, address 1, external 50MHz RMII clock.
    emac.smi_gpio.mdc_num = 31;
    emac.smi_gpio.mdio_num = 52;
    emac.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac.clock_config.rmii.clock_gpio = 50;
    emac.emac_dataif_gpio.rmii.tx_en_num = 49;
    emac.emac_dataif_gpio.rmii.txd0_num = 34;
    emac.emac_dataif_gpio.rmii.txd1_num = 35;
    emac.emac_dataif_gpio.rmii.crs_dv_num = 28;
    emac.emac_dataif_gpio.rmii.rxd0_num = 29;
    emac.emac_dataif_gpio.rmii.rxd1_num = 30;
    phy_config.phy_addr = 1;
    phy_config.reset_gpio_num = 51;
    s_mac = esp_eth_mac_new_esp32(&emac, &mac_config);
    s_phy = esp_eth_phy_new_ip101(&phy_config);
    if (!s_mac || !s_phy) { error = ESP_ERR_NO_MEM; goto fail; }
    driver = ETH_DEFAULT_CONFIG(s_mac, s_phy);
    error = esp_eth_driver_install(&driver, &s_handle);
    if (error != ESP_OK) goto fail;
    inherent.route_prio = 150;
    netif_config.base = &inherent;
    s_netif = esp_netif_new(&netif_config);
    if (!s_netif) { error = ESP_ERR_NO_MEM; goto fail; }
    error = esp_netif_set_hostname(s_netif, "crystal");
    if (error != ESP_OK) goto fail;
    s_glue = esp_eth_new_netif_glue(s_handle);
    if (!s_glue) { error = ESP_ERR_NO_MEM; goto fail; }
    error = esp_netif_attach(s_netif, s_glue);
    if (error != ESP_OK) goto fail;
    error = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, event_handler, nullptr, &s_eth_event);
    if (error != ESP_OK) goto fail;
    error = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, event_handler, nullptr, &s_ip_event);
    if (error != ESP_OK) goto fail;
    {
        std::lock_guard<std::mutex> lock(s_lock);
        s_info.supported = s_info.initialized = true;
        s_info.error = ESP_OK;
        esp_eth_ioctl(s_handle, ETH_CMD_G_MAC_ADDR, s_info.mac);
    }
    ESP_LOGI(TAG, "IP101 ready: PHY 1, RMII external clock GPIO50");
    return true;
fail:
    release_resources();
    {
        std::lock_guard<std::mutex> lock(s_lock);
        s_info = {};
        s_info.supported = true;
        s_info.error = error;
    }
    ESP_LOGW(TAG, "Ethernet unavailable: %s; WiFi remains available", esp_err_to_name(error));
    return false;
}

bool crystal_ethernet_start()
{
    if (!s_handle) return false;
    if (crystal_ethernet_info().started) return true;
    const esp_err_t error = esp_eth_start(s_handle);
    std::lock_guard<std::mutex> lock(s_lock);
    s_info.error = error;
    if (error == ESP_OK) s_info.started = true;
    return error == ESP_OK;
}
void crystal_ethernet_stop()
{
    if (!s_handle || !crystal_ethernet_info().started) return;
    const esp_err_t error = esp_eth_stop(s_handle);
    {
        std::lock_guard<std::mutex> lock(s_lock);
        s_info.error = error;
        if (error == ESP_OK) { s_info.started = s_info.link = false; clear_address(); }
    }
    crystal_network_policy_refresh();
}
CrystalEthernetInfo crystal_ethernet_info()
{
    std::lock_guard<std::mutex> lock(s_lock);
    auto info = s_info;
    info.supported = true;
    return info;
}
bool crystal_ethernet_is_connected() { return crystal_ethernet_info().has_ip; }
#else
bool crystal_ethernet_init() { return false; }
bool crystal_ethernet_start() { return false; }
void crystal_ethernet_stop() {}
bool crystal_ethernet_is_connected() { return false; }
CrystalEthernetInfo crystal_ethernet_info() { return {}; }
#endif
