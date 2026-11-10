/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include <stddef.h>

// Service-task-only adapter to the shared HTTP queue. Never call from LVGL
// or from the HTTP worker's callbacks. Output is empty on any failure.
bool crystal_core_http_get_json(const char *url, char *output, size_t capacity);
