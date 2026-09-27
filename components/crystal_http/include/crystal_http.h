/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *url;
    uint32_t timeout_ms;
    uint8_t max_attempts;
    uint32_t retry_backoff_ms;
    uint32_t retry_backoff_max_ms;
    size_t max_body_bytes;
    bool keep_alive;
    uint32_t owner_id;
} crystal_http_options_t;

typedef struct {
    uint32_t request_id;
    int status_code;
    esp_err_t transport_error;
    uint8_t *body;
    size_t body_len;
    uint8_t attempts;
    uint32_t elapsed_ms;
} crystal_http_response_t;

typedef void (*crystal_http_callback_t)(const crystal_http_response_t *response,
                                        void *context);

typedef struct {
    uint32_t queued;
    uint32_t completed;
    uint32_t successful;
    uint32_t transport_failures;
    uint32_t http_failures;
    uint32_t retry_attempts;
    uint32_t cancellations;
    uint32_t current_queue_depth;
    uint32_t peak_body_bytes;
} crystal_http_stats_t;

bool crystal_http_init(void);
bool crystal_http_is_ready(void);

uint32_t crystal_http_get(const crystal_http_options_t *options,
                          crystal_http_callback_t callback,
                          void *context);
bool crystal_http_cancel(uint32_t request_id);
size_t crystal_http_cancel_owner(uint32_t owner_id);
void crystal_http_response_release(const crystal_http_response_t *response);
bool crystal_http_get_stats(crystal_http_stats_t *stats);

#ifdef __cplusplus
}
#endif
