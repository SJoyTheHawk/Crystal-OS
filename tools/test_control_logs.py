#!/usr/bin/env python3
"""Run production SSE/session handlers with a simulated clock and HTTP socket."""
from pathlib import Path
import os
import subprocess
import tempfile
from test_control_reboot import function, FIXTURE
import contextlib
import io
import urllib.error
from unittest.mock import patch
import crystal

ROOT = Path(__file__).resolve().parents[1]
EXTRA = r'''
#include <algorithm>
#include <deque>
#include <mutex>
#include <utility>
#include <cstdlib>
#include <cerrno>
constexpr size_t kMaxPackageBytes = 5632 * 1024 + 66;
static std::deque<std::pair<uint32_t, std::string>> s_logs;
static uint32_t s_next_log_id = 1;
static std::mutex s_log_mutex;
constexpr size_t kMaxLogLines = 64;
static std::string query;
static unsigned chunks;
static bool socket_failed;
void sodium_memzero(void *p, size_t n) { memset(p, 0, n); }
int httpd_resp_set_type(httpd_req_t *, const char *) { return 0; }
int httpd_resp_set_hdr(httpd_req_t *, const char *, const char *) { return 0; }
int httpd_req_get_url_query_str(httpd_req_t *, char *out, size_t size) {
    if (query.empty() || query.size() >= size) return ESP_FAIL;
    strcpy(out, query.c_str()); return ESP_OK;
}
int httpd_query_key_value(const char *q, const char *, char *out, size_t size) {
    if (strncmp(q, "since=", 6) || strlen(q + 6) >= size) return ESP_FAIL;
    strcpy(out, q + 6); return ESP_OK;
}
int httpd_resp_send_chunk(httpd_req_t *r, const char *data, size_t size) {
    ++chunks; if (socket_failed) return ESP_FAIL;
    if (data) r->body.append(data, size); return ESP_OK;
}
'''
CHECK = r'''
static httpd_req_t paired() {
    httpd_req_t r; r.authorization = std::string("Bearer ") + s_token; return r;
}
int main() {
    memset(s_token, 'a', 64); s_token[64] = 0;
    now_us = 100; s_token_expires_us = 200;
    crystal_control_log_line("app", "First\ninjected\rmessage");
    crystal_control_log_line("app", "Second");
    auto r = paired(); assert(logs_handler(&r) == ESP_OK);
    assert(r.body == "id: 1\ndata: app: First injected message\n\nid: 2\ndata: app: Second\n\n");
    assert(chunks == 2 && delays == 0); // One bounded batch and its terminator.
    query = "since=1"; r = paired(); assert(logs_handler(&r) == ESP_OK);
    assert(r.body == "id: 2\ndata: app: Second\n\n");
    query = "since=2"; r = paired(); assert(logs_handler(&r) == ESP_OK && r.body.empty());
    query = "since=900"; r = paired(); assert(logs_handler(&r) == ESP_OK && r.body.find("id: 1") == 0);
    for (unsigned i = 0; i < 100; ++i) crystal_control_log_line("app", "bounded");
    assert(s_logs.size() == 64);
    query.clear(); r = paired(); now_us = 200; chunks = 0;
    assert(logs_handler(&r) == ESP_OK && r.status == 401 && chunks == 0);
    now_us = 100; r = paired(); socket_failed = true;
    assert(logs_handler(&r) == ESP_FAIL); socket_failed = false;
    auto old = paired(); r = paired(); assert(sign_out_handler(&r) == ESP_OK && r.status == 200);
    assert(!s_token[0] && s_token_expires_us == 0);
    assert(logs_handler(&old) == ESP_OK && old.status == 401);
    size_t length = 999;
    assert(parse_usb_install("CRYSTAL/1 INSTALL 2048", &length) && length == 2048);
    for (const char *header : {"CRYSTAL/1 INSTALL ", "CRYSTAL/1 INSTALL 0", "CRYSTAL/1 INSTALL -1",
            "CRYSTAL/1 INSTALL +1", "CRYSTAL/1 INSTALL  1", "CRYSTAL/1 INSTALL 5extra",
            "CRYSTAL/1 INSTALL 999999999999999999999999", "CRYSTAL/1 INSTALL 6000000"}) {
        length = 999; assert(!parse_usb_install(header, &length) && length == 999);
    }
    puts("PASS: bounded nonblocking SSE batches, cursor resume/reboot, expiry, sign-out revocation, socket failure and frame sanitization");
}
'''


def main():
    source = (ROOT / 'components/crystal_control/src/crystal_control.cpp').read_text()
    production = '\n'.join(function(source, signature) for signature in (
        'bool token_matches(httpd_req_t *request)', 'esp_err_t require_token(httpd_req_t *request)',
        'esp_err_t logs_handler(httpd_req_t *request)', 'esp_err_t sign_out_handler(httpd_req_t *request)',
        'extern "C" void crystal_control_log_line(const char *app_id, const char *line)',
        'bool parse_usb_install(const std::string &line, size_t *length)',
    ))
    with tempfile.TemporaryDirectory(prefix='crystal-logs-') as directory:
        work = Path(directory)
        (work / 'check.cpp').write_text(FIXTURE + EXTRA + production + CHECK)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', str(work / 'check.cpp'), '-o', str(work / 'check')], check=True)
        subprocess.run([str(work / 'check')], check=True)
    responses = [io.BytesIO(b'id: 1\ndata: First\n\nid: 2\ndata: Incomplete'), urllib.error.URLError('link lost'),
                 io.BytesIO(b'id: 2\ndata: Second\n\n'), io.BytesIO(b'')]
    output = io.StringIO()
    with patch.object(crystal.urllib.request, 'urlopen', side_effect=responses) as fetch, \
         patch.object(crystal.time, 'sleep', side_effect=[None, None, None, KeyboardInterrupt]), \
         contextlib.redirect_stdout(output):
        try:
            crystal.stream_logs('http://device', 'session')
        except KeyboardInterrupt:
            pass
    assert output.getvalue() == 'First\nSecond\n'
    assert [call.args[0].full_url for call in fetch.call_args_list] == [
        'http://device/api/v1/logs?since=0', 'http://device/api/v1/logs?since=1',
        'http://device/api/v1/logs?since=1', 'http://device/api/v1/logs?since=2']
    print('PASS: CLI follows batches and resumes without replay after a connection loss')


if __name__ == '__main__':
    main()
