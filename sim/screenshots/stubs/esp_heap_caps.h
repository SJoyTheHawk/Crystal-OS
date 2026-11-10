#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 0
inline void *heap_caps_malloc(size_t size, unsigned) { return std::malloc(size); }
inline size_t heap_caps_get_free_size(unsigned) { return 1024 * 1024; }
inline void *heap_caps_aligned_alloc(size_t alignment, size_t size, unsigned)
{
    void *memory = nullptr;
    return posix_memalign(&memory, alignment, size) == 0 ? memory : nullptr;
}
