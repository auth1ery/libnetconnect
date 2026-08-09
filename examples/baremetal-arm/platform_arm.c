#include "platform_arm.h"
#include <string.h>

/* Static heap for memory allocation */
static uint8_t g_heap[HEAP_SIZE];
static size_t g_heap_offset = 0;

/* Platform instance */
static struct nc_platform g_platform;

/* Simple memory allocation */
static void *arm_alloc(size_t size)
{
    if (g_heap_offset + size > HEAP_SIZE) return NULL;
    void *ptr = &g_heap[g_heap_offset];
    g_heap_offset += size;
    return ptr;
}

static void arm_free(void *ptr)
{
    /* No-op for simple heap - in real implementation use proper allocator */
    (void)ptr;
}

/* DMA allocation - for baremetal, just use regular memory */
static void *arm_dma_alloc(size_t size, uint64_t *phys_out)
{
    void *ptr = arm_alloc(size);
    if (ptr && phys_out) {
        *phys_out = (uint64_t)(uintptr_t)ptr;
    }
    return ptr;
}

static void arm_dma_free(void *virt, uint64_t phys, size_t size)
{
    (void)phys;
    arm_free(virt);
    (void)size;
}

/* MMIO access */
static uint32_t arm_mmio_read32(volatile void *addr)
{
    return *(volatile uint32_t *)addr;
}

static void arm_mmio_write32(volatile void *addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}

/* Simple timer using SysTick */
static volatile uint32_t g_ticks = 0;

void SysTick_Handler(void)
{
    g_ticks++;
}

static uint64_t arm_time_ns(void)
{
    return g_ticks * 1000000ULL; /* 1ms ticks */
}

static void arm_sleep_ms(uint32_t ms)
{
    uint32_t start = g_ticks;
    while (g_ticks - start < ms) {
        __asm__ volatile ("wfi");
    }
}

/* Spinlock implementation */
static nc_lock_t *arm_lock_create(void)
{
    struct arm_lock *lock = arm_alloc(sizeof(struct arm_lock));
    if (lock) lock->locked = 0;
    return (nc_lock_t *)lock;
}

static void arm_lock_acquire(nc_lock_t *lock)
{
    struct arm_lock *l = (struct arm_lock *)lock;
    while (__sync_lock_test_and_set(&l->locked, 1)) {
        __asm__ volatile ("nop");
    }
    __sync_synchronize();
}

static void arm_lock_release(nc_lock_t *lock)
{
    struct arm_lock *l = (struct arm_lock *)lock;
    __sync_synchronize();
    l->locked = 0;
}

static void arm_lock_destroy(nc_lock_t *lock)
{
    arm_free(lock);
}

/* Interrupt handling - stub for example */
static int arm_irq_request(uint32_t irq, void (*handler)(void *), void *ctx)
{
    (void)irq;
    (void)handler;
    (void)ctx;
    return -1; /* Not implemented in this example */
}

static void arm_irq_free(uint32_t irq)
{
    (void)irq;
}

/* Simple UART logging */
static void arm_log(const char *fmt, ...)
{
    /* Stub - in real implementation, use UART */
    (void)fmt;
}

int platform_arm_init(void)
{
    g_heap_offset = 0;
    g_ticks = 0;

    /* Initialize SysTick for 1ms ticks */
    *(volatile uint32_t *)0xE000E010 = SYSTICK_LOAD_RELOAD; /* Reload value */
    *(volatile uint32_t *)0xE000E014 = 0; /* Current value */
    *(volatile uint32_t *)0xE000E018 = 7; /* Control: enable, interrupt, processor clock */

    g_platform.alloc = arm_alloc;
    g_platform.free = arm_free;
    g_platform.dma_alloc = arm_dma_alloc;
    g_platform.dma_free = arm_dma_free;
    g_platform.mmio_read32 = arm_mmio_read32;
    g_platform.mmio_write32 = arm_mmio_write32;
    g_platform.time_ns = arm_time_ns;
    g_platform.sleep_ms = arm_sleep_ms;
    g_platform.lock_create = arm_lock_create;
    g_platform.lock_acquire = arm_lock_acquire;
    g_platform.lock_release = arm_lock_release;
    g_platform.lock_destroy = arm_lock_destroy;
    g_platform.irq_request = arm_irq_request;
    g_platform.irq_free = arm_irq_free;
    g_platform.log = arm_log;

    return nc_platform_register(&g_platform);
}

struct nc_platform *platform_arm_get(void)
{
    return &g_platform;
}
