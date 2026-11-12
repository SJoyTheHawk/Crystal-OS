#!/usr/bin/env python3
"""Exercise production authorization and reboot handlers with host HTTP stubs.

No board is contacted or rebooted. ESP-IDF compilation and on-device checks
remain separate from this deterministic handler regression.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'components/crystal_control/src/crystal_control.cpp'


def function(source, signature):
    start = source.index(signature + '\n{')
    return source[start:source.index('\n}\n', start) + 3]


FIXTURE = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1;
constexpr size_t kMaxTokenHeader = 96;
struct httpd_req_t {
    std::string authorization, body;
    int status = 0, sends = 0;
    bool header_error = false;
    esp_err_t send_result = ESP_OK;
};
static char s_token[65];
static int64_t now_us = 100, s_token_expires_us = 200;
static std::atomic<bool> s_installing{false};
static int server;
static void *s_server = &server;
static void (*queued_work)(void *) = nullptr;
static void *queued_arg = nullptr;
static int queues = 0, restarts = 0, delays = 0;
static esp_err_t queue_result = ESP_OK;
static httpd_req_t *accepted_request = nullptr;
int64_t esp_timer_get_time() { return now_us; }
size_t httpd_req_get_hdr_value_len(httpd_req_t *r, const char *) { return r->authorization.size(); }
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *, char *out, size_t n) {
    if (r->header_error || r->authorization.size() >= n) return ESP_FAIL;
    std::strcpy(out, r->authorization.c_str()); return ESP_OK;
}
int sodium_memcmp(const void *a, const void *b, size_t n) { return std::memcmp(a, b, n); }
esp_err_t json_response(httpd_req_t *r, int status, const char *body) {
    r->status = status; r->body = body; ++r->sends; return r->send_result;
}
esp_err_t httpd_queue_work(void *handle, void (*work)(void *), void *arg) {
    assert(handle == s_server && s_installing); ++queues;
    if (queue_result != ESP_OK) return queue_result;
    queued_work = work; queued_arg = arg; return ESP_OK;
}
#define pdMS_TO_TICKS(value) (value)
void vTaskDelay(unsigned ticks) {
    assert(ticks == 250 && accepted_request && accepted_request->status == 202);
    assert(accepted_request->sends == 1); ++delays;
}
void esp_restart() { assert(delays == 1); ++restarts; }
#define ESP_LOGI(...) ((void)0)
'''

CHECK = r'''
static void reset() {
    std::memset(s_token, 'a', 64); s_token[64] = 0;
    now_us = 100; s_token_expires_us = 200; s_installing = false;
    queued_work = nullptr; queued_arg = nullptr;
    queues = restarts = delays = 0; queue_result = ESP_OK; accepted_request = nullptr;
}
static httpd_req_t paired() {
    httpd_req_t request; request.authorization = std::string("Bearer ") + s_token; return request;
}
static void rejected(httpd_req_t &request) {
    assert(reboot_handler(&request) == ESP_OK);
    assert(request.status == 401 && request.sends == 1);
    assert(request.body == R"({"error":"unauthorized"})");
    assert(queues == 0 && !queued_work && !s_installing && restarts == 0);
}
int main() {
    reset(); httpd_req_t missing; rejected(missing);
    reset(); auto bad = paired(); bad.authorization.back() = 'b'; rejected(bad);
    reset(); auto short_token = paired(); short_token.authorization.pop_back(); rejected(short_token);
    reset(); auto expired = paired(); now_us = s_token_expires_us; rejected(expired);
    reset(); auto unset = paired(); s_token[0] = 0; rejected(unset);
    reset(); auto header_error = paired(); header_error.header_error = true; rejected(header_error);
    reset(); auto no_bearer = paired(); no_bearer.authorization.replace(0, 7, "Basic  "); rejected(no_bearer);
    reset(); httpd_req_t unauthorized;
    assert(require_token(&unauthorized) == ESP_FAIL && unauthorized.status == 401);
    assert(unauthorized.send_result == ESP_OK); // Sending 401 cannot authorize the caller.
    reset(); httpd_req_t broken_socket; broken_socket.send_result = ESP_FAIL; rejected(broken_socket);

    reset(); auto installing = paired(); s_installing = true;
    assert(reboot_handler(&installing) == ESP_OK && installing.status == 409);
    assert(queues == 0 && s_installing && !queued_work);

    reset(); auto failed = paired(); queue_result = ESP_FAIL;
    assert(reboot_handler(&failed) == ESP_OK && failed.status == 500);
    assert(!s_installing && !queued_work && queues == 1 && restarts == 0);
    queue_result = ESP_OK; auto retry = paired();
    assert(reboot_handler(&retry) == ESP_OK && retry.status == 202);
    assert(s_installing && queued_work && queues == 2 && restarts == 0);

    reset(); auto accepted = paired();
    assert(reboot_handler(&accepted) == ESP_OK && accepted.status == 202);
    assert(accepted.body == R"({"rebooting":true})" && accepted.sends == 1);
    assert(queues == 1 && queued_work && restarts == 0 && delays == 0 && s_installing);
    auto duplicate = paired();
    assert(reboot_handler(&duplicate) == ESP_OK && duplicate.status == 409);
    assert(queues == 1 && s_installing);
    accepted_request = &accepted; queued_work(queued_arg);
    assert(restarts == 1 && delays == 1);
    puts("PASS: reboot authentication, unauthorized early return, install exclusion, queue failure recovery, duplicate rejection, and response-before-restart");
}
'''


def main():
    source = SOURCE.read_text()
    functions = '\n'.join(function(source, signature) for signature in (
        'bool token_matches(httpd_req_t *request)',
        'esp_err_t require_token(httpd_req_t *request)',
        'void reboot_worker(void *)',
        'esp_err_t reboot_handler(httpd_req_t *request)',
    ))
    with tempfile.TemporaryDirectory(prefix='crystal-control-reboot-') as directory:
        work = Path(directory)
        (work / 'check.cpp').write_text(FIXTURE + functions + CHECK)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-g',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        str(work / 'check.cpp'), '-o', str(work / 'check')], check=True)
        subprocess.run([str(work / 'check')], check=True,
                       env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))


if __name__ == '__main__':
    main()
