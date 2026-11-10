/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_phase14_device_test.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "crystal_app.hpp"
#include "crystal_hal.hpp"
#include "crystal_package.hpp"
#include "crystal_registry.hpp"
#include "esp_log.h"
#include "lvgl.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace {
constexpr const char *TAG = "phase14_test";
constexpr const char *kId = "org.crystal.phase14.calculator";
constexpr const char *kCheckpointKey = "phase14.test";
constexpr const char *kFormula = "128x3+64";
constexpr uint32_t kMagic = 0x50313401;
constexpr unsigned kCycles = 10;
enum class Phase : uint32_t { Running = 1, AwaitingReboot, Passed, Failed };
enum class Step { Install, Launch, Enter, Close, Saved, Reopen, Restored, Remove, Restart };
enum class Mode { Cycles, BeforeReboot, AfterReboot, FreshAfterRemoval };

struct Checkpoint {
    uint32_t magic = kMagic;
    uint32_t run_id = 1;
    Phase phase = Phase::Running;
    uint32_t cycles = 0;
    uint32_t builtin_length = 0;
    uint32_t builtin_present = 0;
    char builtin_formula[64]{};
};
struct Test {
    ESP_Brookesia_Phone *phone = nullptr;
    const uint8_t *package = nullptr;
    size_t package_size = 0;
    std::string root;
    void (*restart)() = nullptr;
    lv_timer_t *timer = nullptr;
    Checkpoint checkpoint;
    Step step = Step::Install;
    Mode mode = Mode::Cycles;
    CrystalPhase14TestStatus status = CrystalPhase14TestStatus::Idle;
};
Test s_test;

bool save_checkpoint()
{
    return hal().storage && hal().storage->set(kCheckpointKey, &s_test.checkpoint, sizeof(Checkpoint));
}

void stop_timer()
{
    if (s_test.timer) {
        lv_timer_del(s_test.timer);
        s_test.timer = nullptr;
    }
}

bool failed(const char *message)
{
    ESP_LOGE(TAG, "FAIL: %s (cycle %lu, step %u). Test stopped; inspect before retrying.",
             message, static_cast<unsigned long>(s_test.checkpoint.cycles), static_cast<unsigned>(s_test.step));
    s_test.status = CrystalPhase14TestStatus::Failed;
    s_test.checkpoint.phase = Phase::Failed;
    if (!save_checkpoint()) ESP_LOGE(TAG, "Could not save failure checkpoint");
    stop_timer();
    return false;
}

CrystalApp *test_app()
{
    for (size_t i = 0; i < crystal_registry_installed_count(); ++i) {
        const char *id = crystal_registry_installed_id(i);
        if (id && !strcmp(id, kId)) return crystal_registry_installed_app(i);
    }
    return nullptr;
}

bool has_record()
{
    for (const auto &record : crystal_packages()) if (record.manifest.id == kId) return true;
    return false;
}

bool builtin_unchanged()
{
    CrystalState bundled("Calculator");
    char formula[64]{};
    size_t length = sizeof(formula);
    const bool present = bundled.get("formula", formula, &length);
    const auto &saved = s_test.checkpoint;
    return present == bool(saved.builtin_present) && (!present ||
        (length == saved.builtin_length && !memcmp(formula, saved.builtin_formula, length)));
}

bool saved_formula(CrystalApp *app)
{
    char formula[64]{};
    size_t length = sizeof(formula);
    return app && app->state().get("formula", formula, &length) &&
           length == strlen(kFormula) + 1 && !memcmp(formula, kFormula, length);
}

lv_obj_t *keyboard(lv_obj_t *parent)
{
    if (lv_obj_check_type(parent, &lv_btnmatrix_class)) return parent;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); ++i)
        if (auto *found = keyboard(lv_obj_get_child(parent, i))) return found;
    return nullptr;
}

bool label_exists(lv_obj_t *parent, const char *text)
{
    if (lv_obj_check_type(parent, &lv_label_class) && !strcmp(lv_label_get_text(parent), text)) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); ++i)
        if (label_exists(lv_obj_get_child(parent, i), text)) return true;
    return false;
}

bool send_event(CrystalApp *app, ESP_Brookesia_CoreAppEventType_t type)
{
    if (!app) return false;
    ESP_Brookesia_CoreAppEventData_t event{app->getId(), type, nullptr};
    return s_test.phone->sendAppEvent(&event);
}

bool clean(CrystalApp *app)
{
    CrystalCleanupReport report;
    if (app && crystal_app_verify_cleanup(app->resources(), &report)) return true;
    if (app) ESP_LOGE(TAG, "Resources: tasks=%zu timers=%zu queues=%zu allocations=%zu objects=%zu bytes=%zu",
                     report.tasks, report.timers, report.queues, report.allocations, report.objects, report.heap_bytes);
    return false;
}

