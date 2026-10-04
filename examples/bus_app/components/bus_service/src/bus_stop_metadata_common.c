#include "bus_stop_metadata_common.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

static bool copy_text(const cJSON *value, char *out, size_t out_size)
{
    out[0] = '\0';
    if (value == NULL) {
        return true;
    }
    if (!cJSON_IsString(value) || value->valuestring == NULL ||
        strlen(value->valuestring) >= out_size) {
        return false;
    }
    strcpy(out, value->valuestring);
    return true;
}

static bool number_value(const cJSON *value, double *out)
{
    if (value == NULL || out == NULL) {
        return false;
    }
    if (cJSON_IsNumber(value)) {
        *out = value->valuedouble;
        return isfinite(*out);
    }
    if (cJSON_IsString(value) && value->valuestring != NULL &&
        value->valuestring[0] != '\0') {
        char *end = NULL;
        *out = strtod(value->valuestring, &end);
        return end != value->valuestring && *end == '\0' && isfinite(*out);
    }
    return false;
}

static const cJSON *record_from_root(const cJSON *root,
                                     const char *stop_field,
                                     const char *requested_stop_id)
{
    const cJSON *data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsObject(data)) {
        return data;
    }
    if (cJSON_IsArray(data) && cJSON_GetArraySize(data) > 0) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, data) {
            const cJSON *stop = cJSON_GetObjectItem(item, stop_field);
            if (cJSON_IsString(stop) && stop->valuestring != NULL &&
                strcmp(stop->valuestring, requested_stop_id) == 0) {
                return item;
            }
        }
        return NULL;
    }
    const cJSON *features = cJSON_GetObjectItem(root, "features");
    if (cJSON_IsArray(features) && cJSON_GetArraySize(features) > 0) {
        cJSON *feature = NULL;
        cJSON_ArrayForEach(feature, features) {
            const cJSON *properties = cJSON_GetObjectItem(feature, "properties");
            const cJSON *candidate = cJSON_IsObject(properties) ? properties : feature;
            const cJSON *stop = cJSON_GetObjectItem(candidate, stop_field);
            if (cJSON_IsString(stop) && stop->valuestring != NULL &&
                strcmp(stop->valuestring, requested_stop_id) == 0) {
                return feature;
            }
        }
    }
    return NULL;
}

esp_err_t bus_parse_stop_metadata_fields(const uint8_t *body, size_t len,
                                         const char *requested_stop_id,
                                         bus_operator_t op,
                                         const char *stop_field,
                                         const char *name_en_field,
                                         const char *name_tc_field,
                                         const char *lat_field,
                                         const char *lon_field,
                                         bus_stop_metadata_t *out)
{
    if (body == NULL || len == 0 || requested_stop_id == NULL ||
        requested_stop_id[0] == '\0' || out == NULL || stop_field == NULL ||
        name_en_field == NULL || name_tc_field == NULL || lat_field == NULL ||
        lon_field == NULL || strlen(requested_stop_id) >= sizeof(out->stop_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_ParseWithLength((const char *)body, len);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const cJSON *item = record_from_root(root, stop_field, requested_stop_id);
    const cJSON *fields = item != NULL ? cJSON_GetObjectItem(item, "properties") : NULL;
    if (!cJSON_IsObject(fields)) {
        fields = item;
    }
    const cJSON *stop = fields != NULL ? cJSON_GetObjectItem(fields, stop_field) : NULL;
    if (item == NULL || !cJSON_IsObject(fields) || stop == NULL ||
        !cJSON_IsString(stop) || stop->valuestring == NULL ||
        strcmp(stop->valuestring, requested_stop_id) != 0) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }
    if (!copy_text(cJSON_GetObjectItem(fields, name_en_field), out->name_en,
                   sizeof(out->name_en)) ||
        !copy_text(cJSON_GetObjectItem(fields, name_tc_field), out->name_tc,
                   sizeof(out->name_tc))) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    out->op = op;
    strcpy(out->stop_id, requested_stop_id);
    double lat = 0.0;
    double lon = 0.0;
    bool valid_coordinates =
        number_value(cJSON_GetObjectItem(fields, lat_field), &lat) &&
        number_value(cJSON_GetObjectItem(fields, lon_field), &lon) &&
        lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
    if (!valid_coordinates) {
        const cJSON *geometry = cJSON_GetObjectItem(item, "geometry");
        const cJSON *coordinates = geometry != NULL
            ? cJSON_GetObjectItem(geometry, "coordinates") : NULL;
        if (cJSON_IsArray(coordinates) && cJSON_GetArraySize(coordinates) >= 2 &&
            number_value(cJSON_GetArrayItem(coordinates, 0), &lon) &&
            number_value(cJSON_GetArrayItem(coordinates, 1), &lat) &&
            lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0) {
            valid_coordinates = true;
        }
    }
    if (valid_coordinates) {
        out->lat = (float)lat;
        out->lon = (float)lon;
        out->has_coordinates = isfinite(out->lat) && isfinite(out->lon);
    }
    out->resolved = true;
    cJSON_Delete(root);
    return ESP_OK;
}
