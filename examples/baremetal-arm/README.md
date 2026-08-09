# Baremetal ARM Example Platform

This is a minimal example platform implementation for baremetal ARM systems (e.g., Raspberry Pi, ARMv7-M microcontrollers).

## Usage

This example demonstrates how to implement nc_platform for a baremetal ARM system without an OS. It provides:

- Simple memory allocation using a static heap
- MMIO access using direct volatile pointer access
- Basic spinlock implementation
- Timer using ARM SysTick
- UART logging

## Building

Requires ARM cross-compiler:
```bash
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -c platform_arm.c
```

## Integration

Copy this example as a starting point for your baremetal ARM OS, then adapt:
- Memory allocation to match your heap implementation
- Timer to use your system tick
- Logging to your UART driver
- Interrupt handling to match your NVIC configuration
