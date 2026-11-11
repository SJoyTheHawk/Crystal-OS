/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_package.hpp"
#include "crystal_hal.hpp"
#ifdef ESP_PLATFORM
#include "crystal_signature.hpp"
#endif
#include "cJSON.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#ifdef ESP_PLATFORM
#include "esp_littlefs.h"
#include "esp_partition.h"
#include "nvs.h"
#endif

namespace {
constexpr size_t kManifestMax = 8192, kRegistryMax = 16384, kStateMax = 4096, kAppsMax = 32;
int path_stat(const char *path, struct stat *st) {
#ifdef ESP_PLATFORM
    // The LittleFS VFS cannot create symlinks; ESP-IDF has no lstat syscall.
    return stat(path, st);
#else
    return lstat(path, st);
#endif
}
std::string s_root;
CrystalPackageLimits s_limits;
std::vector<CrystalPackageRecord> s_records;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
Json json(cJSON *value) { return Json(value, cJSON_Delete); }
bool fail(std::string *error, const char *message) { if (error) *error = message; return false; }
std::string dump(const cJSON *value) {
    char *text = cJSON_PrintUnformatted(value);
    std::string result = text ? text : "";
    cJSON_free(text);
    return result;
}
Json parse(const std::string &text) {
    if (text.find('\0') != std::string::npos || text.find("\\u0000") != std::string::npos) return json(nullptr);
    // Bound cJSON recursion and node allocations before parsing untrusted data.
    unsigned depth = 0, punctuation = 0;
    bool quoted = false, escaped = false;
    for (char c : text) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 16 || ++punctuation > 512) return json(nullptr); }
        else if (c == '}' || c == ']') { if (!depth) return json(nullptr); --depth; }
        else if (c == ',' || c == ':') { if (++punctuation > 512) return json(nullptr); }
    }
    if (depth || quoted) return json(nullptr);
    return json(cJSON_ParseWithLengthOpts(text.c_str(), text.size() + 1, nullptr, true));
}
bool unique_fields(const cJSON *value) {
    std::set<std::string> names;
    for (const cJSON *child = value->child; child; child = child->next) {
        if (cJSON_IsObject(value) && (!child->string || !names.insert(child->string).second)) return false;
        if ((cJSON_IsObject(child) || cJSON_IsArray(child)) && !unique_fields(child)) return false;
    }
    return true;
}
const cJSON *field(const cJSON *value, const char *key) { return cJSON_GetObjectItemCaseSensitive(value, key); }
bool text_field(const cJSON *obj, const char *key, std::string *out, size_t max, bool required = true) {
    auto *v = field(obj, key);
    if (!v && !required) { out->clear(); return true; }
    if (!cJSON_IsString(v) || !v->valuestring || !*v->valuestring || strlen(v->valuestring) > max) return false;
    *out = v->valuestring;
    return out->find_first_of("\r\n\t") == std::string::npos;
}
bool number(const cJSON *obj, const char *key, uint32_t *out, uint32_t min, uint32_t max) {
    auto *v = field(obj, key);
    if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || std::floor(v->valuedouble) != v->valuedouble ||
        v->valuedouble < min || v->valuedouble > max) return false;
    *out = static_cast<uint32_t>(v->valuedouble); return true;
}
bool semver(const std::string &version, std::array<unsigned, 3> *out) {
    size_t start = 0;
    for (unsigned i = 0; i < 3; ++i) {
        size_t end = version.find('.', start);
        if ((i < 2 && end == std::string::npos) || (i == 2 && end != std::string::npos)) return false;
        std::string part = version.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part.size() > 5 || (part.size() > 1 && part[0] == '0')) return false;
        unsigned n = 0;
        for (char c : part) { if (c < '0' || c > '9') return false; n = n * 10 + c - '0'; }
        if (n > 65535) return false;
        (*out)[i] = n; start = end + 1;
    }
    return true;
}
bool relative(const std::string &path) {
    if (path.empty() || path.size() > 160 || path[0] == '/' || path.back() == '/' || path.find('\\') != std::string::npos) return false;
    size_t pos = 0;
    while (pos < path.size()) {
        size_t end = path.find('/', pos);
        auto part = path.substr(pos, end == std::string::npos ? end : end - pos);
        if (part.empty() || part == "." || part == ".." || part.size() > 63) return false;
        for (unsigned char c : part) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return true;
}
bool host_name(const std::string &host) {
    if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.') return false;
    size_t begin = 0;
    while (begin < host.size()) {
        auto end = host.find('.', begin);
        auto label = host.substr(begin, end == std::string::npos ? end : end - begin);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-') return false;
        for (char c : label) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return true;
}
int read_blob(const char *key, std::string *out, size_t capacity) {
    out->resize(capacity);
    size_t length = capacity;
#ifdef ESP_PLATFORM
    nvs_handle_t handle;
    esp_err_t err = nvs_open("crystal", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) { out->clear(); return 0; }
    if (err != ESP_OK) return -1;
    err = nvs_get_blob(handle, key, &(*out)[0], &length);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) { out->clear(); return 0; }
    if (err != ESP_OK) return -1;
#else
    if (!hal().storage || !hal().storage->get(key, &(*out)[0], &length)) { out->clear(); return 0; }
#endif
    if (length > capacity) return -1;
    out->resize(length); return 1;
}
bool write_blob(const char *key, const std::string &value) {
    return hal().storage && !value.empty() && hal().storage->set(key, value.data(), value.size());
}
bool mkdir_ok(const std::string &path) {
    if (mkdir(path.c_str(), 0700) == 0) return true;
    struct stat st{};
    return errno == EEXIST && path_stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool remove_tree(const std::string &path) {
    struct stat st{};
    if (path_stat(path.c_str(), &st) != 0) return errno == ENOENT;
    if (!S_ISDIR(st.st_mode)) return unlink(path.c_str()) == 0;
    DIR *dir = opendir(path.c_str());
    if (!dir) return false;
    bool ok = true;
    while (auto *ent = readdir(dir)) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        if (!remove_tree(path + "/" + ent->d_name)) { ok = false; break; }
    }
    closedir(dir);
    return ok && rmdir(path.c_str()) == 0;
}
bool file_text(const std::string &path, std::string *out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out->resize(kManifestMax + 1);
    size_t n = fread(&(*out)[0], 1, out->size(), f);
    bool ok = !ferror(f) && n <= kManifestMax;
    fclose(f); out->resize(n); return ok;
}
bool write_file(const std::string &path, const std::string &data) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    if (fflush(f) != 0 || fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    return ok;
}
bool tree_size(const std::string &path, size_t *size) {
    struct stat st{};
    if (path_stat(path.c_str(), &st)) return false;
    if (S_ISREG(st.st_mode)) { *size += st.st_size; return true; }
    if (!S_ISDIR(st.st_mode)) return false;
    DIR *dir = opendir(path.c_str()); if (!dir) return false;
    bool ok = true;
    while (auto *ent = readdir(dir)) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        if (!tree_size(path + "/" + ent->d_name, size)) { ok = false; break; }
    }
    closedir(dir); return ok;
}
auto find_record(const std::string &id) {
    return std::find_if(s_records.begin(), s_records.end(), [&](const CrystalPackageRecord &r) { return r.manifest.id == id; });
}
bool save_records() {
    auto list = json(cJSON_CreateArray());
    if (!list) return false;
    for (const auto &r : s_records) {
        auto *o = cJSON_CreateObject();
        if (!o) return false;
        if (!cJSON_AddItemToArray(list.get(), o)) { cJSON_Delete(o); return false; }
        if (!cJSON_AddStringToObject(o, "id", r.manifest.id.c_str()) ||
            !cJSON_AddNumberToObject(o, "slot", r.slot) ||
            !cJSON_AddBoolToObject(o, "enabled", r.enabled) ||
            !cJSON_AddNumberToObject(o, "errors", r.error_count) ||
            !cJSON_AddNumberToObject(o, "installed_at", r.installed_at) ||
            !cJSON_AddStringToObject(o, "details", r.details.c_str())) return false;
    }
    auto text = dump(list.get());
    return text.size() <= kRegistryMax && write_blob("pkg.registry", text);
}
bool admission(size_t new_bytes, const CrystalPackageManifest &m, std::string *error) {
    uint64_t memory = uint64_t(m.memory_kb) * 1024, storage = new_bytes + uint64_t(m.storage_kb) * 1024;
    unsigned workers = m.worker_threads;
    for (const auto &r : s_records) {
        if (r.manifest.runtime.empty()) return fail(error, "Remove or repair the damaged app before installing");
        memory += uint64_t(r.manifest.memory_kb) * 1024;
        workers += r.manifest.worker_threads;
        size_t bytes = 0;
        if (!tree_size(s_root + "/" + r.manifest.id, &bytes)) return fail(error, "Could not read installed app storage");
        // Reserve the full data quota in addition to the immutable bundle.
        size_t data_bytes = 0;
        if (!tree_size(s_root + "/" + r.manifest.id + "/data", &data_bytes)) return fail(error, "Could not read app data");
        storage += bytes - data_bytes + uint64_t(r.manifest.storage_kb) * 1024;
    }
    if (memory > s_limits.memory_bytes) return fail(error, "Insufficient PSRAM budget");
    if (storage > s_limits.storage_bytes) return fail(error, "Insufficient app storage budget");
    if (workers > s_limits.workers) return fail(error, "Worker thread quota exceeded");
    return true;
}
std::string state_key(const std::string &id) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : id) { hash ^= c; hash *= 1099511628211ull; }
    char key[16]; snprintf(key, sizeof(key), "p%014llx", static_cast<unsigned long long>(hash & 0xffffffffffffffull));
    return key;
}
Json load_state(const std::string &id) {
    std::string value;
    int status = read_blob(state_key(id).c_str(), &value, kStateMax);
    if (status < 0) return json(nullptr);
    if (!status) {
        auto root = json(cJSON_CreateObject());
        if (!root || !cJSON_AddStringToObject(root.get(), "id", id.c_str()) ||
            !cJSON_AddObjectToObject(root.get(), "values")) return json(nullptr);
        return root;
    }
    auto root = parse(value);
    auto *owner = field(root.get(), "id");
    if (!cJSON_IsString(owner) || id != owner->valuestring || !cJSON_IsObject(field(root.get(), "values"))) return json(nullptr);
    return root;
}
bool state_valid(const std::string &id, const char *key) {
    return crystal_package_valid_id(id) && key && *key && strlen(key) <= 63 && find_record(id) != s_records.end();
}
}

