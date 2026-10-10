#include "crystal_update.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "update";
static crystal_update_snapshot_t s_snapshot;

void crystal_update_init(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.state = UPDATE_STATE_IDLE;
    ESP_LOGI(TAG, "Update subsystem initialized (stub)");
}

esp_err_t crystal_update_check(void)
{
    if (s_snapshot.state != UPDATE_STATE_IDLE) {
        ESP_LOGW(TAG, "Update already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Checking for updates (stub)");
    s_snapshot.state = UPDATE_STATE_CHECKING;
    s_snapshot.state = UPDATE_STATE_IDLE;
    (void)strlcpy(s_snapshot.error_message, "Not implemented yet",
                  sizeof(s_snapshot.error_message));
    return ESP_OK;
}

void crystal_update_cancel_check(void)
{
    if (s_snapshot.state == UPDATE_STATE_CHECKING) {
        ESP_LOGI(TAG, "Cancelling update check");
        s_snapshot.state = UPDATE_STATE_IDLE;
    }
}

esp_err_t crystal_update_install(void)
{
    ESP_LOGI(TAG, "Install requested (stub)");
    return ESP_ERR_NOT_SUPPORTED;
}

void crystal_update_snapshot(crystal_update_snapshot_t *out)
{
    if (out != NULL) *out = s_snapshot;
}
