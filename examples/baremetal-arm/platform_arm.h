#ifndef PLATFORM_ARM_H
#define PLATFORM_ARM_H

#include "nc/platform.h"
#include <stdint.h>

/* Simple heap configuration */
#define HEAP_SIZE (1024 * 1024) /* 1MB heap */

/* SysTick configuration */
#define SYSTICK_LOAD_RELOAD (72000 - 1) /* 1ms at 72MHz */

/* Spinlock structure */
struct arm_lock {
    volatile uint32_t locked;
};

/* Platform initialization */
int platform_arm_init(void);
struct nc_platform *platform_arm_get(void);

#endif
