#ifndef NC_PLATFORM_H
#define NC_PLATFORM_H

#include <stdint.h>
#include <stddef.h>

typedef struct nc_lock nc_lock_t;

struct nc_platform {
    void *(*alloc)(size_t size);
    void (*free)(void *ptr);
    void *(*dma_alloc)(size_t size, uint64_t *phys_out);
    void (*dma_free)(void *virt, uint64_t phys, size_t size);
    uint32_t (*mmio_read32)(volatile void *addr);
    void (*mmio_write32)(volatile void *addr, uint32_t val);
    uint64_t (*time_ns)(void);
    void (*sleep_ms)(uint32_t ms);
    nc_lock_t *(*lock_create)(void);
    void (*lock_acquire)(nc_lock_t *lock);
    void (*lock_release)(nc_lock_t *lock);
    void (*lock_destroy)(nc_lock_t *lock);
    int (*irq_request)(uint32_t irq, void (*handler)(void *ctx), void *ctx);
    void (*irq_free)(uint32_t irq);
    void (*log)(const char *fmt, ...);
};

int nc_platform_register(struct nc_platform *plat);
struct nc_platform *nc_platform_get(void);

#endif
