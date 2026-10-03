#pragma once

// Public stop-catalog lookup API.
//
// The catalog is provider-qualified: a stop ID is only meaningful together
// with its operator.  The metadata and operator types are shared with the
// service API so callers receive the same representation used by route-stop
// requests and catalog persistence.
#include "bus_service.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Look up a stop in the locally persisted metadata catalog.
 *
 * On ESP_OK, out is populated with a copied metadata record.  ESP_ERR_NOT_FOUND
 * means the catalog has no matching record; ESP_ERR_INVALID_STATE means the
 * matching record is stale.  Invalid arguments return ESP_ERR_INVALID_ARG.
 */
esp_err_t bus_stop_catalog_lookup(bus_operator_t op,
                                  const char *stop_id,
                                  bus_stop_metadata_t *out);

/** Load the newest valid persisted BSC2 catalog generation, if present. */
esp_err_t bus_stop_catalog_init(void);

typedef enum {
    BUS_CATALOG_NONE = 0,
    BUS_CATALOG_LOADING,
    BUS_CATALOG_READY,
    BUS_CATALOG_STALE,
} bus_catalog_status_t;

typedef struct {
    bus_catalog_status_t status;
    uint32_t kmb_record_count;
    uint32_t ctb_record_count;
    time_t kmb_source_time;
    time_t ctb_source_time;
} bus_catalog_info_t;

/** Copy the current local stop-catalog state for UI/status reporting. */
void bus_catalog_get_info(bus_catalog_info_t *out);

/** Return true when at least one provider has usable catalog records loaded. */
bool bus_stop_catalog_ready(void);

/** Return true when the loaded catalog is within its freshness window. */
bool bus_stop_catalog_fresh(void);

#ifdef __cplusplus
}
#endif
