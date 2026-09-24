#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
esp_err_t bus_cache_init(void);
bool bus_cache_is_valid(const char *name, unsigned max_age_sec);
esp_err_t bus_cache_save(const char *name, const char *data, size_t len);
esp_err_t bus_cache_load(const char *name, char **data, size_t *len);
void bus_cache_free(char *data);
esp_err_t bus_cache_clear_all(void);
