#include "nc/platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>

struct nc_lock {
    pthread_mutex_t mtx;
};

static void *linux_alloc(size_t size)
{
    return malloc(size);
}

static void linux_free(void *ptr)
{
    free(ptr);
}

static void *linux_dma_alloc(size_t size, uint64_t *phys_out)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t aligned = (size + page - 1) & ~(page - 1);

    void *mem = mmap(NULL, aligned, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_LOCKED, -1, 0);
    if (mem == MAP_FAILED) {
        mem = mmap(NULL, aligned, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) return NULL;
    }

    memset(mem, 0, aligned);

    if (phys_out) {
        *phys_out = (uint64_t)(uintptr_t)mem;
    }

    return mem;
}

static void linux_dma_free(void *virt, uint64_t phys, size_t size)
{
    (void)phys;
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t aligned = (size + page - 1) & ~(page - 1);
    munmap(virt, aligned);
}

static uint32_t linux_mmio_read32(volatile void *addr)
{
    return *(volatile uint32_t *)addr;
}

static void linux_mmio_write32(volatile void *addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}

static uint64_t linux_mmio_read64(volatile void *addr)
{
    return *(volatile uint64_t *)addr;
}

static void linux_mmio_write64(volatile void *addr, uint64_t val)
{
    *(volatile uint64_t *)addr = val;
}

static uint64_t linux_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void linux_sleep_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static nc_lock_t *linux_lock_create(void)
{
    nc_lock_t *lock = malloc(sizeof(nc_lock_t));
    if (!lock) return NULL;
    pthread_mutex_init(&lock->mtx, NULL);
    return lock;
}

static void linux_lock_acquire(nc_lock_t *lock)
{
    if (lock) pthread_mutex_lock(&lock->mtx);
}

static void linux_lock_release(nc_lock_t *lock)
{
    if (lock) pthread_mutex_unlock(&lock->mtx);
}

static void linux_lock_destroy(nc_lock_t *lock)
{
    if (!lock) return;
    pthread_mutex_destroy(&lock->mtx);
    free(lock);
}

static int linux_irq_request(uint32_t irq, void (*handler)(void *ctx), void *ctx)
{
    (void)irq;
    (void)handler;
    (void)ctx;
    /* Userspace has no real IRQ path, drivers must fall back to polling */
    return -1;
}

static void linux_irq_free(uint32_t irq)
{
    (void)irq;
}

static void linux_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

static int linux_firmware_load(const char *name, void **data, size_t *len)
{
    /* Userspace shim doesn't implement firmware loading */
    (void)name;
    (void)data;
    (void)len;
    return -1;
}

static void linux_firmware_free(void *data)
{
    (void)data;
}

struct nc_platform g_linux_platform = {
    .alloc = linux_alloc,
    .free = linux_free,
    .dma_alloc = linux_dma_alloc,
    .dma_free = linux_dma_free,
    .mmio_read32 = linux_mmio_read32,
    .mmio_write32 = linux_mmio_write32,
    .mmio_read64 = linux_mmio_read64,
    .mmio_write64 = linux_mmio_write64,
    .time_ns = linux_time_ns,
    .sleep_ms = linux_sleep_ms,
    .lock_create = linux_lock_create,
    .lock_acquire = linux_lock_acquire,
    .lock_release = linux_lock_release,
    .lock_destroy = linux_lock_destroy,
    .irq_request = linux_irq_request,
    .irq_free = linux_irq_free,
    .log = linux_log,
    .firmware_load = linux_firmware_load,
    .firmware_free = linux_firmware_free,
};
