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
/** Returns true once when the bootloader rolled back the attempted image. */
bool crystal_update_did_rollback(void);
/** Clear the attempted-version marker after an image is confirmed. */
void crystal_update_clear_attempted_version(void);

#ifdef __cplusplus
}
#endif
