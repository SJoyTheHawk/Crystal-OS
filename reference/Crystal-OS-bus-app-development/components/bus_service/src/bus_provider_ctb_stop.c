#include "bus_provider_ctb_stop.h"
#include "bus_stop_metadata_common.h"

esp_err_t bus_ctb_parse_stop_metadata(const uint8_t *body, size_t len,
                                      const char *requested_stop_id,
                                      bus_stop_metadata_t *out)
{
    return bus_parse_stop_metadata_fields(body, len, requested_stop_id,
                                           BUS_OP_CTB, "stop", "name_en",
                                           "name_tc", "lat", "long", out);
}
