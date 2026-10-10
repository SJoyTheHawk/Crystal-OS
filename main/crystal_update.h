#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "crystal_update_view.h"

#ifdef __cplusplus
extern "C" {
#endif

void crystal_update_init(void);
esp_err_t crystal_update_check(void);
esp_err_t crystal_update_start_download(void);
void crystal_update_cancel_check(void);
esp_err_t crystal_update_install(void);
void crystal_update_snapshot(crystal_update_snapshot_t *out);

#ifdef __cplusplus
}
#endif