bool crystal_package_valid_id(const std::string &id) {
    if (id.size() > 63 || id.find('.') == std::string::npos || id.empty() || id.back() == '.') return false;
    bool first = true;
    for (char c : id) {
        if (c == '.') { if (first) return false; first = true; continue; }
        if (first && !(c >= 'a' && c <= 'z')) return false;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
        first = false;
    }
    return true;
}

bool crystal_package_parse_manifest(const std::string &text, CrystalPackageManifest *out, std::string *error) {
    if (!out || text.empty() || text.size() > kManifestMax) return fail(error, "Manifest size is invalid");
    auto root = parse(text);
    if (!cJSON_IsObject(root.get()) || !unique_fields(root.get())) return fail(error, "Invalid or duplicate manifest fields");
    uint32_t abi = 0;
    if (!number(root.get(), "abi", &abi, 1, 1)) return fail(error, "App requires a supported ABI (1)");
    CrystalPackageManifest m;
    if (!text_field(root.get(), "id", &m.id, 63) || !crystal_package_valid_id(m.id) ||
        !text_field(root.get(), "name", &m.name, 31) || !text_field(root.get(), "version", &m.version, 23) ||
        !text_field(root.get(), "min_os", &m.min_os, 23) || !text_field(root.get(), "runtime", &m.runtime, 16) ||
        !text_field(root.get(), "entry", &m.entry, 160) || !relative(m.entry) || m.entry == "app.json" || m.entry.compare(0, 5, "data/") == 0 ||
        !text_field(root.get(), "icon", &m.icon, 160, false) || (!m.icon.empty() && (!relative(m.icon) || m.icon.compare(0, 7, "assets/"))) ||
        !text_field(root.get(), "bar_style", &m.bar_style, 5) || (m.bar_style != "dark" && m.bar_style != "light") ||
        !number(root.get(), "memory_kb", &m.memory_kb, 1, 8192) || !number(root.get(), "storage_kb", &m.storage_kb, 0, 5632) ||
        !number(root.get(), "worker_threads", &m.worker_threads, 0, 2)) return fail(error, "Invalid manifest metadata, paths or budgets");
    if (m.runtime != "native" && m.runtime != "lua" && m.runtime != "declarative") return fail(error, "Unknown runtime");
    std::array<unsigned, 3> version{}, required{}, os{};
    const char *os_version = hal().system_info ? hal().system_info->app_version() : "0.12.14";
    if (!semver(m.version, &version) || !semver(m.min_os, &required) || !semver(os_version, &os) || required > os) return fail(error, "App requires a newer OS or invalid version");
    auto *permissions = field(root.get(), "permissions");
    if (permissions && !cJSON_IsObject(permissions)) return fail(error, "Invalid permissions");
    const char *keys[] = {"notify", "audio", "system_timer", "location"};
    bool *values[] = {&m.notify, &m.audio, &m.system_timer, &m.location};
    for (unsigned i = 0; i < 4; ++i) {
        auto *v = field(permissions, keys[i]);
        if (v && !cJSON_IsBool(v)) return fail(error, "Permission must be a boolean");
        *values[i] = cJSON_IsTrue(v);
    }
    auto *hosts = field(permissions, "network_hosts");
    if (hosts && (!cJSON_IsArray(hosts) || cJSON_GetArraySize(hosts) > 16)) return fail(error, "Invalid network allowlist");
    for (const cJSON *h = hosts ? hosts->child : nullptr; h; h = h->next) {
        if (!cJSON_IsString(h) || !host_name(h->valuestring)) return fail(error, "Invalid network host");
        m.network_hosts.emplace_back(h->valuestring);
    }
    if (!m.network_hosts.empty() && !m.worker_threads) return fail(error, "Networking requires a worker thread");
    *out = std::move(m); return true;
}

