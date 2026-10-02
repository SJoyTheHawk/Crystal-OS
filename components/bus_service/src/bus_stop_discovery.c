#include "bus_service.h"

#include <string.h>
#include <stdlib.h>

#ifdef CRYSTAL_DISCOVERY_HOST
#include <stdlib.h>
static bus_stop_key_t *alloc_keys(size_t count)
{
    return calloc(count, sizeof(bus_stop_key_t));
}
#else
#include "esp_heap_caps.h"
static bus_stop_key_t *alloc_keys(size_t count)
{
    return heap_caps_calloc(count, sizeof(bus_stop_key_t),
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
#endif

static bus_stop_key_t *s_pending;
static uint16_t s_pending_count;

bool bus_stop_key_equal(const bus_stop_key_t *left,
                        const bus_stop_key_t *right)
{
    return left != NULL && right != NULL && left->op == right->op &&
           strncmp(left->stop_id, right->stop_id, sizeof(left->stop_id)) == 0;
}

void bus_service_stop_discovery_reset(void)
{
    free(s_pending);
    s_pending = NULL;
    s_pending_count = 0;
}

esp_err_t bus_service_discover_route_stops(const bus_stop_t *stops,
                                           uint16_t count)
{
    if (stops == NULL && count != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_pending == NULL) {
        s_pending = alloc_keys(BUS_STOP_DISCOVERY_CAPACITY);
        if (s_pending == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    for (uint16_t i = 0; i < count; i++) {
        if (stops[i].stop_id[0] == '\0' ||
            (stops[i].op != BUS_OP_KMB && stops[i].op != BUS_OP_CTB)) {
            continue;
        }
        bus_stop_key_t candidate = {.op = stops[i].op};
        strncpy(candidate.stop_id, stops[i].stop_id,
                sizeof(candidate.stop_id) - 1);
        bool duplicate = false;
        for (uint16_t j = 0; j < s_pending_count; j++) {
            if (bus_stop_key_equal(&candidate, &s_pending[j])) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        if (s_pending_count >= BUS_STOP_DISCOVERY_CAPACITY) {
            return ESP_ERR_NO_MEM;
        }
        s_pending[s_pending_count++] = candidate;
    }
    return ESP_OK;
}

uint16_t bus_service_stop_discovery_pending(void)
{
    return s_pending_count;
}

uint16_t bus_service_stop_discovery_copy(bus_stop_key_t *out,
                                         uint16_t max_count)
{
    if (out == NULL || max_count == 0) {
        return 0;
    }
    const uint16_t count = s_pending_count < max_count ? s_pending_count : max_count;
    memcpy(out, s_pending, (size_t)count * sizeof(*out));
    return count;
}
