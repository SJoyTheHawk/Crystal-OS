#!/usr/bin/env python3
"""Check production LVGL and JSON PSRAM allocation with simulated heap caps."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "crystal_memory.h"
#include "cJSON.h"
#include "lvgl.h"
static unsigned allocations, reallocations, live;
static int fail_alloc;
void *heap_caps_malloc(size_t n, unsigned caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ++allocations; if (fail_alloc) return NULL;
    void *p = malloc(n); if (p) ++live; return p;
}
void *heap_caps_realloc(void *p, size_t n, unsigned caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ++reallocations; if (fail_alloc) return NULL;
    int was_null = p == NULL;
    void *q = realloc(p, n); if (q && was_null) ++live; return q;
}
void heap_caps_free(void *p) { if (p) { assert(live); --live; free(p); } }
int main(void) {
    crystal_memory_init(); lv_mem_init();
    char *p = lv_mem_alloc(16); assert(p && allocations);
    memcpy(p, "widget", 7);
    p = lv_mem_realloc(p, 128); assert(p && reallocations && !strcmp(p, "widget"));
    fail_alloc = 1;
    assert(lv_mem_alloc(16) == NULL);
    assert(lv_mem_realloc(p, 256) == NULL && !strcmp(p, "widget"));
    assert(cJSON_Parse("[42]") == NULL);
    fail_alloc = 0; lv_mem_free(p); assert(live == 0);
    unsigned before = allocations;
    cJSON *j = cJSON_Parse("[1,2,3]");
    assert(j && allocations > before);
    char *s = cJSON_PrintUnformatted(j); assert(s);
    cJSON_free(s); cJSON_Delete(j); assert(live == 0);
    void *scratch = lv_mem_buf_get(64); assert(scratch);
    lv_mem_buf_release(scratch); lv_mem_buf_free_all(); assert(live == 0);
    puts("PASS: LVGL/JSON PSRAM caps, realloc preservation, allocation failure without internal fallback, and cleanup");
}
'''
def main():
    lvgl = ROOT / 'managed_components/lvgl__lvgl'
    cjson = ROOT / 'managed_components/espressif__cjson/cJSON'
    with tempfile.TemporaryDirectory(prefix='crystal-memory-') as directory:
        work = Path(directory)
        (work / 'esp_heap_caps.h').write_text('''#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1024
#define MALLOC_CAP_8BIT 4
void *heap_caps_malloc(size_t, unsigned);
void *heap_caps_realloc(void *, size_t, unsigned);
void heap_caps_free(void *);
''')
        (work / 'esp_log.h').write_text('#define ESP_LOGI(...) ((void)0)' + chr(10))
        (work / 'lv_conf.h').write_text('''#pragma once
#define LV_CONF_H
#define LV_MEM_CUSTOM 1
#define LV_MEMCPY_MEMSET_STD 1
#define LV_USE_LOG 0
#define LV_MEM_CUSTOM_INCLUDE "crystal_memory.h"
#define LV_MEM_CUSTOM_ALLOC crystal_ui_malloc
#define LV_MEM_CUSTOM_REALLOC crystal_ui_realloc
#define LV_MEM_CUSTOM_FREE crystal_ui_free
''')
        (work / 'check.c').write_text(HARNESS)
        subprocess.run(['cc', '-std=c11', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', f'-DLV_CONF_PATH={work}/lv_conf.h',
                        '-I', str(work), '-I', str(ROOT / 'main'),
                        '-I', str(lvgl), '-I', str(cjson),
                        str(work / 'check.c'), str(ROOT / 'main/crystal_memory.c'),
                        str(lvgl / 'src/misc/lv_mem.c'), str(lvgl / 'src/misc/lv_gc.c'),
                        str(cjson / 'cJSON.c'), '-lm', '-o', str(work / 'check')], check=True)
        subprocess.run([str(work / 'check')], check=True)
if __name__ == '__main__':
    main()