bool crystal_packages_mount() {
#ifdef ESP_PLATFORM
    // Format only a blank factory partition. Never erase a damaged filesystem
    // automatically, since it may contain the user's installed apps and state.
    esp_vfs_littlefs_conf_t config{};
    config.base_path = "/littlefs"; config.partition_label = "apps";
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, "apps");
    if (!part) return false;
    std::array<uint8_t, 256> block{};
    bool blank = true;
    // Check both LittleFS superblock locations before allowing factory format.
    for (size_t offset : {size_t(0), size_t(4096)}) {
        if (esp_partition_read(part, offset, block.data(), block.size()) != ESP_OK) return false;
        for (auto b : block) if (b != 0xff) blank = false;
    }
    if (blank) {
        std::array<uint8_t, 2048> scan{};
        for (size_t offset = 0; offset < part->size && blank; offset += scan.size()) {
            size_t n = std::min(scan.size(), size_t(part->size) - offset);
            if (esp_partition_read(part, offset, scan.data(), n) != ESP_OK) return false;
            for (size_t i = 0; i < n; ++i) if (scan[i] != 0xff) { blank = false; break; }
        }
    }
    config.format_if_mount_failed = blank;
    return esp_vfs_littlefs_register(&config) == ESP_OK;
#else
    return true;
