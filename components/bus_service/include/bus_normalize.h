#pragma once
#include "bus_service.h"
void bus_normalize_stop_id(char *stop_id);
char bus_normalize_direction(const char *direction, bus_operator_t op);
uint8_t bus_normalize_service_type(const char *value, bus_operator_t op);
