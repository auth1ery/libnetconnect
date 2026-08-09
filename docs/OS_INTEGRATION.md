# OS Integration Guide

This guide explains how to integrate libnetconnect into your own operating system, separate from Linux or other existing platforms.

## Overview

libnetconnect provides hardware drivers through a small, well-defined interface. Your OS needs to implement two main components:

1. **nc_platform** - A set of function pointers that provide OS services to drivers
2. **Device enumeration** - Code that discovers hardware and populates nc_device structures

## Step 1: Implement nc_platform

The nc_platform interface is defined in `include/nc/platform.h`. You must implement each function pointer:

### Memory Management

```c
void *my_alloc(size_t size) {
    // Return pointer to allocated memory
    // Can use your kernel's heap allocator
}

void my_free(void *ptr) {
    // Free memory allocated by my_alloc
}
```

### DMA Memory

```c
void *my_dma_alloc(size_t size, uint64_t *phys_out) {
    // Allocate physically contiguous memory
    // Return virtual pointer and physical address
    // May need to disable caching for this memory
}

void my_dma_free(void *virt, uint64_t phys, size_t size) {
    // Free DMA memory and restore caching
}
```

**Important**: Never assume virtual and physical addresses are the same. Some systems use IOMMU or have virtual memory enabled.

### MMIO Access

```c
uint32_t my_mmio_read32(volatile void *addr) {
    // Read 32-bit value from MMIO address
    // Insert memory barriers if needed for your architecture
    return *(volatile uint32_t *)addr;
}

void my_mmio_write32(volatile void *addr, uint32_t val) {
    // Write 32-bit value to MMIO address
    // Insert memory barriers if needed
    *(volatile uint32_t *)addr = val;
}
```

**Important**: Always go through these functions, never dereference BAR pointers directly. This allows for barriers and simulation.

### Timing

```c
uint64_t my_time_ns(void) {
    // Return monotonic time in nanoseconds
    // Use your kernel's timer or CPU timestamp counter
}

void my_sleep_ms(uint32_t ms) {
    // Block current thread for specified milliseconds
    // Can be implemented with timer or busy-wait
}
```

### Locking

```c
typedef struct {
    // Your lock implementation (spinlock, mutex, etc.)
} my_lock_t;

my_lock_t *my_lock_create(void) {
    // Allocate and initialize a lock
}

void my_lock_acquire(my_lock_t *lock) {
    // Acquire lock
}

void my_lock_release(my_lock_t *lock) {
    // Release lock
}

void my_lock_destroy(my_lock_t *lock) {
    // Free lock resources
}
```

### Interrupts

```c
int my_irq_request(uint32_t irq, void (*handler)(void *), void *ctx) {
    // Register interrupt handler for IRQ line
    // Return 0 on success, -1 on failure
    // If interrupts not available, return -1 and drivers will use polling
}

void my_irq_free(uint32_t irq) {
    // Unregister interrupt handler
}
```

**Important**: Drivers must work with both interrupts and polling. If irq_request returns -1, drivers will fall back to polling.

### Logging

```c
void my_log(const char *fmt, ...) {
    // Log message using your kernel's logging system
    // Can use va_list for variable arguments
}
```

### Firmware Loading (Optional)

```c
int my_firmware_load(const char *name, void **data, size_t *len) {
    // Load firmware blob by name
    // Return 0 on success, -1 on failure
    // Set data and len on success
}

void my_firmware_free(void *data) {
    // Free firmware data
}
```

**Important**: Only needed for WiFi drivers (iwlwifi, ath9k, rtl8188). Can return -1 if not implementing WiFi.

### Register Platform

```c
struct nc_platform my_platform = {
    .alloc = my_alloc,
    .free = my_free,
    .dma_alloc = my_dma_alloc,
    .dma_free = my_dma_free,
    .mmio_read32 = my_mmio_read32,
    .mmio_write32 = my_mmio_write32,
    .time_ns = my_time_ns,
    .sleep_ms = my_sleep_ms,
    .lock_create = my_lock_create,
    .lock_acquire = my_lock_acquire,
    .lock_release = my_lock_release,
    .lock_destroy = my_lock_destroy,
    .irq_request = my_irq_request,
    .irq_free = my_irq_free,
    .log = my_log,
    .firmware_load = my_firmware_load,
    .firmware_free = my_firmware_free,
};

// During kernel initialization
nc_platform_register(&my_platform);
```

## Step 2: Implement Device Enumeration

Your OS is responsible for discovering hardware and populating nc_device structures:

### PCI Enumeration Example

