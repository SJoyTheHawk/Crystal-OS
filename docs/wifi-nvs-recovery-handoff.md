# Wi-Fi and NVS Recovery Handoff

Copy this document outside the repository before reverting the worktree. It records the changes made during the Bus app startup investigation so they can be reviewed and reapplied independently.

## Observed failures

- Bus app network event registration returned `ESP_ERR_INVALID_STATE` during `onCreate()`. The default ESP event loop was created later by the core service task.
- Wi-Fi initialization reported `wifi osi_nvs_open fail ret=4367` and failed to deinitialize cleanly.
- A later log identified the NVS cause: `Invalid HMAC key ID received!` followed by failure to read or generate encrypted NVS keys (`ESP_ERR_INVALID_ARG`).

These symptoms occurred around Bus app integration, but the event-loop ordering and encrypted NVS metadata are platform concerns. The logs do not establish that rendering inbound/outbound rows caused either failure.

## Source changes to review after the revert

### 1. Create the default event loop before apps run

File: `components/crystal_core/src/crystal_core.cpp`, in `crystal_core_init()` before app registration and `onCreate()` can occur.

```cpp
const esp_err_t event_loop_err = esp_event_loop_create_default();
if (event_loop_err != ESP_OK && event_loop_err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "failed to create default event loop: %s",
             esp_err_to_name(event_loop_err));
    return false;
}
```

The later event-loop creation in `service_task()` can remain because it tolerates an already created loop. After this change, Bus app registration logged `Network event handler registration: ESP_OK`.

### 2. Handle unavailable NVS in storage access

File: `components/crystal_hal/src/crystal_hal.cpp`.

Add `#include "nvs_flash.h"`. The current working tree has these helpers:

```cpp
static bool recover_nvs()
{
    const esp_err_t err = nvs_flash_init();
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        return true;
    }
    ESP_LOGE(TAG, "NVS initialization failed during recovery: %s", esp_err_to_name(err));
    return false;
}

static esp_err_t open_storage(nvs_open_mode_t mode, nvs_handle_t *handle)
{
    esp_err_t err = nvs_open(kStorageNamespace, mode, handle);
    if (err == ESP_ERR_NVS_NOT_INITIALIZED && recover_nvs()) {
        err = nvs_open(kStorageNamespace, mode, handle);
    }
    return err;
}
```

Replace direct `nvs_open(kStorageNamespace, ...)` calls with `open_storage(...)` in `DeviceStorage::get`, `DeviceStorage::set`, `DeviceStorage::erase`, `DeviceWifi::read_enabled`, and `DeviceWifi::write_enabled`.

At the start of `DeviceWifi::start()`, after its `started_` guard, the current change checks NVS before continuing:

```cpp
if (!recover_nvs()) {
    ESP_LOGE(TAG, "Wi-Fi start aborted because NVS is unavailable");
    return;
}
```

Review whether `nvs_flash_init()` should be centralized instead of called again from Wi-Fi startup. This guard detects failure; it cannot repair an invalid HMAC key ID by itself.

## One-time device recovery already performed

The encrypted NVS key metadata was invalid. With user authorization, only these adjacent partitions were erased on the device:

| Partition | Offset | Length |
| --- | ---: | ---: |
| `nvs` | `0x9000` | `0x6000` |
| `nvs_keys` | `0xf000` | `0x1000` |

The command was `python -m esptool --chip esp32s3 --port /dev/cu.usbmodem101 erase-region 0x9000 0x7000`. The route cache in the separate `storage` partition at `0xa30000` was preserved. This erase cleared saved Wi-Fi credentials and NVS preferences; the user reconfigured Wi-Fi afterward.

The following boot log showed `NVS partition "nvs" is encrypted.`, normal Wi-Fi driver startup, an IP address (`192.168.1.193`), SNTP synchronization, and successful NVS writes. It did not show `Invalid HMAC key ID` or `wifi osi_nvs_open fail`. This confirms the device recovery worked; it does not isolate which source change, if any, was necessary for NVS initialization.

## Separate route download settings

The route HTTP settings in `components/bus_service/src/bus_service.c` are `BUS_HTTP_TIMEOUT_MS 15000` and `BUS_ROUTE_FETCH_ATTEMPTS 10`. They control API fetching and are independent of Wi-Fi driver startup and NVS recovery. Review them separately if the revert removes them.
