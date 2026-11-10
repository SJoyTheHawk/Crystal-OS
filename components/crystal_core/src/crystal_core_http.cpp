/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "crystal_core_http.hpp"

#include <atomic>
#include <new>
#include <string.h>

#include "crystal_http.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
constexpr uint32_t kWeatherOwner = 0x57454154; // "WEAT"
constexpr uint32_t kTransportTimeoutMs = 8000;
// Includes queue time. A timeout cancels the request without leaving a
// callback pointing into the caller's stack or reusable weather buffer.
constexpr uint32_t kCompletionTimeoutMs = 30000;
const char *TAG = "crystal_core_http";

struct PendingJson {
    std::atomic<unsigned> references{2}; // caller and completion callback
    SemaphoreHandle_t done = nullptr;
    crystal_http_response_t response = {};
};

void release(PendingJson *pending)
{
    if (pending->references.fetch_sub(1) != 1) return;
    if (pending->response.body != nullptr) {
        crystal_http_response_release(&pending->response);
    }
    vSemaphoreDelete(pending->done);
    delete pending;
}

void completed(const crystal_http_response_t *response, void *context)
{
    auto *pending = static_cast<PendingJson *>(context);
    // The worker's response struct is temporary; retain its body ownership
    // until both callback and caller are done. A late callback after timeout
    // only frees its own context and never writes to the next request.
    pending->response = *response;
    xSemaphoreGive(pending->done);
    release(pending);
}
}

bool crystal_core_http_get_json(const char *url, char *output, size_t capacity)
{
    if (output == nullptr || capacity == 0) return false;
    output[0] = '\0';
    if (url == nullptr || capacity < 2) return false;

    auto *pending = new (std::nothrow) PendingJson;
    if (pending == nullptr) return false;
    pending->done = xSemaphoreCreateBinary();
    if (pending->done == nullptr) {
        delete pending;
        return false;
    }

    crystal_http_options_t options = {};
    options.url = url;
    options.timeout_ms = kTransportTimeoutMs;
    options.max_attempts = 1; // existing weather backoff owns retries
    options.max_body_bytes = capacity - 1;
    options.owner_id = kWeatherOwner;
    const uint32_t id = crystal_http_get(&options, completed, pending);
    if (id == 0) {
        ESP_LOGW(TAG, "JSON request rejected by HTTP queue");
        release(pending); // no callback will arrive
        release(pending);
        return false;
    }
    ESP_LOGI(TAG, "JSON request queued id=%lu; service task waiting", (unsigned long)id);
    if (xSemaphoreTake(pending->done, pdMS_TO_TICKS(kCompletionTimeoutMs)) != pdTRUE) {
        (void)crystal_http_cancel(id);
        ESP_LOGW(TAG, "JSON request timed out id=%lu; cancellation requested", (unsigned long)id);
        release(pending);
        return false;
    }

    const auto &response = pending->response;
    const bool ok = response.transport_error == ESP_OK && response.status_code == 200 &&
                    response.body != nullptr && response.body_len > 0 &&
                    response.body_len < capacity;
    if (ok) {
        memcpy(output, response.body, response.body_len);
        output[response.body_len] = '\0';
    }
    ESP_LOGI(TAG, "JSON request finished id=%lu status=%d bytes=%u error=%s accepted=%d",
             (unsigned long)id, response.status_code, (unsigned)response.body_len,
             esp_err_to_name(response.transport_error), ok);
    release(pending);
    return ok;
}
