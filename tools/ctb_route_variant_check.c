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

static bool check_fixture(const char *directory, const char *name,
                          esp_err_t expected_status, uint16_t expected_count,
                          bool (*verify)(const bus_route_variant_t *, uint16_t))
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    size_t length = 0;
    char *body = read_file(path, &length);
    if (body == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return false;
    }
    bus_route_variant_t *variants = NULL;
    uint16_t count = 0;
    const esp_err_t status = bus_ctb_parse_route_variants(
        (const uint8_t *)body, length, NULL, &variants, &count);
    const bool ok = status == expected_status && count == expected_count &&
        ((expected_count == 0 && variants == NULL) ||
         (expected_count > 0 && variants != NULL)) &&
        (verify == NULL || verify(variants, count));
    if (!ok) {
        fprintf(stderr, "%s: status=%d count=%u expected=%d/%u\n",
                name, status, count, expected_status, expected_count);
    }
    free(variants);
    free(body);
    return ok;
}

static bool valid_variants(const bus_route_variant_t *variants, uint16_t count)
{
    return count == 2 &&
        strcmp(variants[0].route, "969") == 0 &&
        variants[0].op == BUS_OP_CTB && variants[0].bound == BUS_DIR_INBOUND &&
        variants[0].service_type == 1 &&
        strcmp(variants[0].orig_en, "Sha Tin") == 0 &&
        strcmp(variants[0].dest_en, "Causeway Bay") == 0 &&
        strcmp(variants[0].orig_tc, "沙田") == 0 &&
        strcmp(variants[0].dest_tc, "銅鑼灣") == 0 &&
        strcmp(variants[1].route, "969") == 0 &&
        variants[1].op == BUS_OP_CTB && variants[1].bound == BUS_DIR_OUTBOUND &&
        variants[1].service_type == 1 &&
        strcmp(variants[1].orig_en, "Causeway Bay") == 0 &&
        strcmp(variants[1].dest_en, "Sha Tin") == 0 &&
        strcmp(variants[1].orig_tc, "銅鑼灣") == 0 &&
        strcmp(variants[1].dest_tc, "沙田") == 0;
}

static bool schema_variants(const bus_route_variant_t *variants, uint16_t count)
{
    return count == 2 && variants[0].bound == BUS_DIR_INBOUND &&
        variants[1].bound == BUS_DIR_OUTBOUND &&
        variants[0].op == BUS_OP_CTB && variants[1].op == BUS_OP_CTB &&
        variants[0].service_type == 1 && variants[1].service_type == 1 &&
        variants[0].dest_en[0] == '\0' && variants[1].dest_en[0] == '\0' &&
        variants[0].dest_tc[0] == '\0' && variants[1].dest_tc[0] == '\0';
}

static bool invalid_variants(const bus_route_variant_t *variants, uint16_t count)
{
    return count == 1 && variants[0].bound == BUS_DIR_INBOUND &&
        strcmp(variants[0].dest_en, "Causeway Bay") == 0;
}

static bool oversized_variants(const bus_route_variant_t *variants, uint16_t count)
{
    return count == 1 && variants[0].bound == BUS_DIR_OUTBOUND &&
        strlen(variants[0].orig_en) == sizeof(variants[0].orig_en) - 1 &&
        strlen(variants[0].orig_tc) == sizeof(variants[0].orig_tc) - 1 &&
        strlen(variants[0].dest_en) == sizeof(variants[0].dest_en) - 1 &&
        strlen(variants[0].dest_tc) == sizeof(variants[0].dest_tc) - 1;
}

static bool duplicate_variants(const bus_route_variant_t *variants, uint16_t count)
{
    return count == 1 && strcmp(variants[0].orig_en, "First origin") == 0 &&
        strcmp(variants[0].dest_en, "First destination") == 0;
}

static bool check_route_filter(const char *directory)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory,
             "ctb_route_variants_valid.json");
    size_t length = 0;
    char *body = read_file(path, &length);
    if (body == NULL) return false;
    bus_route_variant_t *variants = NULL;
    uint16_t count = 0;
    const esp_err_t status = bus_ctb_parse_route_variants(
        (const uint8_t *)body, length, "10", &variants, &count);
    const bool ok = status == ESP_ERR_INVALID_RESPONSE && variants == NULL &&
        count == 0;
    free(variants);
    free(body);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <fixture-directory>\n", argv[0]);
        return 2;
    }
    const struct {
        const char *name;
        esp_err_t status;
        uint16_t count;
        bool (*verify)(const bus_route_variant_t *, uint16_t);
    } cases[] = {
        {"ctb_route_variants_valid.json", ESP_OK, 2, valid_variants},
        {"ctb_route_variants_schema.json", ESP_OK, 2, schema_variants},
        {"ctb_route_variants_invalid_record.json", ESP_OK, 1, invalid_variants},
        {"ctb_route_variants_oversized.json", ESP_OK, 1, oversized_variants},
        {"ctb_route_variants_duplicate.json", ESP_OK, 1, duplicate_variants},
        {"ctb_route_variants_empty.json", ESP_OK, 0, NULL},
        {"ctb_route_variants_malformed.json", ESP_ERR_INVALID_RESPONSE, 0, NULL},
        {"ctb_route_variants_missing_data.json", ESP_ERR_INVALID_RESPONSE, 0, NULL},
        {"ctb_route_variants_nonarray_data.json", ESP_ERR_INVALID_RESPONSE, 0, NULL},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (!check_fixture(argv[1], cases[i].name, cases[i].status,
                           cases[i].count, cases[i].verify)) {
            return 1;
        }
    }
    if (!check_route_filter(argv[1])) {
        fprintf(stderr, "route filter identity check failed\n");
        return 1;
    }
    puts("CTB route variant parser fixtures: 10 passed");
    return 0;
}
