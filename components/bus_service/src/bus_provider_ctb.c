#include "bus_provider_ctb.h"

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
