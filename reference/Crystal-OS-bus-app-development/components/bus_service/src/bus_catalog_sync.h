#pragma once

#include <stdbool.h>

bool bus_catalog_sync_init(void);
bool bus_catalog_sync_request(bool explicit_refresh);
void bus_catalog_sync_foreground_begin(void);
void bus_catalog_sync_foreground_end(void);
void bus_catalog_sync_cancel_all(void);
void bus_catalog_sync_network_lost(void);
