/* Host-only stand-ins for the system allocator. */
#ifndef NET_BUF_TEST_MM_H
#define NET_BUF_TEST_MM_H
#include <stdint.h>
#include <stdlib.h>
static inline void *kmalloc(size_t size, uint32_t flags)
{
    (void)flags;
    return malloc(size);
}
static inline void *kmemalign(size_t size, size_t alignment)
{
    void *data = NULL;
    return posix_memalign(&data, alignment, size) == 0 ? data : NULL;
}
static inline void kfree(void *data)
{
    free(data);
}
#endif
