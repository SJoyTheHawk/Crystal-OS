#!/usr/bin/env python3
"""Exercise production app-action parsing and handlers with real cJSON.

HTTP and registry storage are host fixtures; this does not operate a board.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


FIXTURE = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <cmath>
#include <string>
#include <vector>
#include "cJSON.h"
#include "crystal_registry.hpp"
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1;
constexpr int HTTP_POST = 1, HTTP_DELETE = 2;
constexpr uint8_t CRYSTAL_CONTROL_ACTION_CLEAR_DATA = 1,
    CRYSTAL_CONTROL_ACTION_UNINSTALL = 2, CRYSTAL_CONTROL_ACTION_LAUNCH = 3;
constexpr size_t kMaxTokenHeader = 96;
struct httpd_req_t {
    const char *uri;
    int method = HTTP_POST;
    std::string authorization, input, body;
    size_t content_len = 0, received = 0;
    int status = 0, sends = 0, reads = 0;
};
static char s_token[65];
static int64_t s_token_expires_us = 200, now_us = 100;
static bool storage_ok = true, queue_ok = true;
static int saves = 0, posts = 0;
static uint8_t posted_kind = 0;
static std::string saved_id, posted_id;
static std::map<std::string, bool> states;
static std::map<std::string, uint16_t> slots;
static CrystalAppEntry catalog[] = {
    {"dev_tester", nullptr, true, 0}, {"clock", nullptr, true, 1},
    {"weather", nullptr, true, 2}, {"calculator", nullptr, true, 3},
    {"org.crystal.declarative-dashboard", nullptr, true, 4, CRYSTAL_ORIGIN_PACKAGE},
    {"package_alias", nullptr, true, 5, CRYSTAL_ORIGIN_PACKAGE},
};
size_t crystal_registry_catalog_count() { return sizeof(catalog) / sizeof(catalog[0]) + 1; }
const CrystalAppEntry *crystal_registry_catalog_entry(size_t index) {
    return index < sizeof(catalog) / sizeof(catalog[0]) && catalog[index].available ? &catalog[index] : nullptr;
}
bool crystal_registry_set_enabled(const char *id, bool enabled) {
    ++saves; saved_id = id;
    bool found = false;
    for (const auto &entry : catalog) if (entry.available && !strcmp(entry.id, id)) found = true;
    if (!storage_ok || !found) return false;
    states[id] = enabled; return true;
}
bool crystal_registry_enabled(const char *id, bool fallback) {
    auto found = states.find(id); return found == states.end() ? fallback : found->second;
}
bool crystal_registry_set_slot(const char *id, uint16_t slot) {
    ++saves; if (!storage_ok) return false; slots[id] = slot; return true;
}
int64_t esp_timer_get_time() { return now_us; }
size_t httpd_req_get_hdr_value_len(httpd_req_t *r, const char *) { return r->authorization.size(); }
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *, char *out, size_t length) {
    if (r->authorization.size() >= length) return ESP_FAIL;
    strcpy(out, r->authorization.c_str()); return ESP_OK;
}
int sodium_memcmp(const void *a, const void *b, size_t size) { return memcmp(a, b, size); }
int httpd_req_recv(httpd_req_t *r, char *buffer, size_t length) {
    ++r->reads;
    const size_t count = std::min({length, r->input.size() - r->received, size_t(3)});
    if (!count) return 0;
    memcpy(buffer, r->input.data() + r->received, count); r->received += count; return count;
}
esp_err_t json_response(httpd_req_t *r, int status, const char *body) {
    ++r->sends; r->status = status; r->body = body; return ESP_OK;
}
bool post_control_action(uint8_t kind, const std::string &id) {
    ++posts; posted_kind = kind; posted_id = id; return queue_ok;
}
esp_err_t action_response(httpd_req_t *r, bool queued) {
    return json_response(r, queued ? 202 : 500, queued ? "queued" : "queue unavailable");
}
'''

CHECK = r'''
static void reset() {
    memset(s_token, 'a', 64); s_token[64] = 0; now_us = 100;
    storage_ok = queue_ok = true; saves = posts = 0; states.clear();
    for (auto &entry : catalog) entry.available = true;
}
static httpd_req_t request(const char *path, int method = HTTP_POST, const char *body = "") {
    httpd_req_t r{path}; r.method = method; r.input = body; r.content_len = r.input.size();
    r.authorization = std::string("Bearer ") + s_token; return r;
}
static void run(httpd_req_t &r, int expected) {
    assert(app_action_handler(&r) == ESP_OK);
    assert(r.status == expected && r.sends == 1);
}
int main() {
    reset();
    // Preserve strict package grammar; only catalog built-ins are exceptions.
    assert(!crystal_package_valid_id("clock") && !crystal_package_valid_id("dev_tester"));
    assert(crystal_package_valid_id("org.crystal.declarative-dashboard"));
    const char *suffixes[] = {"", "/data", "/enabled", "/launch"};
    for (const char *name : {"clock", "dev_tester", "weather", "calculator",
            "org.crystal.declarative-dashboard", "org.crystal.unknown"}) {
        for (const char *suffix : suffixes) {
            std::string path = std::string("/api/v1/apps/") + name + suffix;
            auto r = request(path.c_str()); std::string id; const char *parsed = nullptr;
            assert(request_path_id(&r, &id, &parsed) && id == name);
            assert(*suffix ? parsed && !strcmp(parsed, suffix) : parsed == nullptr);
        }
    }
    for (const char *path : {"/api/v1/apps/unknown/enabled", "/api/v1/apps/package_alias/enabled",
            "/api/v1/apps/Clock/enabled", "/api/v1/apps/%63lock/enabled",
            "/api/v1/apps/../enabled", "/api/v1/apps/clock/../enabled",
            "/api/v1/apps/clock/enabled/extra", "/api/v1/apps//enabled",
            "/api/v1/apps/org..app/data", "/api/v1/apps/org.App/data",
            "/api/v1/apps/org.crystal.app/unknown", "/api/v1/apps"}) {
        auto r = request(path, HTTP_POST, R"({"enabled":false})"); run(r, 400);
        assert(r.body == R"({"error":"invalid app id"})" && r.reads == 0);
    }
    std::string id; auto r = request("/api/v1/apps/clock");
    assert(!request_path_id(nullptr, &id) && !request_path_id(&r, nullptr));
    std::string long_path = "/api/v1/apps/org." + std::string(60, 'a') + "/enabled";
    auto long_request = request(long_path.c_str()); assert(!request_path_id(&long_request, &id));
    catalog[1].available = false; auto unavailable = request("/api/v1/apps/clock/enabled");
    assert(!request_path_id(&unavailable, &id)); catalog[1].available = true;

    // This exact production handler used to reject the first disable as invalid app id.
    for (const auto &entry : catalog) {
        if (!strcmp(entry.id, "package_alias")) continue;
        for (bool enabled : {false, true}) {
            std::string path = std::string("/api/v1/apps/") + entry.id + "/enabled";
            auto toggle = request(path.c_str(), HTTP_POST, enabled ?
                R"({"enabled":true})" : R"({"enabled":false})");
            const int before = saves; run(toggle, 202);
            assert(saves == before + 1 && saved_id == entry.id && states.at(entry.id) == enabled);
            assert(toggle.body == (enabled ? R"({"enabled":true,"restart_required":true})" :
                R"({"enabled":false,"restart_required":true})"));
        }
    }
    const int saved_before = saves;
    for (const char *body : {"", "{}", "[]", "null", "{", R"({"enabled":1})", R"({"enabled":"false"})"}) {
        auto bad = request("/api/v1/apps/clock/enabled", HTTP_POST, body); run(bad, 400);
        assert(saves == saved_before);
    }
    auto truncated = request("/api/v1/apps/clock/enabled", HTTP_POST, "{");
    truncated.content_len = 18; run(truncated, 400); assert(saves == saved_before);
    auto oversized = request("/api/v1/apps/clock/enabled"); oversized.content_len = 129;
    run(oversized, 400); assert(oversized.reads == 0 && saves == saved_before);
    storage_ok = false;
    auto failed = request("/api/v1/apps/clock/enabled", HTTP_POST, R"({"enabled":false})");
    run(failed, 400); assert(states.at("clock"));
    storage_ok = true;
    auto retry = request("/api/v1/apps/clock/enabled", HTTP_POST, R"({"enabled":false})");
    run(retry, 202); assert(!states.at("clock"));

    auto clear = request("/api/v1/apps/clock/data", HTTP_DELETE); run(clear, 202);
    assert(posted_id == "clock" && posted_kind == CRYSTAL_CONTROL_ACTION_CLEAR_DATA);
    auto launch = request("/api/v1/apps/dev_tester/launch"); run(launch, 202);
    assert(posted_id == "dev_tester" && posted_kind == CRYSTAL_CONTROL_ACTION_LAUNCH);
    auto remove = request("/api/v1/apps/org.crystal.declarative-dashboard", HTTP_DELETE); run(remove, 202);
    assert(posted_id == "org.crystal.declarative-dashboard" && posted_kind == CRYSTAL_CONTROL_ACTION_UNINSTALL);
    auto builtin_remove = request("/api/v1/apps/weather", HTTP_DELETE); run(builtin_remove, 202);
    assert(!states.at("weather") && saved_id == "weather");
    auto disabled_launch = request("/api/v1/apps/weather/launch"); run(disabled_launch, 409);
    const auto actions_before = posts;
    auto wrong_delete = request("/api/v1/apps/org.crystal.declarative-dashboard/launch", HTTP_DELETE);
    run(wrong_delete, 400); assert(posts == actions_before);
    auto unknown = request("/api/v1/apps/org.crystal.unknown", HTTP_DELETE); run(unknown, 404);
    assert(posts == actions_before);
    queue_ok = false; auto blocked = request("/api/v1/apps/clock/data", HTTP_DELETE); run(blocked, 500);
    const int before_order = saves;
    for (const char *body : {R"([{"id":"clock","slot":1},{"id":"weather","slot":1}])",
            R"([{"id":"clock","slot":1},{"id":"org.crystal.unknown","slot":2}])",
            R"([{"id":"clock","slot":1.5}])", R"([{"id":"clock","slot":1},{"id":"clock","slot":2}])"}) {
        auto order = request("/api/v1/apps/order", HTTP_POST, body);
        assert(order_handler(&order) == ESP_OK && order.status == 400 && saves == before_order);
    }
    auto order = request("/api/v1/apps/order", HTTP_POST, R"([{"id":"clock","slot":1},{"id":"weather","slot":0}])");
    assert(order_handler(&order) == ESP_OK && order.status == 202 && slots["clock"] == 1 && slots["weather"] == 0);
    const int before_saves = saves, before_posts = posts;
    for (const char *path : {"/api/v1/apps/clock/enabled", "/api/v1/apps/dev_tester/data",
            "/api/v1/apps/org.crystal.declarative-dashboard"}) {
        for (int auth = 0; auth < 3; ++auth) {
            auto rejected = request(path, HTTP_DELETE);
            if (auth == 0) rejected.authorization.clear();
            if (auth == 1) rejected.authorization = "Bearer invalid-test-token";
            now_us = auth == 2 ? s_token_expires_us : 100;
            run(rejected, 401); assert(rejected.reads == 0);
            assert(saves == before_saves && posts == before_posts);
        }
    }
    puts("PASS: production built-in/package app paths, Clock disable/enable, restart feedback, storage failure/retry, action dispatch, malformed IDs and unauthorized early return");
}
'''


def main():
    control = (ROOT / 'components/crystal_control/src/crystal_control.cpp').read_text()
    package = (ROOT / 'components/crystal_package/src/crystal_package.cpp').read_text()
    production = function(package, 'bool crystal_package_valid_id(const std::string &id)')
    production += '\n'.join(function(control, signature) for signature in (
        'bool request_path_id(httpd_req_t *request, std::string *id, const char **suffix = nullptr)',
        'bool token_matches(httpd_req_t *request)',
        'esp_err_t require_token(httpd_req_t *request)',
        'esp_err_t app_action_handler(httpd_req_t *request)',
        'esp_err_t order_handler(httpd_req_t *request)',
    ))
    cjson = ROOT / 'managed_components/espressif__cjson/cJSON'
    with tempfile.TemporaryDirectory(prefix='crystal-control-app-actions-') as directory:
        work = Path(directory)
        (work / 'check.cpp').write_text(FIXTURE + production + CHECK)
        flags = ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', *flags,
                        '-c', str(cjson / 'cJSON.c'), '-o', str(work / 'cjson.o')], check=True)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', *flags,
                        '-I', str(cjson), '-I', str(ROOT / 'components/crystal_registry/include'),
                        str(work / 'check.cpp'), str(work / 'cjson.o'), '-o', str(work / 'check')], check=True)
        subprocess.run([str(work / 'check')], check=True,
                       env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))


if __name__ == '__main__':
    main()
