#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_provider_ctb.h"

static char *read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) fclose(file);
        return NULL;
    }
    const long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *body = malloc((size_t)size + 1);
    if (body == NULL || fread(body, 1, (size_t)size, file) != (size_t)size) {
        free(body);
        fclose(file);
        return NULL;
    }
    fclose(file);
    body[size] = '\0';
    *length = (size_t)size;
    return body;
}

static bool expect_error(const char *path, esp_err_t expected)
{
    size_t length = 0;
    char *body = read_file(path, &length);
    if (body == NULL) return false;
    bus_stop_t *stops = NULL;
    uint16_t count = 0;
    const esp_err_t result = bus_ctb_parse_route_stops(
        (const uint8_t *)body, length, "969", BUS_DIR_INBOUND, 1, &stops, &count);
    const bool ok = result == expected && stops == NULL && count == 0;
    free(stops);
    free(body);
    return ok;
}

static bool expect_empty(const char *path)
{
    return expect_error(path, ESP_OK);
}

static bool expect_valid(const char *path, uint16_t expected_count)
{
    size_t length = 0;
    char *body = read_file(path, &length);
    if (body == NULL) return false;
    bus_stop_t *stops = NULL;
    uint16_t count = 0;
    const esp_err_t result = bus_ctb_parse_route_stops(
        (const uint8_t *)body, length, "969", BUS_DIR_INBOUND, 1, &stops, &count);
    bool ok = result == ESP_OK && stops != NULL && count == expected_count;
    if (ok) {
        const char *expected_ids[] = {"002536", "002554", "002440"};
        const uint16_t expected_seq[] = {1, 2, 3};
        for (uint16_t i = 0; i < count; i++) {
            if (strcmp(stops[i].stop_id, expected_ids[i]) != 0 ||
                stops[i].seq != expected_seq[i] ||
                stops[i].op != BUS_OP_CTB ||
                stops[i].bound != BUS_DIR_INBOUND ||
                stops[i].service_type != 1 ||
                strcmp(stops[i].route, "969") != 0) {
                ok = false;
                break;
            }
        }
    }
    free(stops);
    free(body);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <fixture-directory>\n", argv[0]);
        return 2;
    }
    char path[512];
#define FIXTURE(name) snprintf(path, sizeof(path), "%s/%s", argv[1], name)

    FIXTURE("ctb_route_stops_valid.json");
    if (!expect_valid(path, 3)) return 1;
    FIXTURE("ctb_route_stops_invalid_record.json");
    if (!expect_valid(path, 1)) return 1;
    FIXTURE("ctb_route_stops_malformed.json");
    if (!expect_error(path, ESP_ERR_INVALID_RESPONSE)) return 1;
    FIXTURE("ctb_route_stops_missing_data.json");
    if (!expect_error(path, ESP_ERR_INVALID_RESPONSE)) return 1;
    FIXTURE("ctb_route_stops_empty.json");
    if (!expect_empty(path)) return 1;

    puts("CTB parser fixtures: 5 passed");
    return 0;
}
