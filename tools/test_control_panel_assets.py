#!/usr/bin/env python3
"""Check packaged icon dimensions and the production static-asset handler."""
from pathlib import Path
import json
import os
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

FIXTURE = r'''
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, HTTPD_404_NOT_FOUND = 404;
struct httpd_req_t {
    const char *uri;
    std::string type, cache, body;
    int status = 200, chunks = 0, fail_chunk = -1;
    bool terminated = false;
};
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type) { r->type = type; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *name, const char *value) {
    assert(!strcmp(name, "Cache-Control")); r->cache = value; return ESP_OK;
}
esp_err_t httpd_resp_send_err(httpd_req_t *r, int status, const char *body) {
    r->status = status; r->body = body; return ESP_OK;
}
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *bytes, size_t size) {
    if (r->chunks == r->fail_chunk) return ESP_FAIL;
    if (!size) { assert(!bytes); r->terminated = true; return ESP_OK; }
    assert(bytes && size <= 1024); ++r->chunks; r->body.append(bytes, size); return ESP_OK;
}
#define ESP_LOGE(...) ((void)0)
'''

CHECK = r'''
static std::string contents(const char *name) {
    std::ifstream input(std::string(CONFIG_BSP_SPIFFS_MOUNT_POINT) + "/control-panel/" + name, std::ios::binary);
    assert(input); return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
static void check(const char *uri, const char *name, const char *type) {
    httpd_req_t request{uri};
    assert(panel_handler(&request) == ESP_OK && request.status == 200);
    assert(request.type == type && request.cache == "no-store");
    assert(request.body == contents(name) && request.terminated && request.chunks > 0);
}
int main() {
    check("/", "index.html", "text/html; charset=utf-8");
    check("/favicon.png", "favicon.png", "image/png");
    check("/favicon.png?v=1", "favicon.png", "image/png");
    check("/favicon.ico", "favicon.ico", "image/x-icon");
    check("/apple-icon.png", "apple-icon.png", "image/png");
    check("/apple-touch-icon.png", "apple-icon.png", "image/png");
    check("/apple-touch-icon-precomposed.png", "apple-icon.png", "image/png");
    for (const char *uri : {"/favicon.png/../index.html", "/favicon-extra.png", "/apple-touch-icon-secret.png",
            "/api/v1/system", "/../index.html", "/favicon.png.bak"}) {
        httpd_req_t request{uri};
        assert(panel_handler(&request) == ESP_OK && request.status == 404);
        assert(request.body == "Not found" && request.chunks == 0);
    }
    for (int fail_at : {0, 1}) {
        httpd_req_t request{"/apple-touch-icon.png"}; request.fail_chunk = fail_at;
        assert(panel_handler(&request) == ESP_FAIL && !request.terminated);
    }
    for (const char *name : {"favicon.png", "index.html"}) {
        const std::string path = std::string(CONFIG_BSP_SPIFFS_MOUNT_POINT) + "/control-panel/" + name;
        assert(std::remove(path.c_str()) == 0);
        const std::string uri = !strcmp(name, "index.html") ? "/" : std::string("/") + name;
        httpd_req_t request{uri.c_str()};
        assert(panel_handler(&request) == ESP_OK && request.status == 404);
        assert(request.body == (!strcmp(name, "index.html") ?
            "Control panel is not installed" : "Panel icon is not installed"));
    }
    puts("PASS: production panel/icon responses, MIME types, binary chunks, aliases, fixed paths, missing files and interrupted transfers");
}
'''


def main():
    assets = ROOT / 'assets/control-panel'
    for name, size, limit in (('favicon.png', 48, 8192), ('apple-icon.png', 180, 20480)):
        data = (assets / name).read_bytes()
        assert data[:8] == b'\x89PNG\r\n\x1a\n'
        assert struct.unpack('>II', data[16:24]) == (size, size)
        assert len(data) < limit, (name, len(data))
        assert len(('/control-panel/' + name).encode()) < 32, 'SPIFFS stored name must fit'
    ico = (assets / 'favicon.ico').read_bytes()
    assert struct.unpack('<HHH', ico[:6]) == (0, 1, 1)
    width, height, _, _, planes, bits, length, offset = struct.unpack('<BBBBHHII', ico[6:22])
    assert (width, height, planes, bits, offset) == (48, 48, 1, 32, 22)
    assert ico[offset:offset + length] == (assets / 'favicon.png').read_bytes()
    source = (ROOT / 'components/crystal_control/src/crystal_control.cpp').read_text()
    start = source.index('esp_err_t panel_handler(httpd_req_t *request)\n{')
    handler = source[start:source.index('\n}\n', start) + 3]
    with tempfile.TemporaryDirectory(prefix='crystal-panel-assets-') as directory:
        work = Path(directory)
        shutil.copytree(assets, work / 'control-panel')
        define = '#define CONFIG_BSP_SPIFFS_MOUNT_POINT ' + json.dumps(directory) + '\n'
        (work / 'check.cpp').write_text(define + FIXTURE + handler + CHECK)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-g',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        str(work / 'check.cpp'), '-o', str(work / 'check')], check=True)
        subprocess.run([str(work / 'check')], check=True,
                       env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))
    print('PASS: optimized PNG dimensions, size budgets and ICO fallback payload')


if __name__ == '__main__':
    main()
