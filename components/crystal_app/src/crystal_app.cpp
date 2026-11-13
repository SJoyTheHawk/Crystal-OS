/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "crystal_app.hpp"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "crystal_hal.hpp"
#include "crystal_package.hpp"
#include "esp_log.h"
#include "lvgl.h"
#include "nvs.h"

static const char *TAG = "crystal_app";

static crystal_app_log_hook_t s_log_hook = nullptr;
static void control_log(const char *app_id, const char *line)
{
    if (s_log_hook != nullptr) s_log_hook(app_id, line);
}

namespace {
constexpr size_t kMaxStateBytes = 2048;
constexpr size_t kMaxKeyBytes = 7;
crystal_shell_back_hook_t s_shell_back_hook = nullptr;
crystal_app_cleanup_hook_t s_cleanup_hook = nullptr;

ESP_Brookesia_PhoneAppData_t crystal_phone_app_data(const void *launcher_icon)
{
    ESP_Brookesia_PhoneAppData_t data =
        ESP_BROOKESIA_PHONE_APP_DATA_DEFAULT(launcher_icon, true, false);
    // Crystal arbitrates the bottom edge so a shell page is dismissed before
    // the card underneath it is sent home.
    data.flags.enable_navigation_gesture = 0;
    return data;
}
}

void crystal_app_set_shell_back_hook(crystal_shell_back_hook_t hook)
{
    s_shell_back_hook = hook;
}

void crystal_app_set_cleanup_hook(crystal_app_cleanup_hook_t hook) { s_cleanup_hook = hook; }
void crystal_app_set_log_hook(crystal_app_log_hook_t hook) { s_log_hook = hook; }

CrystalState::CrystalState(const char *app_name, bool package)
    : prefix_("a"), package_(package)
{
    if (package_) { prefix_ = app_name ? app_name : ""; return; }
    uint32_t hash = 2166136261u;
    if (app_name != nullptr) {
        for (const unsigned char *p = reinterpret_cast<const unsigned char *>(app_name); *p != '\0'; ++p) {
            hash ^= *p;
            hash *= 16777619u;
        }
    }
    char suffix[8];
    snprintf(suffix, sizeof(suffix), "%06lx", static_cast<unsigned long>(hash & 0xFFFFFFu));
    prefix_ += suffix;
    prefix_ += ".";
}

bool CrystalState::make_key(const char *key, std::string *out) const
{
    if (key == nullptr || out == nullptr || key[0] == '\0' || strlen(key) > kMaxKeyBytes) {
        return false;
    }
    *out = prefix_ + key;
    return out->size() < 64;
}

bool CrystalState::get(const char *key, void *value, size_t *length) const
{
    if (package_) return crystal_package_state_get(prefix_, key, value, length);
    std::string full_key;
    if (value == nullptr || length == nullptr || *length > kMaxStateBytes || !make_key(key, &full_key)) {
        return false;
    }
    return hal().storage != nullptr && hal().storage->get(full_key.c_str(), value, length);
}

bool CrystalState::set(const char *key, const void *value, size_t length)
{
    if (package_) return crystal_package_state_set(prefix_, key, value, length);
    std::string full_key;
    if (value == nullptr || length > kMaxStateBytes || !make_key(key, &full_key)) {
        return false;
    }
    return hal().storage != nullptr && hal().storage->set(full_key.c_str(), value, length);
}

bool CrystalState::erase(const char *key)
{
    if (package_) return crystal_package_state_erase(prefix_, key);
    std::string full_key;
    if (!make_key(key, &full_key)) {
        return false;
    }
    return hal().storage != nullptr && hal().storage->erase(full_key.c_str());
}