#endif
}

const std::vector<CrystalPackageRecord> &crystal_packages() { return s_records; }

bool crystal_packages_init(const char *root, CrystalPackageLimits limits, std::string *error) {
    if (!root || !*root || !limits.memory_bytes || !limits.storage_bytes) return fail(error, "Invalid package configuration");
    s_root.clear(); s_records.clear();
    const std::string base(root);
    if (!mkdir_ok(base) || !remove_tree(base + "/.staging")) return fail(error, "Could not prepare app storage");
    std::string saved;
    int status = read_blob("pkg.registry", &saved, kRegistryMax);
    auto previous = status > 0 ? parse(saved) : json(cJSON_CreateArray());
    if (status < 0 || !cJSON_IsArray(previous.get()) || !unique_fields(previous.get())) return fail(error, "Could not read package registry");
    if (cJSON_GetArraySize(previous.get()) > int(kAppsMax)) return fail(error, "Package registry exceeds app limit");
    std::vector<CrystalPackageRecord> old_records;
    std::set<std::string> old_ids;
    for (const cJSON *row = previous->child; row; row = row->next) {
        CrystalPackageRecord old;
        uint32_t slot = 0, errors = 0, installed = 0;
        if (!text_field(row, "id", &old.manifest.id, 63) || !crystal_package_valid_id(old.manifest.id) ||
            !old_ids.insert(old.manifest.id).second || !number(row, "slot", &slot, 0, 65534) ||
            !number(row, "errors", &errors, 0, 65535) || !number(row, "installed_at", &installed, 0, UINT32_MAX) ||
            !cJSON_IsBool(field(row, "enabled"))) return fail(error, "Invalid package registry metadata");
        old.slot = slot; old.error_count = errors; old.installed_at = installed;
        old.enabled = cJSON_IsTrue(field(row, "enabled"));
        auto *details = field(row, "details");
        if (!cJSON_IsString(details) || strlen(details->valuestring) > 255) return fail(error, "Invalid registry details");
        old.details = details->valuestring;
        old_records.push_back(std::move(old));
    }
    std::vector<CrystalPackageRecord> next;
    std::set<std::string> found;
    DIR *dir = opendir(base.c_str()); if (!dir) return fail(error, "Could not scan apps");
    bool ok = true;
    while (auto *ent = readdir(dir)) {
        std::string id(ent->d_name);
        if (!crystal_package_valid_id(id)) continue;
        std::string text;
        CrystalPackageManifest m;
        struct stat st{};
        if (path_stat((base + "/" + id).c_str(), &st) || !S_ISDIR(st.st_mode) ||
            !file_text(base + "/" + id + "/app.json", &text) || !crystal_package_parse_manifest(text, &m, nullptr) || m.id != id) continue;
        if (path_stat((base + "/" + id + "/" + m.entry).c_str(), &st) || !S_ISREG(st.st_mode)) continue;
        if (!m.icon.empty() && (path_stat((base + "/" + id + "/" + m.icon).c_str(), &st) || !S_ISREG(st.st_mode))) continue;
        if (next.size() >= kAppsMax || !mkdir_ok(base + "/" + id + "/data")) { ok = false; break; }
        CrystalPackageRecord r; r.manifest = std::move(m); r.slot = UINT16_MAX;
        for (const auto &old : old_records) if (old.manifest.id == id) {
            r.slot = old.slot; r.error_count = old.error_count; r.installed_at = old.installed_at;
            r.enabled = old.enabled; r.details = old.details;
        }
        if (!ok) break;
        found.insert(id); next.push_back(std::move(r));
    }
    closedir(dir);
    if (!ok) return fail(error, "App reconciliation failed");
    for (auto old : old_records) if (!found.count(old.manifest.id)) {
        struct stat st{};
        const int exists = path_stat((base + "/" + old.manifest.id).c_str(), &st);
        if (exists == 0) {
            // A damaged manifest is not a hand-deleted app. Keep its metadata
            // and state so repair or a later explicit uninstall can recover it.
            if (next.size() >= kAppsMax) return fail(error, "App limit reached during recovery");
            old.enabled = false; old.manifest.name = old.manifest.id;
            old.details = "App manifest or entry needs repair";
            found.insert(old.manifest.id); next.push_back(std::move(old));
        } else if (errno != ENOENT) return fail(error, "Could not inspect missing app");
    }
    // Stable deterministic slots for newly discovered folders, after saved slots.
    std::sort(next.begin(), next.end(), [](const CrystalPackageRecord &a, const CrystalPackageRecord &b) { return a.manifest.id < b.manifest.id; });
    uint32_t slot = 16;
    for (const auto &r : next) if (r.slot != UINT16_MAX) slot = std::max(slot, uint32_t(r.slot) + 1);
    for (auto &r : next) if (r.slot == UINT16_MAX) {
        if (slot >= UINT16_MAX) return fail(error, "No free launcher slot");
        r.slot = slot++;
    }
    // Tombstones make interrupted uninstalls restartable. Also clean state for
    // hand-deleted folders before pruning their metadata.
    for (const cJSON *row = previous->child; row; row = row->next) {
        auto *id = field(row, "id");
        if (!cJSON_IsString(id) || !crystal_package_valid_id(id->valuestring)) return fail(error, "Invalid registry identity");
        if (!found.count(id->valuestring) && !crystal_package_state_clear(id->valuestring)) return fail(error, "Could not erase removed app state");
    }
    s_root = base; s_limits = limits; s_records = std::move(next);
    if (!save_records()) { s_root.clear(); s_records.clear(); return fail(error, "Could not save reconciled registry"); }
    return true;
}

