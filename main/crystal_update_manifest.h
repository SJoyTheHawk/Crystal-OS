#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UPDATE_MAX_URL_LEN 256
#define UPDATE_MAX_NOTES_LEN 256
#define UPDATE_MAX_URLS 2

typedef struct {
    uint8_t schema;
    char target[64];
    char channel[16];
    char version[16];
    uint32_t size;
    uint8_t sha256[32];
    char urls[UPDATE_MAX_URLS][UPDATE_MAX_URL_LEN];
    uint8_t url_count;
    char notes[UPDATE_MAX_NOTES_LEN];
} update_manifest_t;

esp_err_t crystal_update_parse_manifest(const char *json, size_t json_len,
                                        const char *expected_target,
                                        const char *expected_channel,
                                        const char *current_version,
                                        update_manifest_t *out);

int crystal_update_compare_semver(const char *a, const char *b);

#ifdef __cplusplus
}
#endif
