/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_memory.h"
#include "cJSON.h"
#include "esp_log.h"

void crystal_memory_init(void)
{
    cJSON_Hooks hooks = {crystal_ui_malloc, crystal_ui_free};
    cJSON_InitHooks(&hooks);
    ESP_LOGI("crystal_memory", "UI and JSON allocators use PSRAM; task stacks remain internal");
}