bool crystal_package_install(FILE *stream, bool developer_mode, std::string *error, crystal_package_verify_t verifier) {
#ifdef ESP_PLATFORM
    if (verifier == nullptr) verifier = crystal_signature_verify_package;
#endif
    if (!stream || s_root.empty()) return fail(error, "App storage is unavailable");
    if (s_records.size() >= kAppsMax) return fail(error, "App limit reached");
    const std::string staging = s_root + "/.staging";
    if (!remove_tree(staging) || !mkdir_ok(staging)) return fail(error, "Could not create staging directory");
    struct Prune { std::string path; ~Prune() { remove_tree(path); } } prune{staging};
    size_t consumed = 0;
    auto read = [&](void *data, size_t size) { if (fread(data, 1, size, stream) != size) return false; consumed += size; return true; };
    auto integer = [&](size_t size, uint32_t *out) {
        uint8_t bytes[4]{}; if (!read(bytes, size)) return false;
        *out = 0; for (size_t i = 0; i < size; ++i) *out = (*out << 8) | bytes[i]; return true;
    };
    char magic[4]; uint32_t version = 0, length = 0;
    if (!read(magic, 4) || memcmp(magic, "CAPP", 4) || !integer(2, &version) || version != 1 ||
        !integer(4, &length) || !length || length > kManifestMax) return fail(error, "Invalid CAPP header");
    std::string text(length, '\0');
    CrystalPackageManifest m;
    if (!read(&text[0], length)) return fail(error, "Truncated manifest");
    if (!crystal_package_parse_manifest(text, &m, error)) return false;
    if (find_record(m.id) != s_records.end()) return fail(error, "App already installed; uninstall before replacing");
    struct stat existing{};
    if (path_stat((s_root + "/" + m.id).c_str(), &existing) == 0 || errno != ENOENT) return fail(error, "App directory already exists");
    std::set<std::string> paths;
    size_t bytes_total = text.size();
    if (!admission(bytes_total, m, error)) return false;
    if (!write_file(staging + "/app.json", text)) return fail(error, "Could not write manifest");
    std::array<uint8_t, 2048> buffer{};
    for (;;) {
        if (!integer(2, &length)) return fail(error, "Truncated entry header");
        if (!length) break; // Signed zero path length is the canonical end marker.
        if (length > 160 || paths.size() >= 128) return fail(error, "Too many entries or invalid path length");
        std::string path(length, '\0'); uint32_t size = 0, expected = 0;
        if (!read(&path[0], length) || !relative(path) || path == "app.json" || path == "data" || path.compare(0, 5, "data/") == 0 ||
            path.front() == '.' || !paths.insert(path).second || !integer(4, &size) || !integer(4, &expected)) return fail(error, "Invalid, duplicate or reserved package path");
        if (size > s_limits.storage_bytes || bytes_total > s_limits.storage_bytes - size) return fail(error, "Package exceeds storage capacity");
        bytes_total += size;
        if (!admission(bytes_total, m, error)) return false;
        for (size_t pos = path.find('/'); pos != std::string::npos; pos = path.find('/', pos + 1))
            if (!mkdir_ok(staging + "/" + path.substr(0, pos))) return fail(error, "Could not create asset directory");
        FILE *out = fopen((staging + "/" + path).c_str(), "wb");
        if (!out) return fail(error, "Could not write package file");
        uint32_t crc = 0xffffffff; bool ok = true;
        while (size && ok) {
            size_t n = std::min(size_t(size), buffer.size());
            ok = read(buffer.data(), n) && fwrite(buffer.data(), 1, n, out) == n;
            if (!ok) break;
            for (size_t i = 0; i < n; ++i) { crc ^= buffer[i]; for (unsigned j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1))); }
            size -= n;
        }
        if (fflush(out) || fsync(fileno(out))) ok = false;
        if (fclose(out)) ok = false;
        if (!ok || (crc ^ 0xffffffff) != expected) return fail(error, "Truncated file, write error or CRC mismatch");
    }
    size_t signed_length = consumed;
    uint8_t algorithm, signature[64], key;
    if (!read(&algorithm, 1) || !read(signature, 64) || !read(&key, 1) || fgetc(stream) != EOF || ferror(stream)) return fail(error, "Invalid package trailer");
    if (algorithm == 0) {
        bool zero = key == 0; for (auto b : signature) zero = zero && b == 0;
        if (!developer_mode || !zero) return fail(error, "Unsigned package requires explicit Developer Mode");
    } else if (algorithm != 1 || !verifier || !verifier(stream, signed_length, signature, key)) return fail(error, "Package signature not trusted");
    if (!paths.count(m.entry) || (!m.icon.empty() && !paths.count(m.icon))) return fail(error, "Entry point or icon is missing");
    if (!mkdir_ok(staging + "/data")) return fail(error, "Could not create app data directory");
    if (rename(staging.c_str(), (s_root + "/" + m.id).c_str())) return fail(error, "Could not commit package directory");
    CrystalPackageRecord r; r.manifest = m; r.installed_at = static_cast<uint32_t>(time(nullptr));
    uint32_t slot = 16; for (const auto &row : s_records) slot = std::max(slot, uint32_t(row.slot) + 1);
    if (slot >= UINT16_MAX) { remove_tree(s_root + "/" + m.id); return fail(error, "No free launcher slot"); }
    r.slot = slot; s_records.push_back(std::move(r));
    if (!save_records()) {
        s_records.pop_back();
        // A committed directory left after failed rollback is recovered at boot.
        remove_tree(s_root + "/" + m.id);
        return fail(error, "Could not commit package registry");
    }
    if (error) error->clear();
    return true;
}