bool CrystalState::clear()
{
    if (package_) return crystal_package_state_clear(prefix_);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("crystal", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "clear '%s': nvs_open failed: %s", prefix_.c_str(), esp_err_to_name(err));
        return false;
    }
    struct CloseHandle {
        nvs_handle_t handle;
        ~CloseHandle() { nvs_close(handle); }
    } close_handle{handle};
    struct ReleaseIterator {
        nvs_iterator_t value = nullptr;
        ~ReleaseIterator() { nvs_release_iterator(value); }
    } iterator;

    // Enumerate the opened namespace using the ESP-IDF 6.1 iterator API.
    // Finish the scan before mutating NVS; a failed scan must not erase a
    // partial list. Include the trailing dot when matching the app prefix.
    std::vector<std::string> keys;
    err = nvs_entry_find_in_handle(handle, NVS_TYPE_ANY, &iterator.value);
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        err = nvs_entry_info(iterator.value, &info);
        if (err != ESP_OK) break;
        if (strncmp(info.key, prefix_.c_str(), prefix_.size()) == 0) {
            keys.emplace_back(info.key);
        }
        err = nvs_entry_next(&iterator.value);
    }
    // The iterator API may already have released it on reaching the end.
    nvs_release_iterator(iterator.value);
    iterator.value = nullptr;
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "clear '%s': NVS enumeration failed: %s", prefix_.c_str(), esp_err_to_name(err));
        return false;
    }

    bool ok = true;
    size_t erased = 0;
    for (const auto &key : keys) {
        err = nvs_erase_key(handle, key.c_str());
        if (err == ESP_OK) {
            ++erased;
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "clear '%s': erase %s failed: %s",
                     prefix_.c_str(), key.c_str(), esp_err_to_name(err));
            ok = false;
        }
    }
    if (!keys.empty()) {
        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "clear '%s': nvs_commit failed: %s", prefix_.c_str(), esp_err_to_name(err));
            ok = false;
        }
    }
    if (ok) {
        ESP_LOGI(TAG, "Cleared %zu keys under prefix '%s'", erased, prefix_.c_str());
    }
    return ok;
}

bool CrystalState::get_u32(const char *key, uint32_t *value) const
{
    size_t length = sizeof(*value);
    return value != nullptr && get(key, value, &length) && length == sizeof(*value);
}

bool CrystalState::set_u32(const char *key, uint32_t value)
{
    return set(key, &value, sizeof(value));
}

CrystalApp::CrystalApp(const char *name, const void *launcher_icon)
    : ESP_Brookesia_PhoneApp(
          ESP_BROOKESIA_CORE_APP_DATA_DEFAULT(name, launcher_icon, true),
          crystal_phone_app_data(launcher_icon)),
      app_name_(name != nullptr ? name : "<unnamed>"), state_(name)
{
}

bool CrystalApp::bindPackage(const char *id, size_t memory_bytes, unsigned workers)
{
    if (lifecycle_state_ != LifecycleState::Destroyed || !id ||
        !crystal_package_valid_id(id) || !resources_.configure(memory_bytes, workers)) return false;
    package_id_ = id;
    state_ = CrystalState(id, true);
    return true;
}

bool CrystalApp::trackObject(lv_obj_t *object)
{
    if (!resources_.track(CrystalResourceKind::Object, object)) return false;
    lv_obj_add_event_cb(object, [](lv_event_t *event) {
        auto *resources = static_cast<CrystalResources *>(lv_event_get_user_data(event));
        resources->release(CrystalResourceKind::Object, lv_event_get_target(event));
    }, LV_EVENT_DELETE, &resources_);
    return true;
}

// Brookesia's init()/deinit() are lifecycle bookkeeping only. They forward to no
// app hook: setup belongs in onCreate(), and boot-time reconciliation of service
// state belongs to the owning service.
bool CrystalApp::clearData()
{
    if (lifecycle_state_ != LifecycleState::Installed &&
        lifecycle_state_ != LifecycleState::Destroyed) return false;
    if (!resources_.report().clean()) return false;
    onClearData();
    return state_.clear();
}

bool CrystalApp::init()
{
    assert(lifecycle_state_ == LifecycleState::Destroyed);
    lifecycle_state_ = LifecycleState::Installed;
    return true;
}

bool CrystalApp::deinit()
{
    lifecycle_state_ = LifecycleState::Destroyed;
    return true;
}