```c
void my_pci_scan(void) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            for (uint32_t func = 0; func < 8; func++) {
                uint16_t vendor_id = pci_read_config16(bus, slot, func, 0);
                uint16_t device_id = pci_read_config16(bus, slot, func, 2);
                
                if (vendor_id == 0xFFFF) continue; // No device
                
                struct nc_device dev = {0};
                dev.vendor_id = vendor_id;
                dev.device_id = device_id;
                
                // Map BARs
                for (int i = 0; i < 6; i++) {
                    uint32_t bar = pci_read_config32(bus, slot, func, 0x10 + i * 4);
                    if (bar & 1) {
                        // I/O space
                        dev.bars[i].phys_addr = bar & ~3;
                        dev.bars[i].virt_addr = (void *)(bar & ~3);
                    } else {
                        // Memory space
                        dev.bars[i].phys_addr = bar & ~15;
                        dev.bars[i].virt_addr = my_map_mmio(bar & ~15, bar & 0xFFFFFFF0);
                        dev.bars[i].size = bar & 0xFFFFFFF0;
                    }
                }
                
                dev.irq = pci_get_irq(bus, slot, func);
                
                // Try to probe drivers
                my_probe_device(&dev);
            }
        }
    }
}
```

## Step 3: Probe Drivers

```c
 void my_probe_device(struct nc_device *dev) {
    // Try network drivers
    for (int i = 0; i < nc_net_driver_count(); i++) {
        struct nc_net_driver *drv = nc_get_net_driver(i);
        if (drv->probe(dev) == 0) {
            my_log("Driver %s matched device %04x:%04x\n", 
                   drv->name, dev->vendor_id, dev->device_id);
            // Store driver association in your OS device tree
            return;
        }
    }
    
    // Try WiFi drivers
    for (int i = 0; i < nc_wifi_driver_count(); i++) {
        struct nc_wifi_driver *drv = nc_get_wifi_driver(i);
        if (drv->probe(dev) == 0) {
            my_log("WiFi driver %s matched device %04x:%04x\n",
                   drv->name, dev->vendor_id, dev->device_id);
            return;
        }
    }
}
```

## Step 4: Initialize Drivers

```c
// During kernel initialization, after platform is registered
nc_drivers_init();
```

This registers all built-in drivers (virtio-net, e1000, r8169, iwlwifi, ath9k, rtl8188).

## Step 5: Use Drivers

### Network Driver Usage

```c
// Send a packet
struct nc_device *net_dev = my_get_network_device();
const uint8_t packet[] = { /* Ethernet frame */ };
int ret = net_dev->net_driver->send(net_dev, packet, sizeof(packet));

// Receive a packet
uint8_t buffer[1518];
int len = net_dev->net_driver->recv(net_dev, buffer, sizeof(buffer));
if (len > 0) {
    // Process received packet
}

// Get MAC address
uint8_t mac[6];
net_dev->net_driver->get_mac(net_dev, mac);
```

### WiFi Driver Usage

```c
// Scan for networks
struct nc_wifi_network networks[NC_MAX_NETWORKS];
int count = 0;
int ret = wifi_dev->wifi_driver->scan(wifi_dev, networks, &count);

// Connect to network
ret = wifi_dev->wifi_driver->connect(wifi_dev, "MySSID", "password");

// Check connection status
char ssid[NC_MAX_SSID_LEN + 1];
int connected;
ret = wifi_dev->wifi_driver->get_status(wifi_dev, ssid, &connected);
```

## Memory Management Considerations

### DMA Memory

- DMA memory must be physically contiguous
- May need to disable caching for DMA regions
- Some architectures require specific alignment
- Virtual and physical addresses may differ

### Error Paths

- Always clean up allocations on error
- Drivers expect proper cleanup in remove() function
- Track all DMA allocations for proper cleanup

### Example Cleanup Pattern

```c
static int my_driver_probe(struct nc_device *dev) {
    void *ring1 = plat->dma_alloc(size1, &phys1);
    if (!ring1) return -1;
    
    void *ring2 = plat->dma_alloc(size2, &phys2);
    if (!ring2) {
        plat->dma_free(ring1, phys1, size1);
        return -1;
    }
    
    // Store in private data for cleanup
    struct my_priv *priv = plat->alloc(sizeof(*priv));
    priv->ring1 = ring1;
    priv->ring2 = ring2;
    dev->driver_data = priv;
    
    return 0;
}

static void my_driver_remove(struct nc_device *dev) {
    struct my_priv *priv = dev->driver_data;
    plat->dma_free(priv->ring1, priv->phys1, priv->size1);
    plat->dma_free(priv->ring2, priv->phys2, priv->size2);
    plat->free(priv);
}
```

## Interrupt Handling

### Interrupt Handler Registration

```c
void my_network_irq_handler(void *ctx) {
    struct nc_device *dev = (struct nc_device *)ctx;
    // Handle interrupt, possibly call driver-specific handler
}

// During probe
plat->irq_request(dev->irq, my_network_irq_handler, dev);
```

### Polling Fallback

If your OS doesn't support interrupts or irq_request returns -1, drivers will use polling:

```c
// In your network receive loop
while (1) {
    uint8_t buffer[1518];
    int len = dev->net_driver->recv(dev, buffer, sizeof(buffer));
    if (len > 0) {
        // Process packet
    }
    my_sleep_ms(1); // Avoid busy-wait
}
```

## Testing Without Hardware

