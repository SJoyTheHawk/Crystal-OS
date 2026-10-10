#include "crystal_update_manifest.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "manifest";

static void skip_ws(const char **p, const char *end)
{
    while (*p < end && (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')) (*p)++;
}

static bool parse_uint(const char **p, const char *end, uint32_t *value)
{
    uint64_t result = 0;
    const char *start = *p;
    if (*p < end && **p == '0' && *p + 1 < end && isdigit((unsigned char)(*p)[1])) return false;
    while (*p < end && isdigit((unsigned char)**p)) {
        result = result * 10u + (uint32_t)(**p - '0');
        if (result > UINT32_MAX) return false;
        (*p)++;
    }
    if (*p == start) return false;
    *value = (uint32_t)result;
    return true;
}

static bool parse_string(const char **p, const char *end, char *out, size_t out_len)
{
    if (*p >= end || **p != '"') return false;
    (*p)++;
    size_t used = 0;
    while (*p < end && **p != '"') {
        unsigned char c = (unsigned char)**p;
        if (c < 0x20) return false;
        if (c == '\\') {
            (*p)++;
            if (*p >= end) return false;
            switch (**p) {
                case '"': case '\\': case '/': c = (unsigned char)**p; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                default: return false;
            }
        }
        if (used + 1 >= out_len) return false;
        out[used++] = (char)c;
        (*p)++;
    }
    if (*p >= end || **p != '"') return false;
    (*p)++;
    out[used] = '\0';
    return true;
}

static bool parse_hex_sha256(const char *text, uint8_t out[32])
{
    if (strlen(text) != 64) return false;
    for (size_t i = 0; i < 32; i++) {
        uint8_t value = 0;
        for (size_t j = 0; j < 2; j++) {
            char c = text[i * 2 + j];
            uint8_t nibble;
            if (c >= '0' && c <= '9') nibble = (uint8_t)(c - '0');
            else if (c >= 'a' && c <= 'f') nibble = (uint8_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') nibble = (uint8_t)(c - 'A' + 10);
            else return false;
            value = (uint8_t)((value << 4) | nibble);
        }
        out[i] = value;
    }
    return true;
}

static int parse_semver(const char *text, uint32_t parts[3])
{
    if (!text || !*text) return -2;
    const char *p = text;
    for (int i = 0; i < 3; i++) {
        if (!isdigit((unsigned char)*p)) return -2;
        if (*p == '0' && isdigit((unsigned char)p[1])) return -2;
        uint64_t value = 0;
        while (isdigit((unsigned char)*p)) {
            value = value * 10u + (unsigned)(*p - '0');
            if (value > UINT32_MAX) return -2;
            p++;
        }
        parts[i] = (uint32_t)value;
        if (i < 2) {
            if (*p++ != '.') return -2;
        } else if (*p != '\0') {
            return -2;
        }
    }
    return 0;
}

int crystal_update_compare_semver(const char *a, const char *b)
{
    uint32_t av[3], bv[3];
    if (parse_semver(a, av) != 0 || parse_semver(b, bv) != 0) return -2;
    for (int i = 0; i < 3; i++) {
        if (av[i] != bv[i]) return av[i] > bv[i] ? 1 : -1;
    }
    return 0;
}

esp_err_t crystal_update_parse_manifest(const char *json, size_t json_len,
                                        const char *expected_target,
                                        const char *expected_channel,
                                        const char *current_version,
                                        update_manifest_t *out)
{
    if (!json || !expected_target || !expected_channel || !current_version || !out ||
        json_len == 0 || json_len > 2048) return ESP_ERR_INVALID_ARG;

    const char *end = json + json_len;
    const char *p = json;
    update_manifest_t result = {0};
    char key[32], hash[65];
    unsigned seen = 0;
    skip_ws(&p, end);
    if (p == end || *p++ != '{') goto invalid;
    for (;;) {
        skip_ws(&p, end);
        if (!parse_string(&p, end, key, sizeof(key))) goto invalid;
        static const char *fields[] = {"schema", "target", "channel", "version", "size", "sha256", "urls", "notes"};
        unsigned field;
        for (field = 0; field < 8 && strcmp(key, fields[field]); ++field) {}
        if (field == 8 || (seen & (1u << field))) goto invalid;
        seen |= 1u << field;
        skip_ws(&p, end);
        if (p == end || *p++ != ':') goto invalid;
        skip_ws(&p, end);
        uint32_t number;
        switch (field) {
        case 0:
            if (!parse_uint(&p, end, &number) || number != 1) goto invalid;
            result.schema = 1; break;
        case 1:
            if (!parse_string(&p, end, result.target, sizeof(result.target))) goto invalid;
            break;
        case 2:
            if (!parse_string(&p, end, result.channel, sizeof(result.channel))) goto invalid;
            break;
        case 3:
            if (!parse_string(&p, end, result.version, sizeof(result.version))) goto invalid;
            break;
        case 4:
            if (!parse_uint(&p, end, &result.size) || result.size == 0) goto invalid;
            break;
        case 5:
            if (!parse_string(&p, end, hash, sizeof(hash)) || !parse_hex_sha256(hash, result.sha256)) goto invalid;
            break;
        case 6:
            if (p == end || *p++ != '[') goto invalid;
            for (;;) {
                skip_ws(&p, end);
                if (result.url_count == UPDATE_MAX_URLS) goto invalid;
                char *url = result.urls[result.url_count];
                if (!parse_string(&p, end, url, UPDATE_MAX_URL_LEN) || strncmp(url, "https://", 8) || !url[8]) goto invalid;
                for (const char *u = url; *u; ++u) if ((unsigned char)*u <= 32 || *u == '@' || *u == '#' || *u == '\\') goto invalid;
                ++result.url_count;
                skip_ws(&p, end);
                if (p == end) goto invalid;
                if (*p == ']') { ++p; break; }
                if (*p++ != ',') goto invalid;
            }
            break;
        case 7:
            if (!parse_string(&p, end, result.notes, sizeof(result.notes))) goto invalid;
            break;
        }
        skip_ws(&p, end);
        if (p == end) goto invalid;
        if (*p == '}') { ++p; break; }
        if (*p++ != ',') goto invalid;
    }
    skip_ws(&p, end);
    if (p != end || (seen & 127u) != 127u || strcmp(result.target, expected_target) ||
        strcmp(result.channel, expected_channel)) goto invalid;
    int comparison = crystal_update_compare_semver(result.version, current_version);
    if (comparison == -2) goto invalid;
    if (comparison != 1) {
        memset(out, 0, sizeof(*out));
        return ESP_ERR_NOT_FOUND; // Valid feed, no newer release.
    }
    *out = result;
    ESP_LOGI(TAG, "Manifest valid: %s -> %s", current_version, out->version);
    return ESP_OK;
invalid:
    memset(out, 0, sizeof(*out));
    return ESP_FAIL;
}
