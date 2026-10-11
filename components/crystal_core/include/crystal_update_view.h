#pragma once

#include <stdint.h>
#include <stdbool.h>
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
    UPDATE_STATE_CURRENT,
} crystal_update_state_t;

typedef struct {
    crystal_update_state_t state;
    uint8_t progress_percent;
    char available_version[16];
    char error_message[128];
    char notes[256];
    uint32_t download_size;
    uint32_t downloaded_bytes;
} crystal_update_snapshot_t;

void crystal_update_snapshot(crystal_update_snapshot_t *out);
void crystal_update_clear_attempted_version(void);
esp_err_t crystal_update_check(void);
esp_err_t crystal_update_start_download(void);
void crystal_update_cancel_check(void);
esp_err_t crystal_update_install(void);
bool crystal_update_active(void);
const char *crystal_update_source(void);
bool crystal_ui_post_ota_changed(void);
#ifdef __cplusplus
}
#endif
