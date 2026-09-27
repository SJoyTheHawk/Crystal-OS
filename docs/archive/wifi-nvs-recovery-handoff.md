# Wi-Fi and NVS Recovery Notes

This records the findings from the Bus app startup investigation and the changes that should remain in the stable tree.

## Observed failures

- Bus app network event registration returned `ESP_ERR_INVALID_STATE` during `onCreate()`. The default ESP event loop was created later by the core service task.
- Wi-Fi initialization reported `wifi osi_nvs_open fail ret=4367` and failed to deinitialize cleanly. In the ESP-IDF version used here, `4367` is `ESP_ERR_NVS_PART_NOT_FOUND`; it is not the `ESP_ERR_NVS_NOT_INITIALIZED` code.
- A later log identified the NVS cause: `Invalid HMAC key ID received!` followed by failure to read or generate encrypted NVS keys (`ESP_ERR_INVALID_ARG`).

These symptoms occurred around Bus app integration, but the event-loop ordering and encrypted NVS metadata are platform concerns. The logs do not establish that rendering inbound/outbound rows caused either failure.

## Source change to keep

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

The later event-loop creation in `service_task()` remains because it tolerates an already created loop. After this change, Bus app registration logged `Network event handler registration: ESP_OK`.

## NVS decision

Do not add a second NVS initialization path in `components/crystal_hal/src/crystal_hal.cpp`. `app_main()` already initializes NVS before HAL storage access and Wi-Fi startup. A retry for `ESP_ERR_NVS_NOT_INITIALIZED` would not repair an invalid encrypted-NVS key ID, and it would not explain error `4367`.

The one-time erase of the `nvs` and `nvs_keys` partitions was the appropriate recovery for the invalid encrypted metadata. Future key-ID failures should be handled as an explicit device recovery/configuration problem, not by silently erasing data during Wi-Fi startup.

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