bool crystal_package_set_enabled(const std::string &id, bool enabled) {
    auto it = find_record(id); if (it == s_records.end()) return false;
    if (enabled && it->manifest.runtime.empty()) return false;
    auto old = *it; it->enabled = enabled;
    if (enabled) { it->error_count = 0; it->details.clear(); }
    if (save_records()) return true;
    *it = old; return false;
}
bool crystal_package_set_slot(const std::string &id, uint16_t slot) {
    auto it = find_record(id); if (it == s_records.end() || slot == UINT16_MAX) return false;
    auto old = it->slot; it->slot = slot;
    if (save_records()) return true;
    it->slot = old; return false;
}
bool crystal_package_launch_result(const std::string &id, bool success) {
    auto it = find_record(id); if (it == s_records.end()) return false;
    auto old = *it;
    if (success) { it->error_count = 0; it->details.clear(); }
    else {
        if (it->error_count < UINT16_MAX) ++it->error_count;
        it->details = "App failed to launch";
        if (it->error_count >= 3) it->enabled = false;
    }
    if (save_records()) return true;
    *it = old; return false;
}
bool crystal_package_remove(const std::string &id, bool cleanup_verified, std::string *error) {
    auto it = find_record(id); if (it == s_records.end()) return fail(error, "Package is not installed");
    if (!cleanup_verified) {
        auto old = *it; it->enabled = false; it->details = "Resources still active; restart before removing";
        if (!save_records()) { *it = old; return fail(error, "Resources active; could not persist disabled state"); }
        return fail(error, "Resources still active; app disabled and data retained");
    }
    if (!remove_tree(s_root + "/" + id)) return fail(error, "Could not remove app files; retry removal");
    if (!crystal_package_state_clear(id)) return fail(error, "Could not clear package state; retry removal");
    const auto old = s_records; s_records.erase(it);
    if (!save_records()) { s_records = old; return fail(error, "Could not prune registry; boot will reconcile"); }
    return true;
}

