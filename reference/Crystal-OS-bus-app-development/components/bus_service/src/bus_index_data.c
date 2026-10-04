#include "bus_routes.h"

// Minimal route index for testing
// Format: 4-char space-padded name, ops bitmask (bit0=KMB, bit1=CTB)
// This is a placeholder - should be generated from actual data
const bus_route_name_t bus_route_index[] = {
    {{'1', ' ', ' ', ' '}, 0x01},  // KMB
    {{'2', ' ', ' ', ' '}, 0x01},  // KMB
    {{'3', ' ', ' ', ' '}, 0x01},  // KMB
    {{'6', '8', 'X', ' '}, 0x01},  // KMB
    {{'9', '6', '0', ' '}, 0x01},  // KMB
    {{'A', '1', '0', ' '}, 0x02},  // CTB
    {{'B', '3', 'X', ' '}, 0x01},  // KMB
};

const uint16_t bus_route_index_count = sizeof(bus_route_index) / sizeof(bus_route_index[0]);
