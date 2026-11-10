#!/usr/bin/env python3
"""Exercise the production queued JSON adapter under ASan/UBSan.

Checks ownership when completion races a timeout, late completion after a new
request, queue rejection, malformed responses, and bounded blocking. This does
not simulate ESP32 scheduling or establish that the device watchdog is fixed.
"""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
MOCK = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <cstdio>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
struct Semaphore { bool given = false; };
using SemaphoreHandle_t = Semaphore *;
static unsigned semaphore_count, body_count, cancellations;
static bool fail_semaphore;
static std::function<void()> on_wait, on_cancel;
static SemaphoreHandle_t xSemaphoreCreateBinary() {
    if (fail_semaphore) return nullptr;
    ++semaphore_count;
    return new Semaphore;
}
static int xSemaphoreGive(SemaphoreHandle_t s) { s->given = true; return pdTRUE; }
static int xSemaphoreTake(SemaphoreHandle_t s, unsigned timeout) {
    assert(timeout > 0 && timeout < UINT32_MAX);
    if (on_wait) { auto fn = on_wait; on_wait = {}; fn(); }
    return s->given ? pdTRUE : 0;
}
static void vSemaphoreDelete(SemaphoreHandle_t s) { assert(s); --semaphore_count; delete s; }
static void mock_log(const char *, const char *, ...) {}
#define ESP_LOGI mock_log
#define ESP_LOGW mock_log
static const char *esp_err_to_name(int) { return "mock"; }
'''
CHECK = r'''
struct Queued {
    crystal_http_callback_t callback;
    void *context;
};
static Queued queued;
static bool reject_submission, complete_on_submit;
static unsigned next_id;
static void finish(Queued request, const char *body, int status = 200, int error = ESP_OK) {
    crystal_http_response_t response = {};
    response.status_code = status;
    response.transport_error = error;
    if (body) {
        response.body_len = strlen(body);
        response.body = reinterpret_cast<uint8_t *>(strdup(body));
        ++body_count;
    }
    request.callback(&response, request.context);
}
extern "C" uint32_t crystal_http_get(const crystal_http_options_t *options,
                                     crystal_http_callback_t callback, void *context) {
    assert(options->timeout_ms > 0 && options->max_attempts == 1);
    assert(options->max_body_bytes > 0);
    if (reject_submission) return 0;
    queued = {callback, context};
    if (complete_on_submit) finish(queued, "{}");
    return ++next_id;
}
extern "C" bool crystal_http_cancel(uint32_t id) {
    assert(id); ++cancellations;
    if (on_cancel) { auto fn = on_cancel; on_cancel = {}; fn(); }
    return true;
}
extern "C" void crystal_http_response_release(const crystal_http_response_t *r) {
    assert(r->body && body_count); --body_count; free(r->body);
}
static bool fetch(char *out, size_t size) {
    return crystal_core_http_get_json("https://weather.test/", out, size);
}
static void clean() { assert(semaphore_count == 0 && body_count == 0); }
int main() {
    char out[32] = "stale";
    complete_on_submit = true;
    assert(fetch(out, sizeof(out)) && strcmp(out, "{}") == 0); clean();
    complete_on_submit = false;

    on_wait = [] { finish(queued, "{\"temperature\":15}"); };
    assert(fetch(out, sizeof(out)) && strstr(out, "15")); clean();

    reject_submission = true;
    assert(!fetch(out, sizeof(out)) && out[0] == 0); clean();
    reject_submission = false;
    fail_semaphore = true;
    assert(!fetch(out, sizeof(out))); clean();
    fail_semaphore = false;

    on_wait = [] { finish(queued, "{\"error\":true}", 503); };
    assert(!fetch(out, sizeof(out)) && out[0] == 0); clean();
    on_wait = [] { finish(queued, "partial", 200, ESP_FAIL); };
    assert(!fetch(out, sizeof(out)) && out[0] == 0); clean();
    on_wait = [] { finish(queued, nullptr, 200); };
    assert(!fetch(out, sizeof(out)) && out[0] == 0); clean();
    on_wait = [] { finish(queued, ""); };
    assert(!fetch(out, sizeof(out)) && out[0] == 0); clean();

    char exact[3];
    on_wait = [] { finish(queued, "{}"); };
    assert(fetch(exact, sizeof(exact)) && strcmp(exact, "{}") == 0); clean();
    on_wait = [] { finish(queued, "xxx"); };
    assert(!fetch(exact, sizeof(exact)) && exact[0] == 0); clean();

    // The first callback survives timeout and arrives during a new request.
    assert(!fetch(out, sizeof(out)) && out[0] == 0);
    assert(cancellations == 1 && semaphore_count == 1);
    const Queued late = queued;
    on_wait = [late] { finish(late, "stale body"); finish(queued, "new body"); };
    assert(fetch(out, sizeof(out)) && strcmp(out, "new body") == 0); clean();

    // Completion can race cancellation before the caller drops its reference.
    on_cancel = [] { finish(queued, "racing body"); };
    assert(!fetch(out, sizeof(out)) && out[0] == 0); clean();

    assert(!crystal_core_http_get_json(nullptr, out, sizeof(out)));
    assert(!crystal_core_http_get_json("https://weather.test", nullptr, 0));
    clean();
    puts("PASS: completion before/during wait, queue/allocation failure, HTTP/transport/body validation, timeout, late callback isolation and cancellation race");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="crystal-core-http-") as directory:
        temp = pathlib.Path(directory)
        (temp / "mock.h").write_text(MOCK)
        for name in ("esp_err.h", "esp_log.h", "freertos/FreeRTOS.h", "freertos/semphr.h"):
            path = temp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "mock.h"\n')
        source = ROOT / "components/crystal_core/src/crystal_core_http.cpp"
        (temp / "check.cpp").write_text(f'#include "{source}"\n' + CHECK)
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I", str(temp), "-I", str(ROOT / "components/crystal_http/include"),
                        str(temp / "check.cpp"), "-o", str(temp / "check")], check=True)
        subprocess.run([str(temp / "check")], check=True)


if __name__ == "__main__":
    main()