bool crystal_package_state_get(const std::string &id, const char *key, void *out, size_t *length) {
    if (!state_valid(id, key) || !out || !length) return false;
    auto root = load_state(id); if (!root) return false;
    auto *value = field(field(root.get(), "values"), (id + ":" + key).c_str());
    if (!cJSON_IsString(value)) return false;
    size_t chars = strlen(value->valuestring);
    if (chars % 2 || chars / 2 > *length) return false;
    auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    for (size_t i = 0; i < chars; ++i) if (digit(value->valuestring[i]) < 0) return false;
    for (size_t i = 0; i < chars / 2; ++i) static_cast<uint8_t *>(out)[i] = (digit(value->valuestring[2*i]) << 4) | digit(value->valuestring[2*i+1]);
    *length = chars / 2; return true;
}
bool crystal_package_state_set(const std::string &id, const char *key, const void *value, size_t length) {
    if (!state_valid(id, key) || !value || length > 1536) return false;
    auto root = load_state(id); if (!root) return false;
    auto *values = cJSON_GetObjectItemCaseSensitive(root.get(), "values");
    std::string full = id + ":" + key, hex(length * 2, '0');
    const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i) { auto b = static_cast<const uint8_t *>(value)[i]; hex[i*2] = digits[b>>4]; hex[i*2+1] = digits[b&15]; }
    cJSON_DeleteItemFromObjectCaseSensitive(values, full.c_str());
    if (!cJSON_AddStringToObject(values, full.c_str(), hex.c_str())) return false;
    auto serialized = dump(root.get());
    auto record = find_record(id);
    if (serialized.size() > kStateMax || serialized.size() > size_t(record->manifest.storage_kb) * 1024) return false;
    return write_blob(state_key(id).c_str(), serialized);
}
bool crystal_package_state_erase(const std::string &id, const char *key) {
    if (!state_valid(id, key)) return false;
    auto root = load_state(id); if (!root) return false;
    cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(root.get(), "values"), (id + ":" + key).c_str());
    return write_blob(state_key(id).c_str(), dump(root.get()));
}
bool crystal_package_state_clear(const std::string &id) {
    if (!crystal_package_valid_id(id) || !hal().storage) return false;
    // Store an empty owner envelope: idempotent even when no blob exists, and
    // verifies a hash collision before touching another app's state.
    auto root = load_state(id); if (!root) return false;
    cJSON_DeleteItemFromObjectCaseSensitive(root.get(), "values");
    if (!cJSON_AddObjectToObject(root.get(), "values")) return false;
    auto key = state_key(id);
    if (!write_blob(key.c_str(), dump(root.get()))) return false;
    return hal().storage->erase(key.c_str());
}