void memory_sample()
{
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Free internal DRAM=%zu bytes (largest block=%zu), PSRAM=%zu bytes",
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#else
    if (hal().system_info) ESP_LOGI(TAG, "Free heap=%lu bytes, PSRAM=%lu bytes",
        static_cast<unsigned long>(hal().system_info->free_heap()),
        static_cast<unsigned long>(hal().system_info->free_psram()));
#endif
}

bool install()
{
    // The read-only stream references the fixture in flash; the packager never
    // receives ownership of that buffer and cannot write through this stream.
    FILE *stream = fmemopen(const_cast<uint8_t *>(s_test.package), s_test.package_size, "rb");
    if (!stream) return failed("Could not open embedded development fixture");
    std::string error;
    const bool installed = crystal_package_install(stream, true, &error);
    fclose(stream);
    if (!installed) return failed(error.c_str());
    if (!crystal_registry_apply_changes() || !test_app()) return failed("Could not add Calculator Test to launcher");
    uint8_t value[64]{};
    size_t length = sizeof(value);
    if (test_app()->state().get("formula", value, &length)) return failed("Uninstalled package state survived a fresh install");
    return true;
}

void tick(lv_timer_t *)
{
    CrystalApp *app = test_app();
    switch (s_test.step) {
    case Step::Install:
        if (!install()) return;
        ESP_LOGI(TAG, "PASS: installed Calculator Test from embedded .capp");
        s_test.step = Step::Launch;
        break;
    case Step::Launch:
        if (!send_event(app, ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START) ||
            s_test.phone->getManager().getActiveApp() != app) { failed("Calculator Test did not launch"); return; }
        s_test.step = Step::Enter;
        break;
    case Step::Enter: {
        auto *keys = keyboard(lv_scr_act());
        if (!keys) { failed("Calculator keyboard missing"); return; }
        // C, then 128 x 3 + 64 through the production input callback.
        for (uint16_t id : {0, 12, 13, 5, 2, 14, 11, 10, 8}) {
            lv_btnmatrix_set_selected_btn(keys, id);
            if (lv_event_send(keys, LV_EVENT_VALUE_CHANGED, nullptr) != LV_RES_OK) { failed("Calculator input failed"); return; }
        }
        if (!label_exists(lv_scr_act(), kFormula) || !label_exists(lv_scr_act(), "= 448")) {
            failed("Calculator displayed an unexpected formula or result"); return;
        }
        s_test.step = Step::Close;
        break;
    }
    case Step::Close:
        if (!send_event(app, ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP)) { failed("Calculator Test did not close"); return; }
        s_test.step = Step::Saved;
        break;
    case Step::Saved:
        if (!clean(app) || !saved_formula(app) || !builtin_unchanged()) {
            failed("Close leaked resources, failed to save, or changed bundled Calculator state"); return;
        }
        if (s_test.mode == Mode::BeforeReboot) {
            s_test.checkpoint.phase = Phase::AwaitingReboot;
            if (!save_checkpoint()) { failed("Could not commit reboot checkpoint"); return; }
            ESP_LOGI(TAG, "PASS: formula saved. Rebooting once to verify persistence.");
            s_test.step = Step::Restart;
        } else s_test.step = Step::Reopen;
        break;
    case Step::Reopen:
        // The shell can already have restored its remembered card at boot.
        // Brookesia treats a repeated START as resume, which requires Paused.
        if (!saved_formula(app) ||
            (s_test.phone->getManager().getActiveApp() != app &&
             !send_event(app, ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START)) ||
            s_test.phone->getManager().getActiveApp() != app) { failed("Saved package could not reopen"); return; }
        s_test.step = Step::Restored;
        break;
    case Step::Restored:
        if (!label_exists(lv_scr_act(), kFormula) || !label_exists(lv_scr_act(), "= 448")) {
            failed("Formula was not restored into the real Calculator UI"); return;
        }
        if (s_test.mode == Mode::AfterReboot) ESP_LOGI(TAG, "PASS: formula and result restored after reboot");
        if (!send_event(app, ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP)) { failed("Restored Calculator did not close"); return; }
        s_test.step = Step::Remove;
        break;
    case Step::Remove: {
        if (!clean(app) || !builtin_unchanged()) { failed("Cleanup or builtin state isolation failed"); return; }
        if (!crystal_registry_uninstall_package(kId)) { failed("Registry refused package uninstall"); return; }
        struct stat st{};
        if (test_app() || has_record() || stat((s_test.root + "/" + kId).c_str(), &st) == 0) {
            failed("Uninstall left an instance, registry row or directory"); return;
        }
        if (s_test.mode == Mode::Cycles) {
            ++s_test.checkpoint.cycles;
            if (!save_checkpoint()) { failed("Could not save cycle checkpoint"); return; }
            ESP_LOGI(TAG, "PASS: cycle %lu/10 install/launch/save/reopen/uninstall; zero tracked leaks",
                     static_cast<unsigned long>(s_test.checkpoint.cycles));
            memory_sample();
            if (s_test.checkpoint.cycles == kCycles) s_test.mode = Mode::BeforeReboot;
            s_test.step = Step::Install;
        } else if (s_test.mode == Mode::AfterReboot) {
            // Reinstall once to prove no old state can reappear under this ID.
            s_test.mode = Mode::FreshAfterRemoval;
            s_test.step = Step::Install;
        } else if (s_test.mode == Mode::FreshAfterRemoval) {
            s_test.checkpoint.phase = Phase::Passed;
            if (!save_checkpoint()) { failed("Could not save completed result"); return; }
            s_test.status = CrystalPhase14TestStatus::Passed;
            stop_timer();
            ESP_LOGI(TAG, "PASS: Phase 14 device test complete (run %lu). 10 cycles, reboot persistence, fresh state and cleanup verified.",
                     static_cast<unsigned long>(s_test.checkpoint.run_id));
            memory_sample();
        }
        break;
    }
    case Step::Restart:
        s_test.status = CrystalPhase14TestStatus::AwaitingReboot;
        stop_timer();
        s_test.restart();
        break;
    }
}
}

