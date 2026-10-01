#include "bus_provider_ctb.h"

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "cJSON.h"

#ifdef CRYSTAL_CTB_PARSER_HOST
static void *ctb_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}
#else
#include "esp_heap_caps.h"
static void *ctb_calloc(size_t count, size_t size)
{
    return heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
#endif

static void ctb_copy(char *destination, size_t destination_size,
                     const char *source)
{
    if (destination == NULL || destination_size == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    const size_t length = strlen(source) < destination_size - 1
        ? strlen(source) : destination_size - 1;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static bool parse_positive_sequence(const cJSON *value, uint16_t *out)
{
    if (value == NULL || out == NULL) {
        return false;
    }

    long sequence = 0;
    if (cJSON_IsNumber(value)) {
        if (value->valuedouble < 1.0 || value->valuedouble > UINT16_MAX ||
            value->valuedouble != (double)(long)value->valuedouble) {
            return false;
        }
        sequence = (long)value->valuedouble;
    } else if (cJSON_IsString(value) && value->valuestring != NULL &&
               value->valuestring[0] != '\0') {
        char *end = NULL;
        sequence = strtol(value->valuestring, &end, 10);
        if (end == value->valuestring || *end != '\0' || sequence < 1 ||
            sequence > UINT16_MAX) {
            return false;
        }
    } else {
        return false;
    }

    *out = (uint16_t)sequence;
    return true;
}

esp_err_t bus_ctb_parse_route_stops(const uint8_t *body,
                                     size_t len,
                                     const char *route,
                                     bus_direction_t bound,
                                     uint8_t service_type,
                                     bus_stop_t **out,
                                     uint16_t *count)
{
    if (body == NULL || len == 0 || route == NULL || route[0] == '\0' ||
        out == NULL || count == NULL ||
        (bound != BUS_DIR_INBOUND && bound != BUS_DIR_OUTBOUND) ||
        service_type == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    *out = NULL;
    *count = 0;

    cJSON *root = cJSON_ParseWithLength((const char *)body, len);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const int item_count = cJSON_GetArraySize(data);
    if (item_count == 0) {
        cJSON_Delete(root);
        return ESP_OK;
    }
    if (item_count < 0 || (unsigned long)item_count > UINT16_MAX) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    bus_stop_t *stops = ctb_calloc((size_t)item_count, sizeof(*stops));
    if (stops == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    uint16_t valid_count = 0;
    for (int i = 0; i < item_count; i++) {
        cJSON *item = cJSON_GetArrayItem(data, i);
        cJSON *co = item != NULL ? cJSON_GetObjectItem(item, "co") : NULL;
        cJSON *item_route = item != NULL ? cJSON_GetObjectItem(item, "route") : NULL;
        cJSON *dir = item != NULL ? cJSON_GetObjectItem(item, "dir") : NULL;
        cJSON *sequence = item != NULL ? cJSON_GetObjectItem(item, "seq") : NULL;
        cJSON *stop_id = item != NULL ? cJSON_GetObjectItem(item, "stop") : NULL;
        uint16_t parsed_sequence = 0;

        if (!cJSON_IsString(co) || strcmp(co->valuestring, "CTB") != 0 ||
            !cJSON_IsString(item_route) || strcmp(item_route->valuestring, route) != 0 ||
            !cJSON_IsString(dir) || dir->valuestring[0] != (char)bound ||
            !cJSON_IsString(stop_id) || stop_id->valuestring == NULL ||
            stop_id->valuestring[0] == '\0' ||
            strlen(stop_id->valuestring) >= sizeof(stops[valid_count].stop_id) ||
            !parse_positive_sequence(sequence, &parsed_sequence)) {
            continue;
        }

        bus_stop_t *stop = &stops[valid_count++];
        ctb_copy(stop->stop_id, sizeof(stop->stop_id), stop_id->valuestring);
        ctb_copy(stop->route, sizeof(stop->route), route);
        stop->seq = parsed_sequence;
        stop->op = BUS_OP_CTB;
        stop->bound = bound;
        stop->service_type = service_type;
        stop->resolved = false;
        snprintf(stop->name_en, sizeof(stop->name_en), "Stop %u",
                 (unsigned)parsed_sequence);
    }

    cJSON_Delete(root);
    if (valid_count == 0) {
        free(stops);
        return ESP_OK;
    }

    *out = stops;
    *count = valid_count;
    return ESP_OK;
}

static bool ctb_route_field(const cJSON *item, const char *name,
                            const char **value)
{
    const cJSON *field = cJSON_GetObjectItem(item, name);
    if (field == NULL) {
        *value = "";
        return true;
    }
    if (!cJSON_IsString(field) || field->valuestring == NULL) {
        return false;
    }
    *value = field->valuestring;
    return true;
}

static bool ctb_route_direction(const cJSON *value, char *bound, bool *present)
{
    if (bound == NULL || present == NULL) {
        return false;
    }
    *present = value != NULL;
    if (value == NULL) {
        return true;
    }
    if (!cJSON_IsString(value) || value->valuestring == NULL) {
        return false;
    }
    if (strcasecmp(value->valuestring, "I") == 0 ||
        strcasecmp(value->valuestring, "inbound") == 0) {
        *bound = BUS_DIR_INBOUND;
        return true;
    }
    if (strcasecmp(value->valuestring, "O") == 0 ||
        strcasecmp(value->valuestring, "outbound") == 0) {
        *bound = BUS_DIR_OUTBOUND;
        return true;
    }
    return false;
}

static bool ctb_route_service_type(const cJSON *value, uint8_t *service_type)
{
    if (service_type == NULL) {
        return false;
    }
    if (value == NULL) {
        *service_type = 1;
        return true;
    }
    long parsed = 0;
    if (cJSON_IsNumber(value)) {
        if (value->valuedouble < 1.0 || value->valuedouble > UINT8_MAX ||
            value->valuedouble != (double)(long)value->valuedouble) {
            return false;
        }
        parsed = (long)value->valuedouble;
    } else if (cJSON_IsString(value) && value->valuestring != NULL &&
               value->valuestring[0] != '\0') {
        char *end = NULL;
        parsed = strtol(value->valuestring, &end, 10);
        if (end == value->valuestring || *end != '\0' || parsed < 1 ||
            parsed > UINT8_MAX) {
            return false;
        }
    } else {
        return false;
    }
    *service_type = (uint8_t)parsed;
    return true;
}

static bool ctb_variant_has_terminals(const bus_route_variant_t *variant)
{
    return variant != NULL &&
           (variant->orig_en[0] != '\0' || variant->dest_en[0] != '\0' ||
            variant->orig_tc[0] != '\0' || variant->dest_tc[0] != '\0');
}

static bool ctb_variant_append(bus_route_variant_t *variants, uint16_t *count,
                               uint16_t capacity,
                               const bus_route_variant_t *candidate)
{
    if (variants == NULL || count == NULL || candidate == NULL ||
        *count > capacity || candidate->route[0] == '\0' ||
        (candidate->bound != BUS_DIR_INBOUND &&
         candidate->bound != BUS_DIR_OUTBOUND) || candidate->service_type == 0) {
        return false;
    }
    for (uint16_t i = 0; i < *count; i++) {
        bus_route_variant_t *existing = &variants[i];
        if (strcmp(existing->route, candidate->route) == 0 &&
            existing->op == candidate->op &&
            existing->bound == candidate->bound &&
            existing->service_type == candidate->service_type) {
            // Preserve the first record unless a later record is the first
            // explicit terminal mapping for a synthetic empty identity.
            if (!ctb_variant_has_terminals(existing) &&
                ctb_variant_has_terminals(candidate)) {
                *existing = *candidate;
            }
            return false;
        }
    }
    if (*count >= capacity) {
        return false;
    }
    variants[(*count)++] = *candidate;
    return true;
}

esp_err_t bus_ctb_parse_route_variants(const uint8_t *body,
                                       size_t len,
                                       const char *route_filter,
                                       bus_route_variant_t **out,
                                       uint16_t *count)
{
    if (body == NULL || len == 0 || out == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    *count = 0;

    cJSON *root = cJSON_ParseWithLength((const char *)body, len);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    const int item_count = cJSON_GetArraySize(data);
    if (item_count < 0 || (unsigned long)item_count > UINT16_MAX / 2u) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (item_count == 0) {
        cJSON_Delete(root);
        return ESP_OK;
    }

    bus_route_variant_t *variants = ctb_calloc((size_t)item_count * 2u,
                                               sizeof(*variants));
    if (variants == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    char normalized_filter[sizeof(variants[0].route)] = {0};
    if (route_filter != NULL && route_filter[0] != '\0') {
        if (strlen(route_filter) >= sizeof(normalized_filter)) {
            free(variants);
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t i = 0; route_filter[i] != '\0'; i++) {
            normalized_filter[i] =
                (char)toupper((unsigned char)route_filter[i]);
        }
    }

    uint16_t valid_count = 0;
    const uint16_t capacity = (uint16_t)((size_t)item_count * 2u);
    for (int i = 0; i < item_count; i++) {
        cJSON *item = cJSON_GetArrayItem(data, i);
        if (!cJSON_IsObject(item)) {
            continue;
        }
        cJSON *co = cJSON_GetObjectItem(item, "co");
        cJSON *route = cJSON_GetObjectItem(item, "route");
        if (!cJSON_IsString(co) || strcmp(co->valuestring, "CTB") != 0 ||
            !cJSON_IsString(route) || route->valuestring == NULL ||
            route->valuestring[0] == '\0' ||
            strlen(route->valuestring) >= sizeof(variants[0].route)) {
            continue;
        }

        char normalized_route[sizeof(variants[0].route)] = {0};
        for (size_t j = 0; route->valuestring[j] != '\0'; j++) {
            normalized_route[j] =
                (char)toupper((unsigned char)route->valuestring[j]);
        }
        if (normalized_filter[0] != '\0' &&
            strcmp(normalized_filter, normalized_route) != 0) {
            continue;
        }

        char bound = 0;
        bool bound_present = false;
        if (!ctb_route_direction(cJSON_GetObjectItem(item, "bound"),
                                 &bound, &bound_present) ||
            (!bound_present &&
             !ctb_route_direction(cJSON_GetObjectItem(item, "dir"),
                                  &bound, &bound_present))) {
            continue;
        }
        uint8_t service_type = 1;
        if (!ctb_route_service_type(cJSON_GetObjectItem(item, "service_type"),
                                    &service_type)) {
            continue;
        }

        const char *orig_en = NULL;
        const char *orig_tc = NULL;
        const char *dest_en = NULL;
        const char *dest_tc = NULL;
        if (!ctb_route_field(item, "orig_en", &orig_en) ||
            !ctb_route_field(item, "orig_tc", &orig_tc) ||
            !ctb_route_field(item, "dest_en", &dest_en) ||
            !ctb_route_field(item, "dest_tc", &dest_tc)) {
            continue;
        }

        const char directions[2] = {BUS_DIR_INBOUND, BUS_DIR_OUTBOUND};
        const uint8_t direction_count = bound_present ? 1u : 2u;
        for (uint8_t direction_index = 0; direction_index < direction_count;
             direction_index++) {
            bus_route_variant_t candidate = {0};
            ctb_copy(candidate.route, sizeof(candidate.route), normalized_route);
            candidate.op = BUS_OP_CTB;
            candidate.bound = bound_present ? bound : directions[direction_index];
            candidate.service_type = service_type;
            if (bound_present) {
                ctb_copy(candidate.orig_en, sizeof(candidate.orig_en), orig_en);
                ctb_copy(candidate.dest_en, sizeof(candidate.dest_en), dest_en);
                ctb_copy(candidate.orig_tc, sizeof(candidate.orig_tc), orig_tc);
                ctb_copy(candidate.dest_tc, sizeof(candidate.dest_tc), dest_tc);
            }
            (void)ctb_variant_append(variants, &valid_count, capacity,
                                     &candidate);
        }
    }

    cJSON_Delete(root);
    if (valid_count == 0) {
        free(variants);
        return ESP_ERR_INVALID_RESPONSE;
    }
    *out = variants;
    *count = valid_count;
    return ESP_OK;
}