bool CrystalApp::run()
{
    assert(lifecycle_state_ == LifecycleState::Installed ||
           lifecycle_state_ == LifecycleState::Destroyed);
    if (!resources_.report().clean()) {
        if (s_cleanup_hook) s_cleanup_hook(this);
        if (!package_id_.empty()) (void)crystal_package_set_enabled(package_id_, false);
        return false;
    }
    if (!package_id_.empty()) {
        bool enabled = false;
        for (const auto &r : crystal_packages()) if (r.manifest.id == package_id_) enabled = r.enabled;
        if (!enabled || !resources_.report().clean()) return false;
    }
    ESP_LOGI(TAG, "%s lifecycle: onCreate", app_name_.c_str());
    control_log(package_id_.empty() ? app_name_.c_str() : package_id_.c_str(), "onCreate");
    const uint32_t start = lv_tick_get();
    const bool ok = onCreate();
    const uint32_t elapsed = lv_tick_elaps(start);
    if (elapsed > startup_budget_ms_) {
        ESP_LOGW(TAG, "%s onCreate took %lu ms (budget %lu ms)",
                 app_name_.c_str(), static_cast<unsigned long>(elapsed), static_cast<unsigned long>(startup_budget_ms_));
    }
    if (ok) {
        lifecycle_state_ = LifecycleState::Created;
    } else {
        (void)onDestroy();
        lifecycle_state_ = LifecycleState::Destroyed;
        if (s_cleanup_hook) s_cleanup_hook(this);
    }
    if (!package_id_.empty()) (void)crystal_package_launch_result(package_id_, ok);
    return ok;
}

bool CrystalApp::pause()
{
    if (lifecycle_state_ == LifecycleState::Paused) {
        return true;
    }
    assert(is_active());
    ESP_LOGI(TAG, "%s lifecycle: onPause", app_name_.c_str());
    control_log(package_id_.empty() ? app_name_.c_str() : package_id_.c_str(), "onPause");
    if (!onPause()) {
        ESP_LOGW(TAG, "%s onPause reported failure; continuing teardown", app_name_.c_str());
    }
    lifecycle_state_ = LifecycleState::Paused;
    return true;
}

bool CrystalApp::resume()
{
    assert(lifecycle_state_ == LifecycleState::Paused);
    ESP_LOGI(TAG, "%s lifecycle: onResume", app_name_.c_str());
    control_log(package_id_.empty() ? app_name_.c_str() : package_id_.c_str(), "onResume");
    const uint32_t start = lv_tick_get();
    const bool ok = onResume();
    const uint32_t elapsed = lv_tick_elaps(start);
    if (elapsed > 80) {
        ESP_LOGW(TAG, "%s onResume took %lu ms (budget 80 ms)",
                 app_name_.c_str(), static_cast<unsigned long>(elapsed));
    }
    if (ok) {
        lifecycle_state_ = LifecycleState::Resumed;
    }
    return ok;
}
bool CrystalApp::close()
{
    if (lifecycle_state_ == LifecycleState::Destroyed) {
        return true;
    }
    if (is_active() && lifecycle_state_ != LifecycleState::Paused) {
        (void)pause();
    }
    ESP_LOGI(TAG, "%s lifecycle: onDestroy", app_name_.c_str());
    control_log(package_id_.empty() ? app_name_.c_str() : package_id_.c_str(), "onDestroy");
    if (!onDestroy()) {
        ESP_LOGW(TAG, "%s onDestroy reported failure; continuing teardown", app_name_.c_str());
    }
    lifecycle_state_ = LifecycleState::Destroyed;
    if (!package_id_.empty()) {
        CrystalCleanupReport report;
        // A surviving worker must never be hidden by resetting its ledger.
        if (!crystal_app_verify_cleanup(resources_, &report)) {
            ESP_LOGE(TAG, "%s resources remain: tasks=%zu timers=%zu queues=%zu heap=%zu objects=%zu",
                     package_id_.c_str(), report.tasks, report.timers, report.queues, report.heap_bytes, report.objects);
            control_log(package_id_.c_str(), "resources remain during teardown");
            (void)crystal_package_set_enabled(package_id_, false);
        }
    }
    if (s_cleanup_hook) s_cleanup_hook(this);
    return true;
}

bool CrystalApp::back()
{
    assert(is_active());
    ESP_LOGI(TAG, "%s lifecycle: onBack", app_name_.c_str());
    return onBack();
}

bool CrystalApp::onBack()
{
    if (s_shell_back_hook != nullptr && s_shell_back_hook()) return true;
    return notifyCoreClosed();
}

bool CrystalApp::is_active() const
{
    return lifecycle_state_ == LifecycleState::Created ||
           lifecycle_state_ == LifecycleState::Started ||
           lifecycle_state_ == LifecycleState::Resumed;
}
