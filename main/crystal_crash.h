#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRASH_BACKTRACE_DEPTH 16

typedef struct {
    uint8_t version;
    uint8_t reset_reason;
    uint8_t reserved[2];
    uint32_t pc;
    uint32_t backtrace[CRASH_BACKTRACE_DEPTH];
    uint8_t elf_sha[32];
    uint64_t timestamp_ms;
    char task_name[16];
    bool is_corrupted;
} crash_summary_t;

void crystal_crash_init(void);
bool crystal_crash_get_last(crash_summary_t *out);
void crystal_crash_mark_displayed(void);
bool crystal_crash_should_notify(void);

#ifdef __cplusplus
}
#endif
