#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bus_service.h"

#ifdef __cplusplus
extern "C" {
#endif

// Canonical route key: trim surrounding whitespace, uppercase, and require
// one to four non-whitespace characters.
bool bus_normalize_route_label(const char *source, char out[5]);

// Map only explicit I/O or inbound/outbound values. Absent input is unknown.
bool bus_normalize_direction(const char *source, char *out);

// Parse a provider service type. The caller controls whether omission defaults.
bool bus_normalize_service_type(const char *source, bool present,
                                bool default_when_missing, uint8_t *out);

// Copy optional localized text with termination. Missing text is valid; an
// oversized field is rejected so normalization never silently changes data.
bool bus_normalize_text(const char *source, char *out, size_t out_size,
                        bool optional);

#ifdef __cplusplus
}
#endif
