/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once
#include <stdint.h>
#include "esp_err.h"

// Snapshot only: Settings and apps never perform PHY/RPC work while drawing.
struct CrystalEthernetInfo {
    bool supported = false;
    bool initialized = false;
    bool started = false;
    bool link = false;
    bool has_ip = false;
    uint32_t ip = 0, mask = 0, gateway = 0, dns = 0;
    uint8_t mac[6] = {};
    uint16_t speed_mbps = 0;
    bool full_duplex = false;
    esp_err_t error = ESP_OK;
};

// Called from the service task after TCP/IP/event-loop setup, never the UI task.
// A missing PHY or driver failure is non-fatal and leaves Wi-Fi usable.
bool crystal_ethernet_init();
bool crystal_ethernet_start();
void crystal_ethernet_stop();
bool crystal_ethernet_is_connected();
CrystalEthernetInfo crystal_ethernet_info();
