/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once
#include <stdint.h>

enum class CrystalNetworkType : uint8_t { None, WiFi, Ethernet };

// Link/association alone is insufficient. An interface needs an IPv4 lease.
constexpr CrystalNetworkType crystal_network_select(bool wifi_ip, bool ethernet_ip)
{
    return ethernet_ip ? CrystalNetworkType::Ethernet :
           wifi_ip ? CrystalNetworkType::WiFi : CrystalNetworkType::None;
}

// Refreshes and their callbacks are serialized across event/service tasks.
// Callbacks must be brief and must not re-enter refresh. A route change is
// reported even while connectivity stays up so services can recover sockets.
using crystal_network_callback_t = void (*)(CrystalNetworkType previous,
                                            CrystalNetworkType active, void *context);
void crystal_network_set_callback(crystal_network_callback_t callback, void *context);
void crystal_network_policy_refresh();
CrystalNetworkType crystal_network_active_type();
const char *crystal_network_type_name(CrystalNetworkType type);

// Route generation includes reconnects to the same interface.
uint32_t crystal_network_revision();
