/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include <stddef.h>
#include "esp_heap_caps.h"

// Widget trees and JSON are cache-enabled task data, not DMA buffers. Never
// fall back to internal RAM: that pool is needed by task stacks and locks.
static inline void *crystal_ui_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static inline void *crystal_ui_realloc(void *ptr, size_t size)
{
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static inline void crystal_ui_free(void *ptr)
{
    heap_caps_free(ptr);
}

#ifdef __cplusplus
extern "C" {
#endif
// Call once, before any cJSON objects or background tasks are created.
void crystal_memory_init(void);
#ifdef __cplusplus
}
#endif
