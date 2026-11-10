/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <array>
#include <mutex>

enum class CrystalResourceKind : uint8_t { Task, Timer, Queue, Allocation, Object };
struct CrystalCleanupReport {
    size_t tasks = 0, timers = 0, queues = 0, allocations = 0, objects = 0, heap_bytes = 0;
    bool clean() const { return !(tasks || timers || queues || allocations || objects); }
};

// One ledger per app instance. Services must register before publishing a
// resource, and release only after it has stopped and can no longer call back.
// Never clears a ledger to hide leaks or forcibly deletes a running worker.
class CrystalResources final {
public:
    explicit CrystalResources(size_t memory_limit = 160 * 1024, unsigned workers = 2)
        : limit_(memory_limit), workers_(workers) {}
    using Handle = uint32_t;
    Handle track(CrystalResourceKind kind, void *resource, size_t bytes = 0);
    void *lookup(Handle handle, CrystalResourceKind kind) const;
    bool release(Handle handle);
    bool release(CrystalResourceKind kind, void *resource);
    void *allocate(size_t bytes);
    bool deallocate(void *memory);
    CrystalCleanupReport report() const;
    bool configure(size_t memory_limit, unsigned workers);
private:
    struct Row { Handle handle = 0; CrystalResourceKind kind{}; void *resource = nullptr; size_t bytes = 0; };
    mutable std::mutex mutex_;
    std::array<Row, 128> rows_{};
    size_t limit_, used_ = 0;
    unsigned workers_;
};

bool crystal_app_verify_cleanup(const CrystalResources &resources, CrystalCleanupReport *report);
