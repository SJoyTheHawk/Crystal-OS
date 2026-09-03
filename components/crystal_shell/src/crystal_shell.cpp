/* SPDX-License-Identifier: MIT */

#include "crystal_shell.hpp"

#include <stdlib.h>
#include <string.h>

#include "crystal_app.hpp"
#include "crystal_hal.hpp"
#include "crystal_registry.hpp"
#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "crystal_shell";

namespace {
constexpr int kSwitchDistance = 240;
constexpr char kCurrentCardKey[] = "shell.card";

ESP_Brookesia_Phone *s_phone = nullptr;
size_t s_current_index = 0;
bool s_switching = false;

bool persist_current_card()
{
    const char *id = crystal_registry_installed_id(s_current_index);
    if (id == nullptr || hal().storage == nullptr) {
        return false;
    }
    return hal().storage->set(kCurrentCardKey, id, strlen(id) + 1);
}

size_t load_current_index()
{
    char stored_id[32] = {};
    size_t length = sizeof(stored_id);
    if (hal().storage == nullptr ||
            !hal().storage->get(kCurrentCardKey, stored_id, &length) ||
            length == 0 || length > sizeof(stored_id) || stored_id[length - 1] != '\0') {
        return 0;
    }

    for (size_t i = 0; i < crystal_registry_installed_count(); ++i) {
        const char *id = crystal_registry_installed_id(i);
        if (id != nullptr && strcmp(id, stored_id) == 0) {
            return i;
        }
    }
    return 0;
}

bool start_card(size_t index)
{
    CrystalApp *app = crystal_registry_installed_app(index);
    if (s_phone == nullptr || app == nullptr || index >= crystal_registry_installed_count()) {
        return false;
    }

    ESP_Brookesia_CoreAppEventData_t event_data = {
        .id = app->getId(),
        .type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START,
        .data = nullptr,
    };
    s_switching = true;
    const bool sent = s_phone->sendAppEvent(&event_data);
    s_switching = false;
    if (!sent || s_phone->getManager().getActiveApp() != app) {
        ESP_LOGE(TAG, "failed to open card %u (%s)", static_cast<unsigned>(index),
                 crystal_registry_installed_id(index));
        return false;
    }

    s_current_index = index;
    if (!persist_current_card()) {
        ESP_LOGW(TAG, "failed to persist current card");
    }
    ESP_LOGI(TAG, "active card %u/%u: %s", static_cast<unsigned>(index + 1),
             static_cast<unsigned>(crystal_registry_installed_count()),
             crystal_registry_installed_id(index));
    return true;
}

void on_app_event(lv_event_t *event)
{
    auto *data = static_cast<ESP_Brookesia_CoreAppEventData_t *>(lv_event_get_param(event));
    if (s_switching || data == nullptr || data->type != ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START) {
        return;
    }
    for (size_t i = 0; i < crystal_registry_installed_count(); ++i) {
        CrystalApp *app = crystal_registry_installed_app(i);
        if (app != nullptr && app->getId() == data->id) {
            s_current_index = i;
            (void)persist_current_card();
            return;
        }
    }
}

void on_gesture_release(lv_event_t *event)
{
    auto *info = static_cast<ESP_Brookesia_GestureInfo_t *>(lv_event_get_param(event));
    if (info == nullptr || s_phone == nullptr || s_switching ||
            s_phone->getManager().getActiveApp() == nullptr) {
        return;
    }

    const int dx = info->stop_x - info->start_x;
    const bool previous = (info->start_area & ESP_BROOKESIA_GESTURE_AREA_LEFT_EDGE) &&
                          info->direction == ESP_BROOKESIA_GESTURE_DIR_RIGHT &&
                          dx >= kSwitchDistance;
    const bool next = (info->start_area & ESP_BROOKESIA_GESTURE_AREA_RIGHT_EDGE) &&
                      info->direction == ESP_BROOKESIA_GESTURE_DIR_LEFT &&
                      dx <= -kSwitchDistance;

    if (previous && s_current_index > 0) {
        (void)start_card(s_current_index - 1);
    } else if (next && s_current_index + 1 < crystal_registry_installed_count()) {
        (void)start_card(s_current_index + 1);
    }
}
}

bool crystal_shell_init(ESP_Brookesia_Phone *phone)
{
    if (phone == nullptr || crystal_registry_installed_count() == 0) {
        return false;
    }

    ESP_Brookesia_Gesture *gesture = phone->getManager().getGesture();
    if (gesture == nullptr || gesture->getEventObj() == nullptr) {
        ESP_LOGE(TAG, "Brookesia gesture source is unavailable");
        return false;
    }

    s_phone = phone;
    s_current_index = load_current_index();
    lv_obj_add_event_cb(gesture->getEventObj(), on_gesture_release,
                        gesture->getReleaseEventCode(), nullptr);
    if (!phone->registerAppEventCallback(on_app_event, nullptr)) {
        ESP_LOGE(TAG, "failed to observe app starts");
        return false;
    }

    if (!start_card(s_current_index) && s_current_index != 0) {
        ESP_LOGW(TAG, "saved card unavailable; falling back to slot 0");
        return start_card(0);
    }
    return phone->getManager().getActiveApp() != nullptr;
}
