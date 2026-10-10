#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UPDATE_STATE_IDLE,
    UPDATE_STATE_CHECKING,
    UPDATE_STATE_AVAILABLE,
    UPDATE_STATE_DOWNLOADING,
    UPDATE_STATE_READY,
    UPDATE_STATE_INSTALLING,
    UPDATE_STATE_FAILED,
} crystal_update_state_t;

typedef struct {
    crystal_update_state_t state;
    uint8_t progress_percent;
    char available_version[16];
    char error_message[128];
    uint32_t download_size;
    uint32_t downloaded_bytes;
} crystal_update_snapshot_t;

void crystal_update_init(void);
esp_err_t crystal_update_check(void);
void crystal_update_cancel_check(void);
esp_err_t crystal_update_install(void);
void crystal_update_snapshot(crystal_update_snapshot_t *out);

#ifdef __cplusplus
}
#endif