CrystalPhase14TestStatus crystal_phase14_device_test_status() { return s_test.status; }

bool crystal_phase14_device_test_start(ESP_Brookesia_Phone *phone,
                                      const uint8_t *package, size_t package_size,
                                      const char *apps_root, void (*restart)(), uint32_t run_id)
{
    if (s_test.timer || !phone || !package || !package_size || !apps_root || !restart || !run_id || !hal().storage) return false;
    s_test = Test{};
    s_test.phone = phone; s_test.package = package; s_test.package_size = package_size;
    s_test.root = apps_root; s_test.restart = restart;
    s_test.checkpoint.run_id = run_id;
    Checkpoint previous{};
    size_t length = sizeof(previous);
    const bool stored = hal().storage->get(kCheckpointKey, &previous, &length);
    if (stored && (length != sizeof(previous) || previous.magic != kMagic || previous.builtin_length > 64 || previous.cycles > kCycles))
        return failed("Invalid checkpoint; existing package data retained");
    if (stored && previous.run_id == run_id) {
        s_test.checkpoint = previous;
        if (previous.phase == Phase::Passed) {
            s_test.status = CrystalPhase14TestStatus::Passed;
            ESP_LOGI(TAG, "PASS: Phase 14 device test was already completed (run %lu). Increase CRYSTAL_PHASE14_DEVICE_TEST_RUN to repeat.",
                     static_cast<unsigned long>(run_id));
            return true;
        }
        if (previous.phase != Phase::AwaitingReboot || previous.cycles != kCycles)
            return failed("Previous test was interrupted or failed. Inspect it, then use a new run ID.");
        if (!test_app() || !has_record() || !saved_formula(test_app()) || !builtin_unchanged())
            return failed("Package, saved formula or builtin state did not survive reboot");
        s_test.mode = Mode::AfterReboot;
        s_test.step = Step::Reopen;
        ESP_LOGI(TAG, "Resuming Phase 14 device test after reboot");
    } else {
        struct stat st{};
        if (has_record() || stat((s_test.root + "/" + kId).c_str(), &st) == 0)
            return failed("Calculator Test already exists; remove it explicitly before starting a new run");
        // Crystal opens a startup card. Stop it before taking the baseline so
        // its legitimate onPause save is included in the isolation snapshot.
        if (auto *active = phone->getManager().getActiveApp()) {
            ESP_Brookesia_CoreAppEventData_t event{active->getId(), ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP, nullptr};
            if (!phone->sendAppEvent(&event) || phone->getManager().getActiveApp())
                return failed("Could not close startup card before test");
        }
        CrystalState builtin("Calculator");
        size_t size = sizeof(s_test.checkpoint.builtin_formula);
        s_test.checkpoint.builtin_present = builtin.get("formula", s_test.checkpoint.builtin_formula, &size);
        s_test.checkpoint.builtin_length = s_test.checkpoint.builtin_present ? size : 0;
        if (!save_checkpoint()) return failed("Could not start persistent test checkpoint");
        ESP_LOGI(TAG, "Starting Phase 14 device test (run %lu). Leave the display untouched; one automatic reboot is expected.",
                 static_cast<unsigned long>(run_id));
        memory_sample();
    }
    s_test.status = CrystalPhase14TestStatus::Running;
    s_test.timer = lv_timer_create(tick, 500, nullptr);
    if (!s_test.timer) return failed("Could not create device test timer");
    return true;
}
