#!/usr/bin/env python3
"""Run the production HTTP worker on the host with a scripted IDF transport.

This checks retry/cleanup policy, not real TLS, Wi-Fi, or device heap capacity.
Run: python3 tools/test_crystal_http_retry.py
"""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

MOCK = r'''
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 257
#define ESP_ERR_INVALID_STATE 259
#define ESP_ERR_INVALID_SIZE 260
#define ESP_ERR_NOT_SUPPORTED 262
#define ESP_ERR_HTTP_CONNECT 28674
#define MBEDTLS_ERR_SSL_ALLOC_FAILED -141
#define CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC 1
#define CONFIG_MBEDTLS_HARDWARE_AES 0
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_SPIRAM 2
#define MALLOC_CAP_8BIT 4
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(n) (n)
typedef unsigned TickType_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
static void mock_log(const char *tag, const char *fmt, ...) {(void)tag;(void)fmt;}
#define ESP_LOGI mock_log
#define ESP_LOGW mock_log
static const char *esp_err_to_name(int n) {(void)n;return "mock";}
static unsigned ticks, opened, cleaned, callbacks, offset;
static bool live;
static int failures[3], tls_codes[3];
static unsigned body_size = 3026;
static size_t heap_caps_get_free_size(int caps) {
    return caps & MALLOC_CAP_SPIRAM ? 3000000 : (opened == 1 ? 4000 : 16000);
}
static size_t heap_caps_get_largest_free_block(int caps) {return heap_caps_get_free_size(caps);}
static void *heap_caps_malloc(size_t n, int caps) {(void)caps;return malloc(n);}
static void *heap_caps_realloc(void *p, size_t n, int caps) {(void)caps;return realloc(p,n);}
static int64_t esp_timer_get_time(void) {return ticks * 1000;}
static TickType_t xTaskGetTickCount(void) {return ticks;}
static void vTaskDelay(unsigned n) {ticks += n;}
static int xPortGetCoreID(void) {return 0;}
static void vTaskDelete(void *p) {(void)p;}
static int xTaskCreatePinnedToCore(void (*f)(void *),const char *n,unsigned s,void *a,int p,TaskHandle_t *h,int c)
{(void)f;(void)n;(void)s;(void)a;(void)p;(void)h;(void)c;return pdPASS;}
static void *xQueueCreate(unsigned n,size_t s) {(void)n;(void)s;return (void *)1;}
static void vQueueDelete(void *p) {(void)p;}
static int xQueueReceive(void *q,void *p,unsigned t) {(void)q;(void)p;(void)t;return 0;}
static int xQueueSend(void *q,const void *p,unsigned t) {(void)q;(void)p;(void)t;return pdTRUE;}
static unsigned uxQueueMessagesWaiting(void *q) {(void)q;return 0;}
static void *xSemaphoreCreateMutex(void) {return (void *)1;}
static int xSemaphoreTake(void *s,unsigned t) {(void)s;(void)t;return pdTRUE;}
static void xSemaphoreGive(void *s) {(void)s;}
static void vSemaphoreDelete(void *s) {(void)s;}
static int esp_crt_bundle_attach(void *p) {(void)p;return 0;}
typedef void *esp_http_client_handle_t;
typedef struct {
    const char *url;
    unsigned timeout_ms;
    bool keep_alive_enable;
    int (*crt_bundle_attach)(void *);
    int buffer_size, buffer_size_tx;
} esp_http_client_config_t;
static void *esp_http_client_init(const esp_http_client_config_t *c) {
    assert(!live);
    assert(c->buffer_size == 1024 && c->buffer_size_tx == 1024);
    live = true;
    offset = 0;
    return (void *)1;
}
static int esp_http_client_open(void *c,int n) {
    (void)c;(void)n;assert(opened < 3);return failures[opened++];
}
static int64_t esp_http_client_fetch_headers(void *c) {(void)c;return body_size;}
static int esp_http_client_get_status_code(void *c) {(void)c;return 200;}
static int esp_http_client_read(void *c,char *b,int n) {
    (void)c;
    unsigned count = body_size - offset;
    if (count > (unsigned)n) count = n;
    if (count > 1024) count = 1024;
    memset(b,'x',count);offset += count;return count;
}
static int esp_http_client_get_errno(void *c) {(void)c;assert(live);return 0;}
static int esp_http_client_get_and_clear_last_tls_error(void *c,int *code,int *flags) {
    (void)c;assert(live);*code = tls_codes[opened-1];*flags = 0;return *code ? 1234 : 0;
}
static int esp_http_client_close(void *c) {(void)c;assert(live);return 0;}
static int esp_http_client_cleanup(void *c) {(void)c;assert(live);live=false;cleaned++;return 0;}
'''

