# architecture

## goals

- one small interface, nc_platform, that any os or kernel implements to get access to every driver in the tree.
- drivers never call into libc, never call into a specific kernel, and never assume a threading model. everything they need comes through nc_platform or is passed in through nc_device.
- devices are discovered by the host os, not by libnetconnect. bus enumeration, PCI/USB/SDIO walking, and resource allocation are the responsibility of the integrating os. libnetconnect receives an already populated nc_device describing bars, irq line, and ids.

## layers

host os
  -> enumerates bus, fills nc_device
  -> implements nc_platform
  -> calls driver probe

libnetconnect core
  -> registry of drivers
  -> shared helper code, not hardware specific

driver
  -> pure logic against nc_platform and nc_device
  -> no os specific code anywhere

## the platform interface

nc_platform covers five categories of primitives a driver needs regardless of target:

1. general purpose memory, alloc and free.
2. dma safe memory, physically contiguous and mapped for device access.
3. mmio access, so drivers do not read or write raw pointers directly and the host can insert barriers or tracing.
4. timing, both a monotonic clock and a blocking sleep.
5. locking and interrupt registration, kept intentionally minimal.

expect nc_platform to grow. the version in include/nc/platform.h is a starting point, not the final shape. any addition needs a real driver that requires it before it gets merged.

## driver contract

every driver exports a struct matching its device class, for example nc_net_driver for anything ethernet or wifi shaped. the struct is a plain table of function pointers: probe, remove, and class specific operations like send and recv for networking. drivers register themselves through the core registry so a host can enumerate what is available without hardcoding driver names.

## why virtio-net first

virtio has a public, stable specification, requires no closed firmware blob, and runs identically under qemu regardless of host os, which makes it the best first target to validate that the platform interface is sufficient before tackling messier real hardware.
