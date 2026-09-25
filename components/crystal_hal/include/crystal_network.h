#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns true only after the station has acquired an IP address. Network
// clients must use this gate before starting DNS/TLS work during boot.
bool crystal_network_has_ip(void);

#ifdef __cplusplus
}
#endif
