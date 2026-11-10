// Capture-only adapter. Persistent NVS clearing is not available on the host.
#pragma once
#include <cstdint>
using esp_err_t = int;
using nvs_handle_t = unsigned;
using nvs_iterator_t = void *;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NVS_NOT_FOUND = 1;
constexpr int NVS_READWRITE = 1, NVS_TYPE_ANY = 255;
struct nvs_entry_info_t { char key[16]; };
inline const char *esp_err_to_name(esp_err_t) { return "NVS unavailable in capture runner"; }
inline esp_err_t nvs_open(const char *, int, nvs_handle_t *) { return ESP_FAIL; }
inline void nvs_close(nvs_handle_t) {}
inline esp_err_t nvs_entry_find_in_handle(nvs_handle_t, int, nvs_iterator_t *) { return ESP_FAIL; }
inline esp_err_t nvs_entry_info(nvs_iterator_t, nvs_entry_info_t *) { return ESP_FAIL; }
inline esp_err_t nvs_entry_next(nvs_iterator_t *) { return ESP_FAIL; }
inline void nvs_release_iterator(nvs_iterator_t) {}
inline esp_err_t nvs_erase_key(nvs_handle_t, const char *) { return ESP_FAIL; }
inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_FAIL; }
