#include "crystal_crash.h"

#include <string.h>

#include "esp_core_dump.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"

static const char *TAG = "crash";
static const char *const kNamespace = "crash";
static const char *const kSummaryKey = "summary";
static const char *const kNotifyKey = "notify";

static crash_summary_t s_last_crash;
static bool s_has_crash;
static bool s_should_notify;

static void load_saved_summary(void)
{
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return;

    size_t size = sizeof(s_last_crash);
    if (nvs_get_blob(handle, kSummaryKey, &s_last_crash, &size) == ESP_OK &&
            size == sizeof(s_last_crash) && s_last_crash.version == 1) {
        s_has_crash = true;
    }
    uint8_t notify = 0;
    if (nvs_get_u8(handle, kNotifyKey, &notify) == ESP_OK) s_should_notify = notify != 0;
    nvs_close(handle);
}

static bool save_summary(const crash_summary_t *summary)
{
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(handle, kSummaryKey, summary, sizeof(*summary));
    if (err == ESP_OK) err = nvs_set_u8(handle, kNotifyKey, 1);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

void crystal_crash_init(void)
{
    esp_core_dump_summary_t core = {0};
    const esp_err_t dump_err = esp_core_dump_get_summary(&core);
    if (dump_err != ESP_OK) {
        load_saved_summary();
        ESP_LOGI(TAG, "No coredump found");
        return;
    }

    memset(&s_last_crash, 0, sizeof(s_last_crash));
    s_last_crash.version = 1;
    s_last_crash.reset_reason = (uint8_t)esp_reset_reason();
    s_last_crash.pc = core.exc_pc;
    size_t depth = core.exc_bt_info.depth;
    if (depth > CRASH_BACKTRACE_DEPTH) depth = CRASH_BACKTRACE_DEPTH;
    memcpy(s_last_crash.backtrace, core.exc_bt_info.bt, depth * sizeof(uint32_t));
    memcpy(s_last_crash.elf_sha, core.app_elf_sha256, sizeof(s_last_crash.elf_sha));
    memcpy(s_last_crash.task_name, core.exc_task, sizeof(s_last_crash.task_name));
    s_last_crash.task_name[sizeof(s_last_crash.task_name) - 1] = '\0';
    s_last_crash.is_corrupted = core.exc_bt_info.corrupted;
    s_has_crash = save_summary(&s_last_crash);
    s_should_notify = s_has_crash;
    ESP_LOGI(TAG, "Coredump saved: task=%s pc=0x%08lx corrupted=%s",
             s_last_crash.task_name, (unsigned long)s_last_crash.pc,
             s_last_crash.is_corrupted ? "yes" : "no");
    if (s_has_crash) (void)esp_core_dump_image_erase();
}

bool crystal_crash_get_last(crash_summary_t *out)
{
    if (!s_has_crash || out == NULL) return false;
    *out = s_last_crash;
    return true;
}

void crystal_crash_mark_displayed(void)
{
    if (!s_should_notify) return;
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) == ESP_OK) {
        if (nvs_set_u8(handle, kNotifyKey, 0) == ESP_OK) (void)nvs_commit(handle);
        nvs_close(handle);
    }
    s_should_notify = false;
}

bool crystal_crash_should_notify(void)
{
    return s_should_notify;
}
