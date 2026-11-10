/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_resources.hpp"
#include <stdlib.h>
#include <atomic>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace { std::atomic<uint32_t> s_next_handle{1}; }

CrystalResources::Handle CrystalResources::track(CrystalResourceKind kind, void *resource, size_t bytes)
{
    if (!resource) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (bytes > limit_ - used_) return 0;
    unsigned tasks = 0;
    for (const auto &row : rows_) {
        if (row.handle && row.kind == kind && row.resource == resource) return 0;
        if (row.handle && row.kind == CrystalResourceKind::Task) ++tasks;
    }
    if (kind == CrystalResourceKind::Task && tasks >= workers_) return 0;
    for (auto &row : rows_) if (!row.handle) {
        uint32_t handle = s_next_handle.load();
        do { if (handle == UINT32_MAX) return 0; }
        while (!s_next_handle.compare_exchange_weak(handle, handle + 1));
        row = {handle, kind, resource, bytes};
        used_ += bytes;
        return row.handle;
    }
    return 0;
}

void *CrystalResources::lookup(Handle handle, CrystalResourceKind kind) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &row : rows_) if (handle && row.handle == handle && row.kind == kind) return row.resource;
    return nullptr;
}

bool CrystalResources::release(Handle handle)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &row : rows_) if (handle && row.handle == handle) {
        used_ -= row.bytes;
        row = {};
        return true;
    }
    return false;
}

bool CrystalResources::release(CrystalResourceKind kind, void *resource)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &row : rows_) if (row.handle && row.kind == kind && row.resource == resource) {
        used_ -= row.bytes;
        row = {};
        return true;
    }
    return false;
}

void *CrystalResources::allocate(size_t bytes)
{
    if (!bytes || bytes > limit_) return nullptr;
#ifdef ESP_PLATFORM
    void *memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void *memory = malloc(bytes);
#endif
    if (memory && !track(CrystalResourceKind::Allocation, memory, bytes)) {
        free(memory);
        return nullptr;
    }
    return memory;
}

bool CrystalResources::deallocate(void *memory)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &row : rows_) if (row.handle && row.kind == CrystalResourceKind::Allocation && row.resource == memory) {
        used_ -= row.bytes;
        row = {};
        free(memory);
        return true;
    }
    return false;
}

CrystalCleanupReport CrystalResources::report() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    CrystalCleanupReport result;
    for (const auto &row : rows_) if (row.handle) {
        switch (row.kind) {
        case CrystalResourceKind::Task: ++result.tasks; break;
        case CrystalResourceKind::Timer: ++result.timers; break;
        case CrystalResourceKind::Queue: ++result.queues; break;
        case CrystalResourceKind::Allocation: ++result.allocations; break;
        case CrystalResourceKind::Object: ++result.objects; break;
        }
        result.heap_bytes += row.bytes;
    }
    return result;
}

bool CrystalResources::configure(size_t memory_limit, unsigned workers)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &row : rows_) if (row.handle) return false;
    limit_ = memory_limit;
    workers_ = workers;
    return true;
}

bool crystal_app_verify_cleanup(const CrystalResources &resources, CrystalCleanupReport *report)
{
    if (!report) return false;
    *report = resources.report();
    return report->clean();
}
