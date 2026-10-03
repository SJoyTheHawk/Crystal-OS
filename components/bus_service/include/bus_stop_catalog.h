#pragma once

// Public stop-catalog lookup API.
//
// The catalog is provider-qualified: a stop ID is only meaningful together
// with its operator.  The metadata and operator types are shared with the
// service API so callers receive the same representation used by route-stop
// requests and catalog persistence.
#include "bus_service.h"

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

/** Return true when at least one provider has usable catalog records loaded. */
bool bus_stop_catalog_ready(void);

/** Return true when the loaded catalog is within its freshness window. */
bool bus_stop_catalog_fresh(void);

#ifdef __cplusplus
}
#endif