CHECK = r'''
static crystal_http_response_t result;
static void completed(const crystal_http_response_t *r, void *context) {
    (void)context;
    assert(!live && cleaned == opened);
    result = *r;
    callbacks++;
}
static request_slot_t fresh(void) {
    ticks = opened = cleaned = callbacks = 0;
    live = false;
    body_size = 3026;
    memset(failures, 0, sizeof(failures));
    memset(tls_codes, 0, sizeof(tls_codes));
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.min_tls_internal_free = UINT32_MAX;
    s_stats.min_tls_largest_internal = UINT32_MAX;
    s_stats.min_tls_psram_free = UINT32_MAX;
    return (request_slot_t){.request_id=1, .url="https://example.test/route-stop",
        .callback=completed, .max_attempts=3, .max_body_bytes=4096};
}
int main(void) {
    request_slot_t slot = fresh();
    failures[0] = ESP_ERR_HTTP_CONNECT;
    deliver_request(&slot);
    assert(result.transport_error == ESP_OK && result.attempts == 2);
    assert(result.body_len == 3026 && callbacks == 1 && cleaned == 2);
    assert(s_stats.min_tls_internal_free == 4000);
    crystal_http_response_release(&result);

    slot = fresh();
    failures[0] = ESP_ERR_HTTP_CONNECT;
    tls_codes[0] = MBEDTLS_ERR_SSL_ALLOC_FAILED;
    deliver_request(&slot);
    assert(result.transport_error == ESP_ERR_NO_MEM && result.attempts == 1);
    assert(callbacks == 1 && cleaned == 1 && s_stats.retry_attempts == 0);

    slot = fresh();
    failures[0] = ESP_ERR_HTTP_CONNECT;
    tls_codes[0] = -0x7f80;
    deliver_request(&slot);
    assert(result.transport_error == ESP_OK && result.attempts == 2);
    crystal_http_response_release(&result);

    slot = fresh();
    failures[0] = failures[1] = failures[2] = ESP_ERR_HTTP_CONNECT;
    deliver_request(&slot);
    assert(result.transport_error == ESP_ERR_HTTP_CONNECT && result.attempts == 3);
    assert(callbacks == 1 && cleaned == 3);

    slot = fresh();
    slot.cancelled = true;
    deliver_request(&slot);
    assert(result.transport_error == ESP_ERR_INVALID_STATE && opened == 0);
    assert(callbacks == 1 && s_stats.min_tls_internal_free == UINT32_MAX);

    slot = fresh();
    body_size = 4097;
    deliver_request(&slot);
    assert(result.transport_error == ESP_ERR_INVALID_SIZE && result.attempts == 1);
    assert(callbacks == 1 && cleaned == 1);
    puts("PASS: retry recovery, TLS allocation failure, crypto retry, bounded failures, cancellation, body limit, cleanup and heap minima");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="crystal-http-retry-") as directory:
        temp = pathlib.Path(directory)
        (temp / "mock.h").write_text(MOCK)
        for name in ("esp_err.h", "esp_log.h", "esp_crt_bundle.h",
                     "esp_http_client.h", "esp_heap_caps.h", "esp_timer.h",
                     "mbedtls/ssl.h", "freertos/FreeRTOS.h", "freertos/queue.h",
                     "freertos/semphr.h", "freertos/task.h"):
            header = temp / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text('#include "mock.h"\n')
        source = ROOT / "components/crystal_http/src/crystal_http.c"
        (temp / "check.c").write_text(f'#include "{source}"\n' + CHECK)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-I", str(temp), "-I", str(ROOT / "components/crystal_http/include"),
                        str(temp / "check.c"), "-o", str(temp / "check")], check=True)
        subprocess.run([str(temp / "check")], check=True)


if __name__ == "__main__":
    main()
