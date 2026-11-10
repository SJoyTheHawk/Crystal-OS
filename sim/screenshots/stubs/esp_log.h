#pragma once
#include <cstdio>
#define ESP_LOGI(tag, ...) do { std::printf(__VA_ARGS__); std::puts(""); } while (0)
#define ESP_LOGW ESP_LOGI
#define ESP_LOGE ESP_LOGI
#define ESP_LOGD ESP_LOGI
