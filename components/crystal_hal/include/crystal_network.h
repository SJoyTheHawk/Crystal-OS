#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// True only while Wi-Fi or Ethernet holds an IPv4 address. Link/association
// alone is insufficient; this does not promise Internet reachability.
bool crystal_network_has_ip(void);

#ifdef __cplusplus
}
#endif