### QEMU Integration

Test virtio-net driver with QEMU:

```bash
qemu-system-x86_64 -kernel my_os.elf \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0
```

### Userspace Testing

Use the Linux userspace shim to test driver compilation and basic logic:

```bash
cd examples/linux-userspace
make
./nc_linux_shim
```

## Common Pitfalls

### 1. Not Implementing All Platform Functions

All platform functions must be implemented, even if they just return errors. Drivers will check for NULL function pointers.

### 2. Incorrect DMA Memory

- Not using physically contiguous memory
- Assuming virtual == physical addresses
- Not disabling caching for DMA regions
- Forgetting to free DMA memory

### 3. Missing Error Cleanup

- Not cleaning up allocations on error paths
- Not calling driver remove() on probe failure
- Memory leaks in error handling

### 4. Interrupt vs Polling

- Assuming interrupts are always available
- Not implementing polling fallback
- Not handling both paths correctly

### 5. MMIO Access

- Dereferencing BAR pointers directly
- Not using mmio_read32/mmio_write32
- Missing memory barriers on some architectures

### 6. Firmware Loading

- Not implementing firmware_load for WiFi drivers
- Not checking for NULL firmware_load before calling
- Not freeing firmware data after use

## Architecture-Specific Notes

### x86

- Use I/O ports for legacy devices
- MMIO for PCI devices
- Memory barriers usually not needed for x86
- Watch out for IOMMU in modern systems

### ARM

- MMIO only (no I/O ports)
- Memory barriers critical (DSB/ISB)
- Cache coherency important for DMA
- Watch for different page sizes

### RISC-V

- MMIO only
- Memory barriers (fence.i, fence)
- Cache coherency varies by implementation
- Watch for S-mode vs M-mode differences

## Example: Minimal x86 OS Integration

```c
// my_os.c
#include "nc/platform.h"
#include "nc/registry.h"

// Simple heap (replace with your allocator)
static uint8_t heap[1MB];
static size_t heap_offset = 0;

void *my_alloc(size_t size) {
    void *ptr = &heap[heap_offset];
    heap_offset += size;
    return ptr;
}

void my_free(void *ptr) {
    // No-op for simple heap
}

// MMIO access (x86 doesn't need barriers)
uint32_t my_mmio_read32(volatile void *addr) {
    return *(volatile uint32_t *)addr;
}

void my_mmio_write32(volatile void *addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

// Simple spinlock
typedef struct { volatile int locked; } my_lock_t;

my_lock_t *my_lock_create(void) {
    my_lock_t *lock = my_alloc(sizeof(*lock));
    lock->locked = 0;
    return lock;
}

void my_lock_acquire(my_lock_t *lock) {
    while (__sync_lock_test_and_set(&lock->locked, 1));
}

void my_lock_release(my_lock_t *lock) {
    lock->locked = 0;
}

// Stub implementations for optional functions
void *my_dma_alloc(size_t size, uint64_t *phys) {
    void *ptr = my_alloc(size);
    *phys = (uint64_t)ptr;
    return ptr;
}

void my_dma_free(void *virt, uint64_t phys, size_t size) {
    my_free(virt);
}

uint64_t my_time_ns(void) {
    // Use RDTSC or PIT
    return 0;
}

void my_sleep_ms(uint32_t ms) {
    // Busy-wait or use timer
}

int my_irq_request(uint32_t irq, void (*handler)(void *), void *ctx) {
    return -1; // Not implemented
}

void my_irq_free(uint32_t irq) {
}

void my_log(const char *fmt, ...) {
    // Use VGA text mode or serial
}

int my_firmware_load(const char *name, void **data, size_t *len) {
    return -1; // Not implemented
}

void my_firmware_free(void *data) {
}

// Platform registration
struct nc_platform my_platform = {
    .alloc = my_alloc,
    .free = my_free,
    .dma_alloc = my_dma_alloc,
    .dma_free = my_dma_free,
    .mmio_read32 = my_mmio_read32,
    .mmio_write32 = my_mmio_write32,
    .time_ns = my_time_ns,
    .sleep_ms = my_sleep_ms,
    .lock_create = my_lock_create,
    .lock_acquire = my_lock_acquire,
    .lock_release = my_lock_release,
    .lock_destroy = my_lock_destroy,
    .irq_request = my_irq_request,
    .irq_free = my_irq_free,
    .log = my_log,
    .firmware_load = my_firmware_load,
    .firmware_free = my_firmware_free,
};

void kernel_main(void) {
    // Initialize platform
    nc_platform_register(&my_platform);
    
    // Initialize drivers
    nc_drivers_init();
    
    // Enumerate PCI devices
    my_pci_scan();
    
    // Now drivers are ready to use
}
```

## Getting Help

- See `docs/DRIVER_GUIDE.md` for driver writing guidance
- See `docs/WIFI_GUIDE.md` for WiFi-specific information
- See `examples/` for reference platform implementations
- Check `drivers/*/REFERENCES.md` for hardware-specific information
