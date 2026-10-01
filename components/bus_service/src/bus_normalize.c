#include "bus_normalize.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

bool bus_normalize_route_label(const char *source, char out[5])
{
    if (source == NULL || out == NULL) {
        return false;
    }
    const unsigned char *start = (const unsigned char *)source;
    while (*start != '\0' && isspace(*start)) {
        start++;
    }
    const unsigned char *end = start + strlen((const char *)start);
    while (end > start && isspace(end[-1])) {
        end--;
    }
    const size_t length = (size_t)(end - start);
    if (length == 0 || length >= 5) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (isspace(start[i])) {
            return false;
        }
        out[i] = (char)toupper(start[i]);
    }
    out[length] = '\0';
    return true;
}

bool bus_normalize_direction(const char *source, char *out)
{
    if (out == NULL) {
        return false;
    }
    *out = 0;
    if (source == NULL || source[0] == '\0') {
        return true;
    }
    if (strcasecmp(source, "I") == 0 || strcasecmp(source, "inbound") == 0) {
        *out = BUS_DIR_INBOUND;
        return true;
    }
    if (strcasecmp(source, "O") == 0 || strcasecmp(source, "outbound") == 0) {
        *out = BUS_DIR_OUTBOUND;
        return true;
    }
    return false;
}

bool bus_normalize_service_type(const char *source, bool present,
                                bool default_when_missing, uint8_t *out)
{
    if (out == NULL) {
        return false;
    }
    if (!present) {
        if (!default_when_missing) {
            return false;
        }
        *out = 1;
        return true;
    }
    if (source == NULL || source[0] == '\0') {
        return false;
    }
    char *end = NULL;
    const long value = strtol(source, &end, 10);
    if (end == source || *end != '\0' || value < 1 || value > UINT8_MAX) {
        return false;
    }
    *out = (uint8_t)value;
    return true;
}

bool bus_normalize_text(const char *source, char *out, size_t out_size,
                        bool optional)
{
    if (out == NULL || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    if (source == NULL) {
        return optional;
    }
    const size_t length = strlen(source);
    if (length >= out_size) {
        return false;
    }
    memcpy(out, source, length + 1);
    return true;
}
